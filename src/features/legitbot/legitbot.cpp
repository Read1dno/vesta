#include <stdafx.hpp>
#include <core/math/ray_capsule.hpp>
#include <features/legitbot/legitbot.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/visuals/event_log.hpp>
#include <core/input/bindings.hpp>
#include <simulation/grenade.hpp>
#include <simulation/seed_timing.hpp>
#include <simulation/shot_state.hpp>

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
#include <fstream>
#endif

namespace features::legitbot {
	using simulation::ballistics;
	using simulation::ballistics_t;

	namespace {
		[[nodiscard]] bool is_host_seed_session( std::uintptr_t controller )
		{
			if ( !controller )
			{
				return false;
			}
			return app::context().process.load<std::int32_t>(
				controller + SCHEMA( "CCSPlayerController", "m_iPing"_id ) ) <= 0;
		}

		[[nodiscard]] bool is_host_seed_session( )
		{
			return is_host_seed_session( game::local_player().controller( ) );
		}

		[[nodiscard]] int read_seed_tick( std::uintptr_t controller,
			std::uintptr_t pawn, bool host_session )
		{
			int host_tick{ -1 };
			if ( host_session )
			{
				const auto global_vars = app::context().process.load<std::uintptr_t>(
					app::context().addresses.global_vars );
				host_tick = global_vars
					? app::context().process.load<int>( global_vars + 0x44, -1 ) : -1;
			}
			const auto simulation_tick = pawn
				? app::context().process.load<int>( pawn
					+ SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id ), -1 ) : -1;
			const auto tick_base = controller
				? app::context().process.load<int>( controller
					+ SCHEMA( "CBasePlayerController", "m_nTickBase"_id ), -1 ) : -1;
			return simulation::seed_timing::select_tick(
				host_session, host_tick, simulation_tick, tick_base );
		}

		[[nodiscard]] int read_seed_tick( )
		{
			const auto controller = game::local_player().controller( );
			const auto pawn = game::local_player().pawn( );
			return read_seed_tick(
				controller, pawn, is_host_seed_session( controller ) );
		}

		struct cmd_rincamera
		{
			std::uintptr_t base{};
			int sequence{};
		};

		struct aim_command_prediction
		{
			int source_sequence{ -1 };
			foundation::vec3 raw_angles{};
			std::chrono::steady_clock::time_point published_at{};
		};

		inline std::mutex g_aim_command_prediction_mutex{};
		inline aim_command_prediction g_aim_command_prediction{};

		void publish_aim_command_prediction( int source_sequence,
			const foundation::vec3& raw_angles,
			std::chrono::steady_clock::time_point published_at )
		{
			if ( source_sequence < 0 ) return;
			std::scoped_lock lock( g_aim_command_prediction_mutex );
			g_aim_command_prediction = { source_sequence, raw_angles, published_at };
		}

		[[nodiscard]] std::optional<foundation::vec3> predicted_aim_command(
			int source_sequence, std::chrono::steady_clock::time_point now )
		{
			std::scoped_lock lock( g_aim_command_prediction_mutex );
			if ( g_aim_command_prediction.source_sequence != source_sequence
				|| now - g_aim_command_prediction.published_at
					> std::chrono::milliseconds( 32 ) )
			{
				return std::nullopt;
			}
			return g_aim_command_prediction.raw_angles;
		}

		inline int stable_tick_offset( int tick, int sequence )
		{
			constexpr auto k_slots = 16;
			struct entry { int offset; int hits; };
			static std::array<entry, k_slots> table{};
			static auto best = 0;
			static auto seeded = false;

			const auto observed = tick - sequence;
			auto free_slot = -1;
			for ( int i = 0; i < k_slots; ++i )
			{
				if ( table[ i ].hits > 0 && table[ i ].offset == observed )
				{
					++table[ i ].hits;
					if ( !seeded || table[ i ].hits > 4 )
					{
						best = observed;
						seeded = true;
					}
					return best;
				}
				if ( table[ i ].hits == 0 && free_slot < 0 )
				{
					free_slot = i;
				}
			}
			// A brand new offset - a reconnect or a genuine drift. Give it a slot; it only
			// becomes the answer once it has been seen more than the incumbent.
			if ( free_slot >= 0 )
			{
				table[ free_slot ] = { observed, 1 };
			}
			else
			{
				table = {};
				table[ 0 ] = { observed, 1 };
				seeded = false;
			}
			if ( !seeded )
			{
				best = observed;
				seeded = true;
			}
			return best;
		}

		[[nodiscard]] bool validate_cmd_ring( std::uintptr_t base, int& out_sequence )
		{
			if ( base < 0x10000 || base > 0x00007fffffffffffULL )
			{
				return false;
			}
			// 150 slots * 152 bytes = 22800 = 0x5910: the counter sits immediately after
			// the ring it belongs to, so this offset is structural rather than guessed.
			constexpr std::uintptr_t k_counter_offset{ 150 * 152 };
			static_assert( k_counter_offset == 0x5910 );
			const auto counter = app::context().process.load<int>( base + k_counter_offset );
			if ( counter <= 0 )
			{
				return false;
			}
			for ( int back = 0; back <= 1; ++back )
			{
				const auto candidate = counter - back;
				if ( candidate <= 0 )
				{
					break;
				}
				const auto command = base +
					static_cast<std::uintptr_t>( 152 ) * ( candidate % 150 );
				if ( app::context().process.load<int>( command + 0x8 ) == candidate )
				{
					out_sequence = candidate;
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool read_cmd_ring( std::uintptr_t controller, cmd_rincamera& out )
		{
			static const auto array_address = [ ]( ) -> std::uintptr_t
			{
				const auto insn = app::context().process.scan_signature(
					app::context().modules.client, "4C 8B 35 ?? ?? ?? ?? 4C 63 F8" );
				const auto resolved = insn ? app::context().process.decode_rip<std::uintptr_t>( insn ) : 0;
#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
				app::context().diagnostics.info( "[seed] ring pattern insn={:#x} array_address={:#x}",
					insn, resolved );
#endif
				return resolved;
			}( );
			if ( !array_address )
			{
				return false;
			}

			const auto array = app::context().process.load<std::uintptr_t>( array_address );
			if ( !array || !controller )
			{
				return false;
			}

			auto entity_index = 0;
			const std::array identities{
				app::context().process.load<std::uintptr_t>( controller + 0x10 ),
				app::context().process.load<std::uintptr_t>( controller + 0x8 ) };
			for ( const auto identity : identities )
			{
				if ( !identity ) continue;
				const auto handle = app::context().process.load<std::uint32_t>(
					identity + 0x10, 0xffffffffu );
				const auto candidate = static_cast<int>( handle & 0x7fffu );
				if ( candidate > 0 && candidate <= 64
					&& game::entity_index().lookup_index( candidate ) == controller )
				{
					entity_index = candidate;
					break;
				}
			}
			if ( !entity_index )
			{
				const auto entities = game::entity_index().all( );
				const auto local = std::ranges::find( *entities, controller,
					&game::entity_directory::cached::ptr );
				if ( local != entities->end( ) && local->index > 0 && local->index <= 64 )
				{
					entity_index = local->index;
				}
			}
			if ( !entity_index )
			{
				return false;
			}

			// Current client FUN_180915370 computes the owning controller's entity
			// index, subtracts one, and indexes PTR_DAT_18209da70 with that value.
			const auto index = entity_index - 1;
			const auto base = app::context().process.load<std::uintptr_t>(
				array + static_cast<std::uintptr_t>( index ) * sizeof( std::uintptr_t ) );
			int sequence{};
			if ( !validate_cmd_ring( base, sequence ) ) return false;
			out = { base, sequence };
			return true;
		}

		[[nodiscard]] bool read_cmd_sequence( std::uintptr_t controller, int& out )
		{
			cmd_rincamera ring{};
			if ( !read_cmd_ring( controller, ring ) )
			{
				return false;
			}
			out = ring.sequence;
			return true;
		}

		[[nodiscard]] bool read_cmd_angles( std::uintptr_t controller,
			foundation::vec3& out, int* out_sequence = nullptr )
		{
			// One resolve for both the object and the slot. Reading them separately meant
			// the two could disagree about which command is live.
			cmd_rincamera ring{};
			if ( !read_cmd_ring( controller, ring ) )
			{
				return false;
			}
			const auto sequence = ring.sequence;
			const auto command = ring.base +
				static_cast<std::uintptr_t>( 152 ) * ( sequence % 150 );

			const auto message = app::context().process.load<std::uintptr_t>( command + 0x40 );
			const auto angles = message ? app::context().process.load<std::uintptr_t>( message + 0x40 ) : 0;
			if ( !angles )
			{
				return false;
			}

			const auto pitch = app::context().process.load<float>( angles + 0x18 );
			const auto yaw = app::context().process.load<float>( angles + 0x1c );
			if ( !std::isfinite( pitch ) || !std::isfinite( yaw ) ||
				std::abs( pitch ) > 90.0f || std::abs( yaw ) > 360.0f )
			{
				return false;
			}

			out = { pitch, yaw, 0.0f };

			if ( out_sequence )
			{
				*out_sequence = sequence;
			}
			return true;
		}

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG

		struct seed_stats
		{
			int scans{};            // ticks that reached the candidate evaluation
			int no_enemy{};         // acquire phase found nobody in view
			int window_skip{};      // outside the 60% scan window
			int pending_skip{};     // previous press still being consumed
			int held_skip{};        // button still held from the previous press
			int not_ready{};        // ctx.weapon_ready was false
			int snapshot_skip{};    // tick advanced while the snapshot was assembled
			int restricted_skip{};  // restricted mode: ordinary trace missed
			int hit_current{};      // hits_at_tick( current_tick )
			int hit_target{};       // hits_at_tick( target_tick )
			int hit_both{};         // both - the current fire condition
			int guard_tick{};       // final re-validation: tick or window moved
			int guard_angle{};      // final re-validation: quantized angle moved
			int guard_bucket{};     // refused near a quantization boundary
			int presses{};          // injected clicks
			int shots{};            // observed m_iShotsFired increments after a press
			int cmd_angles_ok{};    // usercmd ring read succeeded
			int cmd_angles_fail{};  // fell back to eye + punch
			int seq_ok{};           // command sequence drove the tick boundary
			int seq_fail{};         // fell back to the tickcount boundary
			int memo_hit{};         // command already decided, scan skipped
			int stale_aim{};        // aim moved within its bucket between scan and press
			int reaction_wait{};    // target not in view long enough yet
		};
#endif

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG

		struct seed_impact_entry
		{
			foundation::vec3 position{};
			float timestamp{};
			float expiry{};
		};

		struct seed_remote_vector
		{
			std::int32_t size{};
			std::int32_t padding{};
			std::uintptr_t data{};
			std::int32_t capacity{};
			std::uint32_t flags{};
		};

		[[nodiscard]] bool read_impact_queue( std::uintptr_t pawn, int& size, std::uintptr_t& data )
		{
			if ( !pawn )
			{
				return false;
			}
			const auto services = app::context().process.load<std::uintptr_t>(
				pawn + SCHEMA( "C_CSPlayerPawn", "m_pBulletServices"_id ) );
			if ( services < 0x10000 || services > 0x00007fffffffffffULL )
			{
				return false;
			}
			const auto entries = app::context().process.load<seed_remote_vector>( services + 0x50 );
			if ( entries.size < 0 || entries.size > 1000000 ||
				entries.capacity < entries.size || entries.capacity > 4000000 ||
				( entries.size && entries.data < 0x10000 ) )
			{
				return false;
			}
			size = entries.size;
			data = entries.data;
			return true;
		}

#endif

		inline constexpr float g_scan_window_fraction = 0.60f;

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
		// Everything needed to re-derive either candidate's bullet after the fact.
		struct impact_probe
		{
			bool armed{};
			std::chrono::steady_clock::time_point at{};
			int queue_baseline{};
			int tick{};
			// Same tick recovered from the command sequence instead of tickcount.
			int sequence_tick{};
			foundation::vec3 eye{};
			foundation::vec3 fwd{};
			foundation::vec3 right{};
			foundation::vec3 up{};
			foundation::vec3 angles{};
			float inaccuracy{};
			float spread{};
			float recoil{};
			int item_def{};
			int fire_mode{};
			int num_bullets{};
			float speed{};

			foundation::vec3 raw_eye{};
			foundation::vec3 velocity{};
			bool predictive{};
			// The target the seeded bullet was accepted against, so the poll can compare
			// the velocity we led it by against the displacement it actually made.
			std::uintptr_t target_pawn{};
			foundation::vec3 target_origin{};
			foundation::vec3 target_velocity{};
			std::uintptr_t target_scene_node{};
		};

		struct probe_shape
		{
			double cosine_sum{};
			double ratio_sum{};
			int samples{};

			void add( double cosine, double ratio )
			{
				cosine_sum += cosine;
				ratio_sum += ratio;
				++samples;
			}

			[[nodiscard]] double mean_cosine( ) const
			{
				return samples ? cosine_sum / samples : 0.0;
			}
			[[nodiscard]] double mean_ratio( ) const
			{
				return samples ? ratio_sum / samples : 0.0;
			}
		};

		inline probe_shape g_shape_still{};
		inline probe_shape g_shape_moving{};

		inline impact_probe g_probe{};

		inline int g_verdict_lost{};

		[[nodiscard]] foundation::vec2 probe_spread( const impact_probe& p, int tick )
		{
			const auto seed = ballistics().derive_command_seed( p.angles, tick ) + 1u;
			return ballistics().sample_spread_offset( static_cast<int>( seed ),
				p.inaccuracy, p.spread, p.recoil, p.item_def, p.fire_mode, p.num_bullets, 0 );
		}

		struct probe_fit
		{
			double numerator{};
			double denominator{};
			int samples{};

			void add( const foundation::vec2& observed, const foundation::vec2& predicted )
			{
				numerator += static_cast<double>( observed.x ) * predicted.x +
					static_cast<double>( observed.y ) * predicted.y;
				denominator += static_cast<double>( predicted.x ) * predicted.x +
					static_cast<double>( predicted.y ) * predicted.y;
				++samples;
			}

			[[nodiscard]] double slope( ) const
			{
				return denominator > 1e-12 ? numerator / denominator : 0.0;
			}
		};

		inline probe_fit g_fit_a{};
		inline probe_fit g_fit_b{};
		inline probe_fit g_fit_control{};
		// Candidate S: the tick recovered from the command sequence.
		inline probe_fit g_fit_sequence{};

		void impact_probe_poll( std::uintptr_t pawn, std::chrono::steady_clock::time_point now )
		{
			if ( !g_probe.armed )
			{
				return;
			}
			if ( now - g_probe.at > std::chrono::milliseconds( 700 ) )
			{
				g_probe.armed = false;
				++g_verdict_lost;
				return;
			}

			int size{};
			std::uintptr_t data{};
			if ( !read_impact_queue( pawn, size, data ) || size <= g_probe.queue_baseline )
			{
				return;
			}

			seed_impact_entry entry{};
			const auto address = data +
				static_cast<std::uintptr_t>( g_probe.queue_baseline ) * sizeof( entry );
			if ( !app::context().process.copy( address, &entry, sizeof( entry ) ) )
			{
				g_probe.armed = false;
				++g_verdict_lost;
				return;
			}
			g_probe.armed = false;

			const auto to_impact = entry.position - g_probe.eye;
			const auto distance = to_impact.length( );
			if ( !std::isfinite( distance ) || distance < 1.0f )
			{
				++g_verdict_lost;
				return;
			}
			const auto actual = to_impact * ( 1.0f / distance );

			// Short range gives the origin error too much angular leverage to be worth
			// including; it only adds variance.
			if ( distance < 300.0f )
			{
				++g_verdict_lost;
				return;
			}

			// Invert the model the ray is built with: dir ~ fwd + right*x + up*y.
			const auto along = actual.dot( g_probe.fwd );
			if ( along < 0.1f )
			{
				++g_verdict_lost;
				return;
			}
			const foundation::vec2 observed{
				actual.dot( g_probe.right ) / along,
				actual.dot( g_probe.up ) / along };

			const auto pred_a = probe_spread( g_probe, g_probe.tick );
			const auto pred_b = probe_spread( g_probe, g_probe.tick + 1 );
			// Control: a tick neither candidate can be. Its slope is what pure noise
			// looks like for this data, so it calibrates the other two.
			const auto pred_control = probe_spread( g_probe, g_probe.tick + 37 );
			const auto pred_sequence = probe_spread( g_probe, g_probe.sequence_tick );

			// A bullet that penetrated or hit a player can land far off its own ray;
			// such a sample says nothing about the seed and would dominate the fit.
			const auto observed_magnitude = std::sqrt( observed.x * observed.x + observed.y * observed.y );
			const auto predicted_magnitude = std::sqrt( pred_a.x * pred_a.x + pred_a.y * pred_a.y );
			if ( observed_magnitude > std::max( 0.02f, predicted_magnitude * 8.0f ) )
			{
				++g_verdict_lost;
				return;
			}

			g_fit_a.add( observed, pred_a );
			g_fit_b.add( observed, pred_b );
			g_fit_control.add( observed, pred_control );
			g_fit_sequence.add( observed, pred_sequence );

			// Direction versus radius. A cone that points correctly but is drawn the
			// wrong size shows up as cosine near 1 with ratio away from 1.
			auto cosine = 0.0;
			auto ratio = 0.0;
			const auto pred_magnitude = std::sqrt( pred_a.x * pred_a.x + pred_a.y * pred_a.y );
			if ( observed_magnitude > 1e-6f && pred_magnitude > 1e-6f )
			{
				cosine = ( static_cast<double>( observed.x ) * pred_a.x +
					static_cast<double>( observed.y ) * pred_a.y ) /
					( static_cast<double>( observed_magnitude ) * pred_magnitude );
				ratio = static_cast<double>( observed_magnitude ) / pred_magnitude;
				// 5 u/s is the same threshold the trigger itself treats as standing.
				( g_probe.speed > 5.0f ? g_shape_moving : g_shape_still ).add( cosine, ratio );
			}

			if ( g_probe.target_scene_node )
			{
				const auto now_origin = app::context().process.load<foundation::vec3>(
					g_probe.target_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
				const auto elapsed = std::chrono::duration<float>( now - g_probe.at ).count( );
				if ( std::isfinite( now_origin.x ) && elapsed > 1e-4f )
				{
					const auto moved = now_origin - g_probe.target_origin;
					const auto measured = moved * ( 1.0f / elapsed );
					const auto measured_speed = measured.length_2d( );
					const auto read_speed = g_probe.target_velocity.length_2d( );
					app::context().diagnostics.info(
						"[seed] target read={:.0f} measured={:.0f} ratio={:.2f} "
						"moved={:.1f}u over {:.1f}ms",
						read_speed, measured_speed,
						read_speed > 1.0f ? measured_speed / read_speed : 0.0f,
						moved.length_2d( ), elapsed * 1000.0f );
				}
			}

			auto best_lead = -1;
			auto best_lead_cos = -2.0;
			for ( int lead = 0; lead <= 3; ++lead )
			{
				const auto origin = g_probe.raw_eye +
					g_probe.velocity * ( game::rules::simulation_step * static_cast<float>( lead ) );
				const auto delta = entry.position - origin;
				const auto delta_length = delta.length( );
				if ( !std::isfinite( delta_length ) || delta_length < 1.0f )
				{
					continue;
				}
				const auto direction = delta * ( 1.0f / delta_length );
				const auto lead_along = direction.dot( g_probe.fwd );
				if ( lead_along < 0.1f )
				{
					continue;
				}
				const foundation::vec2 lead_observed{
					direction.dot( g_probe.right ) / lead_along,
					direction.dot( g_probe.up ) / lead_along };
				const auto lead_magnitude = std::sqrt(
					lead_observed.x * lead_observed.x + lead_observed.y * lead_observed.y );
				if ( lead_magnitude < 1e-6f || pred_magnitude < 1e-6f )
				{
					continue;
				}
				const auto lead_cos = ( static_cast<double>( lead_observed.x ) * pred_a.x +
					static_cast<double>( lead_observed.y ) * pred_a.y ) /
					( static_cast<double>( lead_magnitude ) * pred_magnitude );
				if ( lead_cos > best_lead_cos )
				{
					best_lead_cos = lead_cos;
					best_lead = lead;
				}
			}

			app::context().diagnostics.info(
				"[seed] lead best={} cos={:.3f} (applied={}) speed={:.0f}",
				best_lead, best_lead_cos, g_probe.predictive ? 1 : 0, g_probe.speed );

			app::context().diagnostics.info(
				"[seed] fit n={} dist={:.0f}u speed={:.0f} obs=({:+.5f},{:+.5f}) "
				"A=({:+.5f},{:+.5f}) | slope A={:.3f} S={:.3f} B={:.3f} ctl={:.3f} "
				"| cos={:.3f} ratio={:.3f} | still n={} cos={:.3f} ratio={:.3f} "
				"| moving n={} cos={:.3f} ratio={:.3f}",
				g_fit_a.samples, distance, g_probe.speed, observed.x, observed.y,
				pred_a.x, pred_a.y,
				g_fit_a.slope( ), g_fit_sequence.slope( ), g_fit_b.slope( ), g_fit_control.slope( ),
				cosine, ratio,
				g_shape_still.samples, g_shape_still.mean_cosine( ), g_shape_still.mean_ratio( ),
				g_shape_moving.samples, g_shape_moving.mean_cosine( ), g_shape_moving.mean_ratio( ) );
		}

		inline seed_stats g_seed_stats{};
		inline std::chrono::steady_clock::time_point g_seed_stats_at{};

		inline std::ofstream g_seed_file{};

		[[nodiscard]] long long unix_ms( )
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now( ).time_since_epoch( ) ).count( );
		}

		void seed_file_write( const std::string& line )
		{
			if ( !g_seed_file.is_open( ) )
			{
				g_seed_file.open( "B:\\Projects\\vesta\\vesta_seed_trace.log",
					std::ios::out | std::ios::app );
				if ( g_seed_file.is_open( ) )
				{
					g_seed_file << "# vesta seed trace; unix_ms is std::system_clock epoch\n";
				}
			}
			if ( g_seed_file.is_open( ) )
			{
				g_seed_file << line << '\n';
				g_seed_file.flush( );
			}
		}

		// Set when a press is injected, consumed when the shot shows up.
		inline int g_press_sequence{ -1 };
		inline int g_press_tick{ -1 };
		inline std::chrono::steady_clock::time_point g_press_at{};

		void seed_stats_flush( std::chrono::steady_clock::time_point now )
		{
			if ( g_seed_stats_at == std::chrono::steady_clock::time_point{} )
			{
				g_seed_stats_at = now;
				return;
			}
			if ( now - g_seed_stats_at < std::chrono::seconds( 1 ) )
			{
				return;
			}
			g_seed_stats_at = now;

			const auto& s = g_seed_stats;
			// Only speak up when the trigger was actually trying to do something.
			if ( s.scans || s.presses || s.no_enemy )
			{
				app::context().diagnostics.info(
					"[seed] scan={} | skip: enemy={} win={} pend={} held={} ready={} snap={} restr={} "
					"| hit: cur={} tgt={} both={} | guard: tick={} ang={} bucket={} "
					"| press={} shot={} | cmd: ok={} fail={} | seq: ok={} fail={} memo={} stale={} react={}",
					s.scans, s.no_enemy, s.window_skip, s.pending_skip, s.held_skip,
					s.not_ready, s.snapshot_skip, s.restricted_skip,
					s.hit_current, s.hit_target, s.hit_both,
					s.guard_tick, s.guard_angle, s.guard_bucket,
					s.presses, s.shots, s.cmd_angles_ok, s.cmd_angles_fail,
					s.seq_ok, s.seq_fail, s.memo_hit, s.stale_aim, s.reaction_wait );
			}
			g_seed_stats = {};
		}
#endif

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
#define VESTA_SEED_COUNT( field ) ( ++g_seed_stats.field )
#else
#define VESTA_SEED_COUNT( field ) ( ( void )0 )
#endif

		[[nodiscard]] foundation::vec3 read_aim_punch( std::uintptr_t pawn )
		{
			struct sample_vector
			{
				std::int32_t size;
				std::int32_t pad;
				std::uintptr_t data;
				std::int32_t capacity;
				std::uint32_t flags;
			};
			const auto plausible = []( std::uintptr_t v ) { return v >= 0x10000 && v <= 0x00007fffffffffffULL; };
			if ( !plausible( pawn ) )
			{
				return {};
			}
			const auto services = app::context().process.load<std::uintptr_t>(
				pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_id ) );
			if ( !plausible( services ) )
			{
				return {};
			}
			const auto valid_angle = []( const foundation::vec3& value )
			{
				return std::isfinite( value.x ) && std::isfinite( value.y )
					&& std::isfinite( value.z ) && std::abs( value.x ) < 45.0f
					&& std::abs( value.y ) < 45.0f && std::abs( value.z ) < 45.0f;
			};
			foundation::vec3 punch{};

			for ( const auto track_offset : {
				std::uintptr_t{ 0x68 }, std::uintptr_t{ 0xb0 } } )
			{
				const auto samples = app::context().process.load<sample_vector>(
					services + track_offset + 0x20 );
				if ( samples.size <= 0 || samples.size > 4096
					|| samples.capacity < samples.size || samples.capacity > 8192
					|| !plausible( samples.data ) )
				{
					continue;
				}
				const auto sample = app::context().process.load<foundation::vec3>(
					samples.data + static_cast<std::uintptr_t>( samples.size - 1 ) * sizeof( foundation::vec3 ) );
				if ( std::isfinite( sample.x ) && std::abs( sample.x ) < 45.0f &&
					std::abs( sample.y ) < 45.0f && std::abs( sample.z ) < 45.0f )
				{
					punch += sample;
				}
			}
			punch *= 2.0f;
			return std::isfinite( punch.x ) && std::isfinite( punch.y ) ? punch : foundation::vec3{};
		}

		// View the shot command carries before punch. Native adds punch to it for both the
		// SHA seed and the ray, so every branch must do the same.
		[[nodiscard]] std::optional<foundation::vec3> resolve_seed_base(
			std::uintptr_t pawn, const foundation::vec3& view_angles,
			bool host_session, const foundation::vec3* command_angles )
		{
			auto base = view_angles;
			if ( command_angles )
			{
				base = *command_angles;
			}
			else if ( !host_session )
			{
				const auto pawn_angles = app::context().process.load<foundation::vec3>(
					pawn + SCHEMA( "C_BasePlayerPawn", "v_angle"_id ) );
				if ( std::isfinite( pawn_angles.x ) && std::isfinite( pawn_angles.y )
					&& std::isfinite( pawn_angles.z ) && std::abs( pawn_angles.x ) <= 89.0f
					&& std::abs( pawn_angles.y ) <= 360.0f )
				{
					base = pawn_angles;
				}
			}
			if ( !std::isfinite( base.x ) || !std::isfinite( base.y ) || !std::isfinite( base.z ) )
			{
				return std::nullopt;
			}
			return base;
		}

		[[nodiscard]] float seed_trajectory_bucket( float angle )
		{
			return std::roundf( angle * 50.0f );
		}

		[[nodiscard]] int part_from_hitbox(
			const game::hitbox_catalog::entry& hitbox, int hitgroup )
		{

			if ( hitbox.index == 0 )
				return config::combat_profile::aim_part::head;
			if ( hitbox.index == 1 )
				return config::combat_profile::aim_part::body;
			const std::string_view name{ hitbox.name.data( ) };
			if ( name == "head" ) return config::combat_profile::aim_part::head;
			if ( name == "neck_0" || name == "neck" )
				return config::combat_profile::aim_part::body;
			switch ( hitgroup )
			{
			case 1:            return config::combat_profile::aim_part::body;
			case 2: case 3:    return config::combat_profile::aim_part::body; // chest + stomach/pelvis
			case 4: case 5:    return config::combat_profile::aim_part::arms;
			case 6: case 7:    return config::combat_profile::aim_part::legs;
			default:           return config::combat_profile::aim_part::body;
			}
		}

		struct point_buffer
		{
			std::array<foundation::vec3, 7> points{};
			int count{};
		};

		[[nodiscard]] point_buffer build_aim_points(
			const game::skeleton_reader::data::bone& bone,
			const game::hitbox_catalog::entry& hb, bool multipoint, int part,
			const config::combat_profile::multipoint_settings& settings )
		{
			point_buffer out{};

			const auto start = bone.position + bone.rotation.apply( hb.mins );
			const auto end   = bone.position + bone.rotation.apply( hb.maxs );
			const auto center = ( start + end ) * 0.5f;

			out.points[ out.count++ ] = center;
			if ( !multipoint )
			{
				return out;
			}

			if ( settings.caps )
			{
				out.points[ out.count++ ] = start;
				out.points[ out.count++ ] = end;
			}

			// Lateral spread perpendicular to the capsule axis, so points cover the sides
			// of the hull (a shoulder/temple edge), not just its length.
			auto axis = end - start;
			if ( settings.sides && axis.length_sqr( ) > 0.0001f )
			{
				axis.normalize( );
				auto right = axis.cross( foundation::vec3{ 0.0f, 0.0f, 1.0f } );
				if ( right.length_sqr( ) < 0.0001f )
				{
					right = axis.cross( foundation::vec3{ 1.0f, 0.0f, 0.0f } );
				}
				right.normalize( );
				const auto up = right.cross( axis ).normalized( );

				const auto scale = part == config::combat_profile::aim_part::head
					? settings.head_scale
					: ( part == config::combat_profile::aim_part::body
						? settings.body_scale : settings.limb_scale );
				const auto reach = hb.radius * std::clamp( scale, 0.05f, 0.95f );

				out.points[ out.count++ ] = center + right * reach;
				out.points[ out.count++ ] = center - right * reach;
				out.points[ out.count++ ] = center + up * reach;
				out.points[ out.count++ ] = center - up * reach;
			}

			return out;
		}

		[[nodiscard]] int part_from_hitbox( int hitbox, int hitgroup )
		{
			game::hitbox_catalog::entry legacy{};
			legacy.index = hitbox;
			return part_from_hitbox( legacy, hitgroup );
		}

		[[nodiscard]] bool segment_intersects_sphere(
			const foundation::vec3& start, const foundation::vec3& end,
			const foundation::vec3& center, float radius )
		{
			const auto segment = end - start;
			const auto length_sqr = segment.length_sqr( );
			if ( length_sqr <= 0.0001f )
			{
				return ( start - center ).length_sqr( ) <= radius * radius;
			}
			const auto t = std::clamp(
				( center - start ).dot( segment ) / length_sqr, 0.0f, 1.0f );
			return ( start + segment * t - center ).length_sqr( ) <= radius * radius;
		}

		[[nodiscard]] bool line_through_smoke(
			const foundation::vec3& start, const foundation::vec3& end )
		{
			const auto projectiles = game::world().projectiles( );
			if ( !projectiles ) return true;
			for ( const auto& projectile : *projectiles )
			{
				if ( projectile.subtype != game::projectile_kind::smoke_grenade
					|| !projectile.smoke_active )
				{
					continue;
				}
				auto center = projectile.smoke_detonation_pos;
				if ( !std::isfinite( center.x ) || !std::isfinite( center.y )
					|| !std::isfinite( center.z ) || center.length_sqr( ) < 1.0f )
				{
					center = projectile.origin;
				}
				center.z += 45.0f;
				const auto radius = projectile.smoke_volume_received
					&& projectile.smoke_voxel_size > 0 ? 145.0f : 160.0f;
				if ( segment_intersects_sphere( start, end, center, radius ) )
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] combat_block_reason local_block_reason(
			const config::combat_profile::legit_checks& checks )
		{
			const auto pawn = game::local_player().pawn( );
			if ( !pawn ) return combat_block_reason::inactive;
			if ( checks.airborne )
			{
				const auto flags = app::context().process.load<std::uint32_t>(
					pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
				if ( ( flags & 1u ) == 0u ) return combat_block_reason::airborne;
			}
			if ( checks.flashed )
			{
				const auto raw_alpha = game::local_player().flash_alpha( );

				const auto blindness = std::clamp(
					raw_alpha > 1.5f ? raw_alpha / 255.0f : raw_alpha, 0.0f, 1.0f );
				const auto threshold = std::clamp(
					checks.flash_threshold * 0.01f, 0.0f, 1.0f );
				if ( blindness >= threshold )
					return combat_block_reason::flashed;
			}
			return combat_block_reason::none;
		}

		[[nodiscard]] bool local_checks_blocked(
			const config::combat_profile::legit_checks& checks )
		{
			return local_block_reason( checks ) != combat_block_reason::none;
		}

		[[nodiscard]] bool live_target_invulnerable(
			std::uintptr_t pawn, const bool snapshot_value )
		{
			if ( !pawn ) return true;
			bool immunity{};
			if ( !app::context().process.copy(
				pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_id ),
				&immunity, sizeof( immunity ) ) )
			{

				return snapshot_value;
			}
			return immunity;
		}

		[[nodiscard]] float distance_fov(
			const config::combat_profile::aimbot& cfg, float distance_units )
		{
			const auto target_mode = cfg.fov_config.selection
				== config::combat_profile::fov_settings::target_distance;
			const auto near_distance = std::max( target_mode
				? cfg.fov_config.target_near_distance_m : cfg.fov_config.near_distance_m, 0.25f );
			const auto far_distance = std::max(
				target_mode ? cfg.fov_config.target_far_distance_m
					: cfg.fov_config.far_distance_m, near_distance + 0.25f );
			const auto distance_m = std::clamp(
				distance_units / 52.4934f, near_distance, far_distance );

			auto t = std::log( distance_m / near_distance )
				/ std::log( far_distance / near_distance );
			t = std::pow( std::clamp( t, 0.0f, 1.0f ), std::clamp( target_mode
				? cfg.fov_config.target_distance_curve : cfg.fov_config.distance_curve,
				0.25f, 4.0f ) );
			const auto near_fov = std::max( target_mode
				? cfg.fov_config.target_near_fov : cfg.fov_config.near_fov, 0.25f );
			const auto far_fov = std::clamp(
				target_mode ? cfg.fov_config.target_far_fov : cfg.fov_config.far_fov,
				0.25f, near_fov );
			return std::exp( std::lerp( std::log( near_fov ),
				std::log( far_fov ), t ) );
		}

		[[nodiscard]] float selection_fov(
			const config::combat_profile::aimbot& cfg,
			const foundation::vec3& eye, const foundation::vec3& target )
		{
			if ( cfg.fov_config.selection == config::combat_profile::fov_settings::fixed )
				return static_cast<float>( cfg.fov );
			return distance_fov( cfg, ( target - eye ).length( ) );
		}

	} // namespace

	void controller_t::reset( )
	{
		if ( this->m_revolver_pre_cock_down )
		{
			app::context().input.pointer(
				0, 0, platform::windows::pointer_action::primary_up );
			this->m_revolver_pre_cock_down = false;
		}
		if ( this->m_trigger_held )
		{
			auto released = true;
			if ( this->m_seed_held_secondary )
			{
				app::context().input.pointer( 0, 0, platform::windows::pointer_action::secondary_up );
			}
			else if ( this->m_seed_held_proxy )
			{
				app::context().input.key( VK_F24, false );
			}
			else
			{
				released = app::context().input.pointer(
					0, 0, platform::windows::pointer_action::primary_up );
			}

			if ( released ) this->m_trigger_held = false;
		}
		this->m_trigger_press_shots = -1;
		this->m_seed_press_active = false;
		this->m_seed_held_secondary = false;
		this->m_seed_held_proxy = false;
		this->m_revolver_committed = false;
		this->m_revolver_weapon = 0;

		this->m_trigger_shot_scheduled = false;
		this->m_trigger_fire_time = {};
		this->m_trigger_cooldown_until = {};
		this->m_trigger_target_valid = false;
		this->m_seed_last_shots = -1;
		this->m_seed_pending_target_tick = -1;
		this->m_seed_pending_time = {};
		this->m_seed_memo_sequence = -1;
		this->m_seed_memo_signature = 0;
		this->m_last_time = 0.0f;
		this->m_aim_pawn = 0;
		this->m_aim_error = {};
		this->m_aim_last_input_sequence = -1;
		this->m_aim_last_input_view = {};
		this->m_aim_last_input_time = {};
		this->m_aim_virtual_angles = {};
		this->m_aim_virtual_angles_valid = false;
		this->m_aim_degrees_per_pixel = {};
		this->m_aim_degrees_candidate = {};
		this->m_aim_degrees_confirmations = {};
		this->m_aim_tracking_lag = 0.0f;
		this->m_aim_previous_relative_velocity = {};
		this->m_aim_relative_acceleration = {};
		this->m_aim_velocity_valid = false;
		this->m_aim_simulation_tick = -1;
		this->m_aim_simulation_time = 0.0f;
		this->m_aim_velocity_samples = 0;
		this->m_rcs_raw = {};
		this->m_rcs_target = {};
		this->m_rcs_velocity = {};
		this->m_rcs_applied = {};
		this->m_rcs_mouse_error = {};
		this->m_rcs_pending_mouse = {};
		this->m_rcs_last_input_sequence = -1;
		this->m_rcs_last_input_time = {};
		this->m_rcs_input_step = 24;
		this->m_rcs_active = false;

		features::misc::auto_stop( ).cancel_request(
			features::misc::auto_stop_source::advanced_trigger );
	}

	void controller_t::reset_seed( )
	{
		if ( this->m_revolver_pre_cock_down )
		{
			app::context().input.pointer(
				0, 0, platform::windows::pointer_action::primary_up );
			this->m_revolver_pre_cock_down = false;
		}
		if ( this->m_trigger_held )
		{
			if ( this->m_seed_held_secondary )
			{
				app::context().input.pointer( 0, 0, platform::windows::pointer_action::secondary_up );
			}
			else if ( this->m_seed_held_proxy )
			{
				app::context().input.key( VK_F24, false );
			}
			else
			{
				app::context().input.pointer( 0, 0, platform::windows::pointer_action::primary_up );
			}
		}
		this->m_trigger_held = false;
		this->m_trigger_press_shots = -1;
		this->m_seed_press_active = false;
		this->m_seed_held_secondary = false;
		this->m_seed_held_proxy = false;
		this->m_revolver_committed = false;
		this->m_revolver_weapon = 0;
		this->m_trigger_release_time = {};
		this->m_seed_pending_target_tick = -1;
		this->m_seed_pending_time = {};
		this->m_seed_memo_sequence = -1;
		this->m_seed_memo_signature = 0;
		this->m_seed_phase_tick = -1;
		this->m_seed_phase_tick_at = {};
		this->m_seed_targets.clear( );
		this->m_seed_player_buffer.clear( );
		this->m_seed_last_controller = 0;
		this->m_seed_last_pawn = 0;
		this->m_seed_last_tick = -1;
		this->m_seed_tick_observed_at = {};
		features::misc::auto_stop( ).cancel_request(
			features::misc::auto_stop_source::seed_trigger );
	}

	bool controller_t::seed_hot_path_requested( ) const noexcept
	{
		return this->m_trigger_held
			|| ( config::combat_settings.seed_trigger_configured( )
				&& config::combat_profile::activation_active(
					config::combat_settings.global.triggerbot_activation_mode,
					config::combat_settings.global.triggerbot_key ) );
	}

	int controller_t::observe_seed_clock(
		std::chrono::steady_clock::time_point observed_at ) noexcept
	{
		const auto controller = app::context().process.load<std::uintptr_t>(
			app::context().addresses.local_player_controller );
		int sequence{};
		if ( read_cmd_sequence( controller, sequence ) )
		{
			this->sync_seed_phase( sequence, observed_at );
			return sequence;
		}
		const auto binding = game::resolve_local_pawn( controller );
		const auto fallback = read_seed_tick(
			controller, binding.pawn, is_host_seed_session( controller ) );
		this->sync_seed_phase( fallback, observed_at );
		return fallback;
	}

	std::optional<controller_t::seed_shot_plan> controller_t::build_seed_plan(
		std::uintptr_t pawn, const foundation::vec3& view_angles,
		bool host_session, const seed_command_source& source,
		std::chrono::steady_clock::time_point now, bool memoize )
	{
		namespace window = simulation::seed_window;
		const auto seed_tick = source.tick;
		if ( seed_tick <= 0 ) return std::nullopt;
		const auto base = resolve_seed_base( pawn, view_angles, host_session,
			source.validated ? &source.raw_angles : nullptr );
		if ( !base ) return std::nullopt;
		const auto recoil = simulation::read_recoil_state( pawn );
		if ( !recoil ) return std::nullopt;

		// The ring holds the latest command; when the aimbot has already published the
		// angles of the next one, the click may land there instead.
		std::array<foundation::vec3, 2> bases{ *base };
		std::size_t base_count = 1;
		if ( source.validated )
		{
			if ( const auto predicted = predicted_aim_command( source.sequence, now );
				predicted && !simulation::shot_model::same_direction( *predicted, *base, 1e-4f ) )
			{
				bases[ base_count++ ] = *predicted;
			}
		}

		seed_shot_plan plan{};
		window::punch_sampler sampler{ *recoil };
		const auto ticks = window::candidate_ticks( host_session, seed_tick );
		// Most likely tick first so the reaction tracker keys on the probable shot.
		std::array<int, 2> order{};
		int order_count{};
		order[ order_count++ ] = seed_tick;
		for ( int index = 0; index < ticks.count; ++index )
		{
			if ( ticks.ticks[ index ] != seed_tick ) order[ order_count++ ] = ticks.ticks[ index ];
		}
		for ( int index = 0; index < order_count; ++index )
		{
			const auto tick = order[ index ];
			for ( std::size_t b = 0; b < base_count; ++b )
			{
				window::window resolved{};
				if ( window::resolve( sampler, bases[ b ], tick, resolved ) != window::status::ok )
				{
					return std::nullopt;
				}
				for ( const auto slot : { 1, 0, 2 } )
				{
					const auto& angles = resolved.angles[ slot ];
					const auto duplicate = std::ranges::any_of(
						std::span{ plan.samples.data( ), plan.sample_count },
						[ & ]( const seed_shot_sample& sample )
						{
							return sample.tick == tick
								&& simulation::shot_model::same_direction(
									sample.direction_angles, angles, 1e-4f );
						} );
					if ( !duplicate && plan.sample_count < plan.samples.size( ) )
					{
						plan.samples[ plan.sample_count++ ] = { tick, angles, angles };
					}
				}
			}
			plan.latest_tick = std::max( plan.latest_tick, tick );
		}
		if ( !plan.sample_count ) return std::nullopt;

		plan.source_key = source.validated
			? static_cast<std::uint64_t>( source.sequence )
			: static_cast<std::uint64_t>( seed_tick );
		plan.source_tick = seed_tick;
		plan.source_angles = *base;
		plan.prepared_punch = plan.samples[ 0 ].hash_angles - *base;

		const auto phase_source = source.validated ? source.sequence : seed_tick;
		std::uint64_t signature{ 1469598103934665603ull };
		const auto mix = [ &signature ]( float value )
		{
			signature = ( signature ^ std::bit_cast<std::uint32_t>( value ) ) * 1099511628211ull;
		};
		for ( std::size_t index = 0; index < plan.sample_count; ++index )
		{
			const auto& sample = plan.samples[ index ];
			mix( static_cast<float>( sample.tick ) );
			mix( simulation::shot_model::quantize( sample.hash_angles.x ) );
			mix( simulation::shot_model::quantize( sample.hash_angles.y ) );
			mix( seed_trajectory_bucket( sample.direction_angles.x ) );
			mix( seed_trajectory_bucket( foundation::wrap_yaw( sample.direction_angles.y ) ) );
		}
		if ( memoize && this->m_seed_memo_sequence == phase_source
			&& this->m_seed_memo_signature == signature )
		{
			return std::nullopt;
		}
		if ( memoize )
		{
			this->m_seed_memo_sequence = phase_source;
			this->m_seed_memo_signature = signature;
		}
		return plan;
	}

	void controller_t::seed_tick( ballistics_t& seed_shared )
	{

		struct stop_request_guard
		{
			bool preserve{};
			~stop_request_guard( )
			{
				if ( !preserve ) features::misc::auto_stop( ).cancel_request(
					features::misc::auto_stop_source::seed_trigger );
			}
		} stop_guard{ .preserve = this->m_trigger_held };

		const auto now = std::chrono::steady_clock::now( );
		if ( this->m_trigger_held && now >= this->m_trigger_release_time )
		{
			if ( this->m_seed_held_secondary )
			{
				app::context().input.pointer( 0, 0, platform::windows::pointer_action::secondary_up );
			}
			else if ( this->m_seed_held_proxy )
			{
				app::context().input.key( VK_F24, false );
			}
			else
			{
				app::context().input.pointer( 0, 0, platform::windows::pointer_action::primary_up );
			}
			this->m_trigger_held = false;
			this->m_seed_press_active = false;
			this->m_seed_held_secondary = false;
			this->m_seed_held_proxy = false;
		}

		if ( !game::collision().valid( ) )
		{
			this->reset_seed( );
			return;
		}

		if ( !config::combat_settings.seed_trigger_configured( ) )
		{
			if ( this->m_trigger_held || this->m_seed_last_controller
				|| this->m_seed_last_pawn || !this->m_seed_targets.empty( ) )
			{
				this->reset_seed( );
			}
			return;
		}
		const auto configured_key =
			config::combat_settings.global.triggerbot_key;
		const auto configured_key_down = config::combat_profile::activation_active(
			config::combat_settings.global.triggerbot_activation_mode, configured_key );
		if ( !configured_key_down && !this->m_trigger_held )
		{

			this->m_seed_targets.clear( );
			this->m_seed_player_buffer.clear( );
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::seed_trigger );
			return;
		}

		const auto controller = app::context().process.load<std::uintptr_t>(
			app::context().addresses.local_player_controller );
		const auto host_session = is_host_seed_session( controller );
		const auto game_type = game::variables().get<std::int32_t>(
			CONVAR( "game_type"_id ) );
		const auto game_mode = game::variables().get<std::int32_t>(
			CONVAR( "game_mode"_id ) );
		const auto free_for_all = ( game_type == 1 && game_mode == 2 )
			|| ( game_type == 2 && game_mode == 0 );
		const auto local_binding = game::resolve_local_pawn( controller );
		const auto pawn = local_binding.pawn;
		if ( !controller || !pawn )
		{
			if ( this->m_seed_last_controller || this->m_seed_last_pawn
				|| this->m_trigger_held )
			{
				this->reset_seed( );
			}
			return;
		}

		const auto local_health = app::context().process.load<std::int32_t>(
			pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_id ) );
		const auto local_team = app::context().process.load<std::int32_t>(
			pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_id ) );
		auto velocity = app::context().process.load<foundation::vec3>(
			pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		if ( local_health <= 0 || local_health > 100
			|| ( local_team != 2 && local_team != 3 )
			|| !std::isfinite( velocity.x ) || !std::isfinite( velocity.y )
			|| !std::isfinite( velocity.z ) )
		{
			this->reset_seed( );
			return;
		}

		foundation::vec3 eye_pos{};
		foundation::vec3 view_angles{};
		if ( !game::camera().sample( eye_pos, view_angles ) )
		{
			return;
		}
		seed_command_source command_source{};
		command_source.validated = read_cmd_angles(
			controller, command_source.raw_angles, &command_source.sequence );
		if ( !command_source.validated )
		{

			command_source.raw_angles = view_angles;
			command_source.sequence = -1;
		}
		command_source.tick = read_seed_tick( controller, pawn, host_session );
		const auto seed_tick = command_source.tick;
		if ( seed_tick <= 0 )
		{
			return;
		}

		const auto shots_fired = app::context().process.load<std::int32_t>(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) );
		const auto shot_consumed = this->m_seed_last_shots >= 0
			&& shots_fired > this->m_seed_last_shots;
		if ( shot_consumed )
		{
			features::visuals::event_log( ).mark_latest_trigger_consumed( );
#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
			if ( g_press_sequence >= 0 )
			{
				foundation::vec3 shot_angles{};
				int shot_sequence{ -1 };
				( void )read_cmd_angles( controller, shot_angles, &shot_sequence );
				const auto latency_ms = std::chrono::duration<float, std::milli>(
					now - g_press_at ).count( );
				VESTA_SEED_COUNT( shots );
				seed_file_write( std::format(
					"SHOT unix_ms={} press_seq={} shot_seq={} seq_delta={} "
					"press_tick={} shot_tick={} tick_delta={} "
					"shot_angle=({:.4f},{:.4f}) latency={:.3f}",
					unix_ms( ), g_press_sequence, shot_sequence,
					shot_sequence >= 0 ? shot_sequence - g_press_sequence : -1,
					g_press_tick, seed_tick, seed_tick - g_press_tick,
					shot_angles.x, shot_angles.y, latency_ms ) );
				g_press_sequence = -1;
			}
#endif
			if ( this->m_trigger_held )
			{
				if ( this->m_seed_held_secondary )
					app::context().input.pointer( 0, 0,
						platform::windows::pointer_action::secondary_up );
				else if ( this->m_seed_held_proxy )
					app::context().input.key( VK_F24, false );
				else
					app::context().input.pointer( 0, 0,
						platform::windows::pointer_action::primary_up );
			}
			this->m_trigger_held = false;
			this->m_seed_press_active = false;
			this->m_seed_held_secondary = false;
			this->m_seed_held_proxy = false;
			this->m_seed_pending_target_tick = -1;
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::seed_trigger );
		}
		this->m_seed_last_shots = shots_fired;

		const auto identity_changed =
			( this->m_seed_last_controller
				&& this->m_seed_last_controller != controller )
			|| ( this->m_seed_last_pawn && this->m_seed_last_pawn != pawn );
		const auto tick_rewound = this->m_seed_last_tick >= 0
			&& seed_tick + 32 < this->m_seed_last_tick;
		if ( identity_changed || tick_rewound )
		{
			this->reset_seed( );
		}
		this->m_seed_last_controller = controller;
		this->m_seed_last_pawn = pawn;
		if ( this->m_seed_last_tick != seed_tick )
		{

			this->m_seed_tick_observed_at = now;
		}
		this->m_seed_last_tick = seed_tick;

		// Preserve seed_trigger_advanced's ordering: coherent local state first,
		// then a private weapon snapshot, then private target/bone reads.
		ballistics_t::context ctx{};
		if ( !seed_shared.seed_weapon( pawn, controller, ctx ) )
		{
			this->m_seed_targets.clear( );
			return;
		}
		const auto resolved = config::combat_settings.get( ctx.weapon_type );
		const auto cfg = resolved.triggerbot;
		const auto is_enemy = [ local_team, free_for_all ]( int team )
		{
			return free_for_all || team != local_team;
		};

		if ( !cfg.enabled
			|| cfg.seed_type == config::combat_profile::seed_mode::none )
		{
			this->reset_seed( );
			return;
		}
		if ( local_checks_blocked( cfg.checks ) )
		{
			this->m_seed_targets.clear( );
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::seed_trigger );
			return;
		}

		constexpr std::uint16_t revolver_id{ 64 };
		if ( ( !ctx.weapon_ready && ctx.item_def_idx != revolver_id )
			|| ctx.is_reloading )
		{
			this->m_seed_targets.clear( );
			return;
		}

		const auto hotkey_down = config::combat_profile::activation_active(
			cfg.activation_mode, cfg.key );
		if ( !hotkey_down )
		{
			this->m_seed_targets.clear( );
			this->m_seed_player_buffer.clear( );
			return;
		}

		const auto plan = this->build_seed_plan(
			pawn, view_angles, host_session, command_source, now,
			!this->m_trigger_held );
		if ( this->m_trigger_held || !plan )
		{
			return;
		}
		if ( this->m_seed_pending_target_tick >= 0
			&& seed_tick <= this->m_seed_pending_target_tick )
		{
			return;
		}

		game::world().seed_players_into( this->m_seed_player_buffer,
			pawn, controller, local_team, free_for_all );
		const auto& players = this->m_seed_player_buffer;
		if ( players.empty( ) )
		{
			return;
		}

		for ( const auto& player : players )
		{
			if ( const auto found = this->m_seed_targets.find( player.pawn );
				found != this->m_seed_targets.end( ) )
			{
				found->second.last_seen_at = now;
			}
		}
		constexpr auto entity_grace = std::chrono::milliseconds( 250 );
		std::erase_if( this->m_seed_targets, [ & ]( const auto& entry )
		{
			return now - entry.second.last_seen_at > entity_grace;
		} );

		const auto hitbox_enabled = [ & ]( int hitbox )
		{
			const auto hitgroup =
				game::hitbox_data().hitgroup_from_hitbox( hitbox );
			return ( cfg.hitbox_parts
				& part_from_hitbox( hitbox, hitgroup ) ) != 0;
		};

		const auto evaluate_sample = [ & ](
			const std::vector<game::player_snapshot>& candidates,
			const ballistics_t::context& weapon_ctx,
			const foundation::vec3& local_eye,
			const foundation::vec3& local_velocity,
			int local_tick, std::uintptr_t target_pawn,
			int required_hitbox, const seed_shot_sample& sample )
		{
			const auto target = std::ranges::find(
				candidates, target_pawn,
				&game::player_snapshot::pawn );
			if ( target == candidates.end( ) || target->invulnerable
				|| target->health <= 0 || !target->bones.is_valid( ) )
			{
				return false;
			}

			const auto horizon = std::max( 0, sample.tick - local_tick );
			auto origin = local_eye;
			if ( host_session || cfg.predictive )
			{
				origin += local_velocity
					* ( static_cast<float>( horizon ) / 64.0f );
			}
			auto target_bones = target->bones;
			if ( host_session && horizon > 0 )
			{
				const auto target_delta = target->velocity
					* ( static_cast<float>( horizon ) / 64.0f );
				for ( auto& bone : target_bones.bones )
				{
					bone.position += target_delta;
				}
			}

			foundation::vec3 forward{}, right{}, up{};
			sample.direction_angles.to_directions( &forward, &right, &up );
			const auto spread_seed =
				seed_shared.derive_command_seed(
					sample.hash_angles, sample.tick ) + 1u;
			const auto bullet_count =
				std::clamp( weapon_ctx.num_bullets, 1, 32 );
			for ( int bullet = 0; bullet < bullet_count; ++bullet )
			{
				const auto spread = seed_shared.sample_predicted_spread(
					static_cast<int>( spread_seed ),
					weapon_ctx.inaccuracy, weapon_ctx.spread,
					weapon_ctx.recoil_index, weapon_ctx.item_def_idx,
					weapon_ctx.fire_mode, bullet, bullet_count, weapon_ctx.pattern_seed );
				const auto direction =
					( forward + right * spread.x + up * spread.y ).normalized( );

				ballistics_t::penetration::result damage{};
				const auto seed_hitboxes = cfg.hitbox_parts
					| ( cfg.lethal_only ? config::combat_profile::aim_part::head : 0 );
				if ( seed_shared.pen( ).run_seed(
					origin, direction, *target, target_bones,
					seed_hitboxes,
					cfg.checks.walls == config::combat_profile::wall_policy::penetration,
					1.0f,
					required_hitbox, damage ) )
				{

					const auto hitgroup = game::hitbox_data( ).hitgroup_from_hitbox(
						damage.hitbox );
					constexpr auto head_hitgroup = 1;
					if ( damage.penetrated && damage.damage + 0.001f < cfg.min_damage )
					{
						continue;
					}
					if ( !cfg.lethal_only || hitgroup == head_hitgroup
						|| damage.damage + 0.001f >= static_cast<float>( target->health ) )
					{
						return true;
					}
				}
			}
			return false;
		};

		int restricted_hitbox = -1;
		std::uintptr_t restricted_pawn{};
		if ( cfg.seed_type == config::combat_profile::seed_mode::restricted )
		{
			foundation::vec3 ordinary_direction{};
			plan->samples[ 0 ].direction_angles.to_directions(
				&ordinary_direction, nullptr, nullptr );
			auto nearest = std::numeric_limits<float>::max( );
			for ( const auto& player : players )
			{
				if ( !is_enemy( player.team )
					|| player.invulnerable || player.health <= 0 )
				{
					continue;
				}
				for ( const auto& hitbox : player.hitboxes )
				{
					if ( hitbox.index < 0 || hitbox.bone < 0
						|| !hitbox_enabled( hitbox.index ) )
					{
						continue;
					}
					const auto& bone = player.bones.bones[ hitbox.bone ];
					const auto start = bone.position
						+ bone.rotation.apply( hitbox.mins );
					const auto end = bone.position
						+ bone.rotation.apply( hitbox.maxs );
					if ( !seed_shared.ray_hits_capsule(
						eye_pos, ordinary_direction, start, end,
						std::max( 0.5f, hitbox.radius ) ) )
					{
						continue;
					}
					const auto distance =
						( ( start + end ) * 0.5f - eye_pos )
							.dot( ordinary_direction );
					if ( distance > 0.0f && distance < nearest )
					{
						nearest = distance;
						restricted_pawn = player.pawn;
						restricted_hitbox = hitbox.index;
					}
				}
			}
			if ( !restricted_pawn )
			{
				return;
			}
		}

		const auto evaluate_plan = [ & ](
			const std::vector<game::player_snapshot>& candidates,
			const ballistics_t::context& weapon_ctx,
			const foundation::vec3& local_eye,
			const foundation::vec3& local_velocity, int local_tick,
			std::uintptr_t target_pawn, int required_hitbox,
			const seed_shot_plan& shot_plan )
		{
			// first: the most likely shot hits (keeps the reaction timer running);
			// second: every shot this click can produce hits.
			std::pair<bool, bool> result{};
			for ( std::size_t index = 0; index < shot_plan.sample_count; ++index )
			{
				if ( !evaluate_sample( candidates, weapon_ctx, local_eye, local_velocity,
					local_tick, target_pawn, required_hitbox, shot_plan.samples[ index ] ) )
				{
					return result;
				}
				result.first = true;
			}
			result.second = result.first;
			return result;
		};

		std::uintptr_t firing_target{};
		int firing_hitbox = -1;
		for ( const auto& player : players )
		{
			if ( !is_enemy( player.team )
				|| player.invulnerable || player.health <= 0
				|| ( restricted_pawn && player.pawn != restricted_pawn ) )
			{
				continue;
			}
			const auto matches = evaluate_plan(
				players, ctx, eye_pos, velocity, seed_tick, player.pawn,
				restricted_pawn ? restricted_hitbox : -1, *plan );
			if ( !matches.first )
			{
				continue;
			}

			auto [state, inserted] = this->m_seed_targets.try_emplace(
				player.pawn, seed_target_state{ now, now } );
			state->second.last_seen_at = now;
			const auto reaction_ready = cfg.reaction_time <= 0
				|| now - state->second.acquired_at
					>= std::chrono::milliseconds( cfg.reaction_time );
			if ( reaction_ready && matches.second )
			{
				firing_target = player.pawn;
				firing_hitbox = restricted_pawn ? restricted_hitbox : -1;
				break;
			}
		}
		if ( !firing_target )
		{
			return;
		}

		const auto ticks_ahead = std::max( plan->latest_tick - seed_tick, 0 );
		const auto planned_at = now + std::chrono::duration_cast<
			std::chrono::steady_clock::duration>( std::chrono::duration<float>(
				static_cast<float>( ticks_ahead ) * game::rules::simulation_step ) );

		const auto final_controller = app::context().process.load<std::uintptr_t>(
			app::context().addresses.local_player_controller );
		if ( final_controller != controller )
		{
			return;
		}
		const auto final_host_session =
			is_host_seed_session( final_controller );
		const auto final_binding = game::resolve_local_pawn( final_controller );
		const auto final_pawn = final_binding.pawn;
		if ( final_pawn != pawn || final_binding.handle != local_binding.handle )
		{
			return;
		}
		const auto final_health = app::context().process.load<std::int32_t>(
			final_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_id ) );
		const auto final_team = app::context().process.load<std::int32_t>(
			final_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_id ) );
		auto final_velocity = app::context().process.load<foundation::vec3>(
			final_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		if ( final_health <= 0 || final_health > 100
			|| final_team != local_team
			|| !std::isfinite( final_velocity.x )
			|| !std::isfinite( final_velocity.y )
			|| !std::isfinite( final_velocity.z ) )
		{
			return;
		}

		foundation::vec3 final_eye{};
		foundation::vec3 final_view{};
		if ( !game::camera().sample( final_eye, final_view ) )
		{
			return;
		}
		seed_command_source final_source{};
		final_source.validated = read_cmd_angles( final_controller,
			final_source.raw_angles, &final_source.sequence );
		if ( !final_source.validated )
		{
			final_source.raw_angles = final_view;
			final_source.sequence = -1;
		}
		final_source.tick = read_seed_tick(
			final_controller, final_pawn, final_host_session );
		const auto final_tick = final_source.tick;
		if ( final_tick != seed_tick
			|| final_source.validated != command_source.validated
			|| ( final_source.validated
				&& final_source.sequence != command_source.sequence ) )
		{
			return;
		}

		ballistics_t::context final_ctx{};
		if ( !seed_shared.seed_weapon(
			final_pawn, final_controller, final_ctx )
			|| ( !final_ctx.weapon_ready
				&& final_ctx.item_def_idx != revolver_id )
			|| final_ctx.is_reloading
			|| final_ctx.weapon != ctx.weapon )
		{
			return;
		}
		const auto final_now = std::chrono::steady_clock::now( );
		const auto final_plan = this->build_seed_plan(
			final_pawn, final_view, final_host_session,
			final_source, final_now, false );
		if ( !final_plan )
		{
			return;
		}

		game::world().seed_players_into( this->m_seed_player_buffer,
			final_pawn, final_controller, final_team, free_for_all,
			firing_target );
		const auto& final_players = this->m_seed_player_buffer;
		const auto final_target = std::ranges::find(
			final_players, firing_target, &game::player_snapshot::pawn );
		if ( final_target == final_players.end( ) || !final_target->game_scene_node )
		{
			return;
		}
		const auto final_matches = evaluate_plan(
			final_players, final_ctx, final_eye, final_velocity, final_tick,
			firing_target, firing_hitbox, *final_plan );
		if ( !final_matches.second )
		{
			return;
		}
		const auto seed_max_speed = final_ctx.fire_mode
			? final_ctx.debug.max_speed.second : final_ctx.debug.max_speed.first;
		const auto required_speed = std::max( 0.5f,
			( std::isfinite( seed_max_speed ) && seed_max_speed > 0.0f
				? seed_max_speed : 250.0f )
			* config::general_settings.m_auto_stop.required_shoot_speed * 0.01f );
		features::misc::auto_stop( ).request_stop(
			features::misc::auto_stop_source::seed_trigger,
			planned_at, required_speed );
		stop_guard.preserve = true;
		if ( !features::misc::auto_stop( ).ready_to_fire(
			features::misc::auto_stop_source::seed_trigger ) )
		{
			return;
		}

		const auto input_publish_at = std::chrono::steady_clock::now( );
		this->m_seed_held_secondary = false;
		this->m_seed_held_proxy =
			cfg.key == VK_LBUTTON && final_ctx.item_def_idx != revolver_id;
		bool press_delivered{};
		if ( this->m_seed_held_proxy )
		{
			press_delivered = app::context().input.key( VK_F24, true );
		}
		else
		{
			// If pre-cock already owns LMB this is a transfer of ownership, not a
			// second click. Otherwise start the accurate primary charge now.
			if ( this->m_revolver_pre_cock_down )
			{
				press_delivered = true;
			}
			else
			{
				press_delivered = app::context().input.pointer( 0, 0,
					platform::windows::pointer_action::primary_down );
			}
			this->m_revolver_pre_cock_down = false;
		}
		if ( !press_delivered )
		{
			// Do not create synthetic ownership for an input event Windows did not
			// accept.  The next poll can safely retry the same still-valid seed.
			this->m_seed_held_proxy = false;
			return;
		}

		features::misc::auto_stop( ).notify_shot(
			features::misc::auto_stop_source::seed_trigger );
		features::visuals::event_log( ).begin_trigger_shot( "Seed Trigger", true );
		this->m_trigger_held = true;
		this->m_seed_press_active = true;
		this->m_revolver_committed = final_ctx.item_def_idx == revolver_id;
		this->m_trigger_release_time =
			final_now + std::chrono::milliseconds(
				this->m_revolver_committed ? 1200 : 40 );
		this->m_seed_pending_target_tick = final_plan->latest_tick;
		this->m_seed_pending_time = final_now;

#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
		foundation::vec3 press_command_angles{};
		g_press_sequence = -1;
		( void )read_cmd_angles( final_controller,
			press_command_angles, &g_press_sequence );
		g_press_tick = final_tick;
		g_press_at = std::chrono::steady_clock::now( );
		VESTA_SEED_COUNT( presses );
		seed_file_write( std::format(
			"PRESS unix_ms={} seq={} tick={} target_tick={} samples={} "
			"hash=({:.4f},{:.4f}) punch=({:.4f},{:.4f}) "
			"recoil_index={:.4f} inaccuracy={:.6f} spread={:.6f} "
			"pipeline_ms={:.3f} final_to_input_us={}",
			unix_ms( ), g_press_sequence, final_tick,
			this->m_seed_pending_target_tick,
			final_plan->sample_count,
			final_plan->samples[ 0 ].hash_angles.x,
			final_plan->samples[ 0 ].hash_angles.y,
			final_plan->prepared_punch.x,
			final_plan->prepared_punch.y,
			final_ctx.recoil_index, final_ctx.inaccuracy, final_ctx.spread,
			std::chrono::duration<float, std::milli>( final_now - now ).count( ),
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now( ) - input_publish_at ).count( ) ) );
#endif

	}

	bool seed_runtime_t::poll( )
	{
		m_controller.seed_tick( m_ballistics );
		return m_controller.seed_hot_path_requested( );
	}

	void seed_runtime_t::reset( )
	{
		m_controller.reset_seed( );
	}

	void controller_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto eye_pos = game::camera().origin( );
		const auto view_angles = game::camera().angles( );

		const auto& ctx = ballistics().ctx( );
		const auto cfg = config::combat_settings.get( ctx.weapon_type );

		if ( !ctx.valid )
		{
			return;
		}

		const auto valid_weapon = game::rules::is_firearm( ctx.weapon_type );

		if ( valid_weapon && cfg.other.penetration_crosshair )
		{
			const auto global_vars = app::context().process.load<std::uintptr_t>( app::context().addresses.global_vars );
			const auto current_time = global_vars ? app::context().process.load<float>( global_vars + 0x30 ) : 0.0f;
			this->draw_penetration_crosshair( draw_list, eye_pos, view_angles, cfg, current_time );
		}

		const auto draw_aim_visualization = cfg.aimbot.draw_fov;
		this->m_fov_alpha.set_target( valid_weapon && draw_aim_visualization
			? 1.0f : 0.0f );
		this->m_fov_alpha.update( );

		if ( this->m_fov_alpha.value( ) <= 0.01f )
		{
			return;
		}

		this->draw_fov_ring( draw_list, eye_pos, view_angles, cfg.aimbot );
	}

	void controller_t::tick( )
	{
		const auto& ctx = ballistics().ctx( );
		if ( ctx.valid )
		{
			if ( ctx.current_time < this->m_last_time - 0.5f )
			{
				this->reset( );
			}
			this->m_last_time = ctx.current_time;
		}

		if ( !this->m_rng_seeded )
		{
			this->m_rng.seed( static_cast< int >( std::chrono::steady_clock::now( ).time_since_epoch( ).count( ) & 0x7fffffff ) );
			this->m_rng_seeded = true;
		}

		auto now = std::chrono::steady_clock::now();
		const auto requested_cfg = config::combat_settings.get(
			game::local_player().weapon_type( ) );
		const auto trigger_activation_active = requested_cfg.triggerbot.enabled
			&& config::combat_profile::activation_active(
				requested_cfg.triggerbot.activation_mode,
				requested_cfg.triggerbot.key );

		constexpr std::uint16_t revolver_id{ 64 };
		const auto revolver_available = ctx.valid
			&& ctx.item_def_idx == revolver_id
			&& requested_cfg.triggerbot.enabled
			&& requested_cfg.triggerbot.revolver_pre_cock
			&& !ctx.is_reloading && ctx.clip != 0;
		if ( this->m_revolver_pre_cock_down
			&& ( !revolver_available || this->m_revolver_weapon != ctx.weapon ) )
		{
			if ( app::context().input.pointer(
				0, 0, platform::windows::pointer_action::primary_up ) )
				this->m_revolver_pre_cock_down = false;
		}
		if ( revolver_available && !this->m_trigger_held )
		{
			this->m_revolver_weapon = ctx.weapon;
			const auto ready_tick = ctx.postpone_fire_ready_tick;
			const auto ticks_left = ready_tick > 0
				? ready_tick - ctx.player_tick : std::numeric_limits<int>::max( );
			const auto ready_fraction = std::isfinite( ctx.postpone_fire_ready_fraction )
				? std::clamp( ctx.postpone_fire_ready_fraction, 0.0f, 1.0f ) : 0.0f;
			const auto milliseconds_left = ticks_left == std::numeric_limits<int>::max( )
				? std::numeric_limits<float>::max( )
				: ( static_cast<float>( ticks_left ) + ready_fraction )
					* game::rules::simulation_step * 1000.0f;
			const auto release_margin = static_cast<float>( std::clamp(
				requested_cfg.triggerbot.revolver_release_margin_ms, 1, 60 ) );
			if ( this->m_revolver_pre_cock_down
				&& ready_tick > 0 && milliseconds_left <= release_margin )
			{
				if ( app::context().input.pointer(
					0, 0, platform::windows::pointer_action::primary_up ) )
					this->m_revolver_pre_cock_down = false;
			}
			else if ( !this->m_revolver_pre_cock_down
				&& ( ready_tick <= 0 || ready_tick <= ctx.player_tick ) )
			{
				this->m_revolver_pre_cock_down = app::context().input.pointer(
					0, 0, platform::windows::pointer_action::primary_down );
			}
		}

		if ( this->m_trigger_held && !this->m_seed_press_active
			&& !trigger_activation_active )
		{
			if ( app::context().input.pointer(
				0, 0, platform::windows::pointer_action::primary_up ) )
			{
				this->m_trigger_held = false;
				this->m_trigger_press_shots = -1;
				this->m_trigger_release_time = {};
				this->m_revolver_committed = false;
			}
			this->m_trigger_shot_scheduled = false;
			this->m_trigger_fire_time = {};
			this->m_trigger_target_valid = false;
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::advanced_trigger );
		}

		if ( this->m_trigger_held )
		{
			bool command_consumed = false;
			if ( ctx.valid && this->m_seed_press_active && this->m_seed_pending_target_tick >= 0 )
			{
				const auto pawn = game::local_player().pawn( );
				const auto command_tick = read_seed_tick( );
				const auto shots_fired = pawn
					? app::context().process.load<std::int32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) )
					: -1;

				command_consumed = command_tick > this->m_seed_pending_target_tick
					|| ( this->m_seed_last_shots >= 0 && shots_fired > this->m_seed_last_shots );
			}
			else if ( !this->m_seed_press_active && this->m_trigger_press_shots >= 0 )
			{
				const auto pawn = game::local_player( ).pawn( );
				const auto shots_fired = pawn
					? app::context( ).process.load<std::int32_t>( pawn
						+ SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) )
					: -1;
				command_consumed = shots_fired > this->m_trigger_press_shots;
			}

			const auto context_lost = !ctx.valid && this->m_seed_press_active;
			if ( context_lost || command_consumed
				|| now >= this->m_trigger_release_time )
			{
				if ( command_consumed )
					features::visuals::event_log( ).mark_latest_trigger_consumed( );
				if ( this->m_seed_press_active )
				{
					// Preserve the existing Seed release path exactly.
					app::context().input.pointer(
						0, 0, platform::windows::pointer_action::primary_up );
					this->m_trigger_held = false;
					this->m_seed_press_active = false;
					this->m_trigger_press_shots = -1;
					this->m_revolver_committed = false;
				}
				else if ( app::context().input.pointer(
					0, 0, platform::windows::pointer_action::primary_up ) )
				{

					this->m_trigger_held = false;
					this->m_trigger_press_shots = -1;
					this->m_revolver_committed = false;
				}
			}
		}

		const auto independent_seed = requested_cfg.triggerbot.seed_type
			!= config::combat_profile::seed_mode::none;
		const auto aimbot_requested = requested_cfg.aimbot.enabled
			&& config::combat_profile::activation_active(
				requested_cfg.aimbot.activation_mode, requested_cfg.aimbot.key );
		const auto trigger_requested = requested_cfg.triggerbot.enabled
			&& !independent_seed
			&& config::combat_profile::activation_active(
				requested_cfg.triggerbot.activation_mode, requested_cfg.triggerbot.key );
		const auto dynamic_fov_visualization_requested = requested_cfg.aimbot.draw_fov
			&& requested_cfg.aimbot.fov_config.selection
				!= config::combat_profile::fov_settings::fixed;

		if ( dynamic_fov_visualization_requested
			&& now >= this->m_next_visual_scan
			&& !aimbot_requested && !trigger_requested )
		{
			simulation::ballistics( ).tick( );
		}

		this->recoil_control(
			requested_cfg.aimbot, aimbot_requested && ctx.valid );
		if ( !aimbot_requested && !trigger_requested && !this->m_trigger_held
			&& !dynamic_fov_visualization_requested )
		{
			this->m_trigger_shot_scheduled = false;
			this->m_trigger_fire_time = {};
			this->m_trigger_target_valid = false;
			this->m_aim_error = {};
			this->m_aim_last_input_sequence = -1;
			this->m_aim_pawn = 0;
			this->m_aim_tracking_lag = 0.0f;
			this->m_aim_velocity_valid = false;
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::advanced_trigger );
			return;
		}

		if ( trigger_requested && this->m_trigger_shot_scheduled )
		{
			static const std::vector<game::player_snapshot> no_targets{};
			this->triggerbot( {}, {}, no_targets, requested_cfg.triggerbot );
		}

		if ( !ctx.valid )
		{
			return;
		}

		const auto valid_weapon = game::rules::is_firearm( ctx.weapon_type );
		const auto cfg = config::combat_settings.get( ctx.weapon_type );
		const auto pawn = game::local_player().pawn( );
		if ( pawn )
		{
			const auto shots_fired = app::context().process.load<std::int32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) );
			if ( this->m_seed_last_shots >= 0 && shots_fired > this->m_seed_last_shots
				&& this->m_seed_pending_target_tick >= 0 )
			{
				this->m_seed_pending_target_tick = -1;
			}
			this->m_seed_last_shots = shots_fired;
		}
#if defined( VESTA_SEED_LOG ) && VESTA_SEED_LOG
		impact_probe_poll( pawn, now );
		seed_stats_flush( now );
#endif

		if ( !valid_weapon )
		{
			return;
		}
		const auto visualization_only = dynamic_fov_visualization_requested
			&& !aimbot_requested && !trigger_requested && !this->m_trigger_held;
		if ( visualization_only && now < this->m_next_visual_scan )
		{
			return;
		}

		combat_frame frame{};
		frame.sequence = ++this->m_combat_sequence;
		frame.timestamp = now;
		frame.camera = game::camera().matrix( );
		frame.eye = game::camera().origin( );
		frame.view_angles = game::camera().angles( );
		static_cast<void>( game::camera().sample( frame.eye, frame.view_angles ) );
		frame.local_pawn = pawn;
		frame.flash_alpha = game::local_player().flash_alpha( );
		frame.weapon = ctx;
		frame.targets = game::world().players( );
		if ( !frame.targets )
		{
			this->flush_recoil_input( );
			return;
		}

		target visual_target{};
		bool visual_target_sampled{};
		if ( cfg.aimbot.draw_fov
			&& cfg.aimbot.fov_config.selection
				!= config::combat_profile::fov_settings::fixed
			&& now >= this->m_next_visual_scan )
		{

			visual_target_sampled = true;
			auto visual_cfg = cfg;
			visual_cfg.aimbot.prediction.enabled = false;
			visual_target = this->choose_target(
				frame.eye, frame.view_angles, *frame.targets, visual_cfg,
				cfg.aimbot.fov_config.selection
					!= config::combat_profile::fov_settings::target_distance );
			std::scoped_lock lock( this->m_indicator_mutex );
			if ( visual_target.player )
			{
				this->m_indicator_point = visual_target.aim_point;
				this->m_indicator_fov = selection_fov(
					cfg.aimbot, frame.eye, visual_target.aim_point );
				this->m_indicator_time = now;
				this->m_indicator_pawn = visual_target.player->pawn;
				this->m_indicator_bone_cache = visual_target.player->bone_cache;
				this->m_indicator_bone = visual_target.bone;
				this->m_indicator_offset = visual_target.aim_offset;
			}

			this->m_next_visual_scan = std::chrono::steady_clock::now( )
				+ std::chrono::milliseconds( 33 );
		}
		if ( visualization_only )
		{
			return;
		}
		const auto frame_block_reason = ctx.is_reloading
			? combat_block_reason::reloading
			: ( ctx.clip == 0
				? combat_block_reason::weapon_not_ready
				: combat_block_reason::none );
		if ( frame_block_reason != combat_block_reason::none )
		{
			this->m_trigger_shot_scheduled = false;
			this->m_trigger_fire_time = {};
			this->m_trigger_target_valid = false;
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::advanced_trigger );
			this->flush_recoil_input( );
			return;
		}

		if ( frame_block_reason == combat_block_reason::none )
		{
			if ( cfg.triggerbot.enabled )
			{
				const auto independent_seed = cfg.triggerbot.seed_type
					!= config::combat_profile::seed_mode::none;
				if ( !independent_seed )
				{
					this->triggerbot(
						frame.eye, frame.view_angles, *frame.targets, cfg.triggerbot );
				}
			}

			if ( cfg.aimbot.enabled && !this->m_trigger_held )
			{

				if ( !config::combat_profile::activation_active(
					cfg.aimbot.activation_mode, cfg.aimbot.key ) )
				{
					// Keep the exact release/reset semantics from aimbot().
					this->m_aim_error = {};
					this->m_aim_last_input_sequence = -1;
					this->m_aim_pawn = 0;
					this->m_aim_tracking_lag = 0.0f;
					this->m_aim_velocity_valid = false;
				}
				else
				{
					if ( local_checks_blocked( cfg.aimbot.checks ) )
					{
						this->m_aim_pawn = 0;
						this->flush_recoil_input( );
						return;
					}
					auto selected_target = visual_target_sampled
						? visual_target : this->choose_target(
							frame.eye, frame.view_angles, *frame.targets, cfg );
					if ( selected_target.player && selected_target.fov
						> selection_fov( cfg.aimbot, frame.eye, selected_target.aim_point ) )
						selected_target = {};
					if ( selected_target.player )
					{
						this->aimbot(
							frame.eye, frame.view_angles, selected_target, cfg.aimbot );
					}
				}
			}
		}
		this->flush_recoil_input( );
	}

	controller_t::target controller_t::choose_target(
		const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
		const std::vector<game::player_snapshot>& players,
		const config::combat_profile::resolved_config& config,
		bool enforce_fov ) const
	{
		target selected{};
		selected.fov = std::numeric_limits<float>::max( );

		for ( const auto& player : players )
		{
			const auto eligible = game::local_player().is_enemy( player.team )
				&& !player.invulnerable && player.hitboxes.count > 0
				&& player.bones.is_valid( );
			if ( !eligible )
				continue;

			target candidate{};
			candidate.player = &player;
			candidate.bones = player.bones;
			candidate.aim_point = get_aim_point( eye_pos, view_angles, player,
				candidate.bones, config, candidate.damage, candidate.hitbox,
				candidate.bone, candidate.penetrated, candidate.aim_offset );
			if ( candidate.hitbox < 0 )
				continue;

			candidate.fov = foundation::angular_distance(
				view_angles, eye_pos, candidate.aim_point );
			const auto candidate_limit = selection_fov(
				config.aimbot, eye_pos, candidate.aim_point );
			if ( ( !enforce_fov || candidate.fov <= candidate_limit )
				&& candidate.fov <= selected.fov )
				selected = std::move( candidate );
		}
		return selected;
	}

	foundation::vec3 controller_t::get_aim_point( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const game::player_snapshot& player, const game::skeleton_reader::data& bones, const config::combat_profile::resolved_config& cfg, float& out_damage, int& out_hitbox, int& out_bone, bool& out_penetrated, foundation::vec3& out_offset ) const
	{
		out_hitbox = -1;
		out_bone = -1;
		out_offset = {};
		if ( !game::collision().valid( ) )
		{
			return {};
		}

		foundation::vec3 best_point{};
		auto best_fov = std::numeric_limits<float>::max( );
		foundation::vec3 selection_prediction{};
		if ( cfg.aimbot.prediction.enabled )
		{
			const auto local_pawn = game::local_player().pawn( );
			auto local_velocity = local_pawn
				? app::context().process.load<foundation::vec3>( local_pawn
					+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) )
				: foundation::vec3{};
			auto target_velocity = player.velocity;
			const auto valid_velocity = []( const foundation::vec3& velocity )
			{
				return std::isfinite( velocity.x ) && std::isfinite( velocity.y )
					&& std::isfinite( velocity.z )
					&& velocity.length_sqr( ) < 4'000'000.0f;
			};
			if ( !valid_velocity( target_velocity ) ) target_velocity = {};
			if ( !valid_velocity( local_velocity ) ) local_velocity = {};

			const auto target_flags = app::context().process.load<std::uint32_t>(
				player.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
			const auto local_flags = local_pawn
				? app::context().process.load<std::uint32_t>( local_pawn
					+ SCHEMA( "C_BaseEntity", "m_fFlags"_id ) ) : 0u;
			if ( ( target_flags & 1u ) != 0u && ( local_flags & 1u ) != 0u )
			{

				target_velocity.z = 0.0f;
				local_velocity.z = 0.0f;
			}
			const auto combat = ballistics().ctx( );
			const auto sample_age = player.simulation_time > 0.0f
				&& std::isfinite( player.simulation_time )
				&& std::isfinite( combat.current_time )
				? std::clamp( combat.current_time - player.simulation_time,
					0.0f, 0.12f ) : 0.0f;
			const auto horizon = std::clamp(
				sample_age + 0.004f, 0.0f,
				std::clamp( cfg.aimbot.prediction.max_horizon_ms,
					0.0f, 120.0f ) * 0.001f );
			selection_prediction = ( target_velocity - local_velocity ) * horizon;
		}

		for ( const auto& hb : player.hitboxes )
		{
			if ( hb.index < 0 || hb.bone < 0 || hb.bone >= 128 )
			{
				continue;
			}

			const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox( hb.index );
			const auto part = part_from_hitbox( hb, hitgroup );
			if ( !( cfg.aimbot.hitbox_parts & part ) )
			{
				continue;
			}

			const auto& bone = bones.bones[ hb.bone ];
			const auto candidates = build_aim_points( bone, hb,
				cfg.aimbot.multipoint, part, cfg.aimbot.multipoint_config );

			for ( int i = 0; i < candidates.count; ++i )
			{
				const auto base_pos = candidates.points[ i ];
				const auto pos = base_pos + selection_prediction;

				const auto fov = this->get_fov( view_angles, eye_pos, pos );
				if ( fov >= best_fov )
				{
					continue;
				}

				auto damage{ 0.0f };
				auto penetrated{ false };
				auto accepted{ false };

				if ( cfg.aimbot.checks.smoke && line_through_smoke( eye_pos, pos ) )
				{
					continue;
				}

				const auto trace = game::collision().trace_ray( eye_pos, pos );

				if ( !trace.hit )
				{
					damage = simulation::ballistics().pen( ).get_max_damage(
						hitgroup, player.armor, player.has_helmet, player.team );
					accepted = true;
				}
				else if ( cfg.aimbot.checks.walls
					!= config::combat_profile::wall_policy::block )
				{
					ballistics_t::penetration::result pen_result{};
					if ( simulation::ballistics().pen( ).run(
						eye_pos, pos, player, bones, pen_result )
						&& pen_result.damage >= cfg.aimbot.min_damage )
					{
						damage = pen_result.damage;
						penetrated = pen_result.penetrated;
						accepted = true;
					}
				}

				if ( !accepted )
				{
					continue;
				}

				constexpr auto head_hitgroup = 1;
				if ( cfg.aimbot.lethal_only && hitgroup != head_hitgroup
					&& damage + 0.001f < static_cast<float>( player.health ) )
				{
					continue;
				}

				best_fov = fov;
				best_point = pos;
				out_damage = damage;
				out_hitbox = hb.index;
				out_bone = hb.bone;
				out_penetrated = penetrated;

				const foundation::rotation inverse{
					-bone.rotation.x, -bone.rotation.y,
					-bone.rotation.z, bone.rotation.w };
				out_offset = inverse.apply( base_pos - bone.position );
			}
		}

		return best_point;
	}

	float controller_t::get_fov( const foundation::vec3& view_angles, const foundation::vec3& eye_pos, const foundation::vec3& target_pos ) const
	{
		return foundation::angular_distance( view_angles, eye_pos, target_pos );
	}

	float controller_t::screen_radius_for_fov(
		const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
		float angle_degrees ) const
	{
		if ( angle_degrees <= 0.0f )
			return 0.0f;
		const auto [ width, height ] = zdraw::get_display_size( );
		if ( angle_degrees >= 90.0f )
			return static_cast<float>( std::max( width, height ) ) * 2.0f;

		const auto projected_direction = [ & ]( foundation::vec3 angles )
		{
			foundation::vec3 direction{};
			angles.to_directions( &direction, nullptr, nullptr );
			return game::camera().project( eye_pos + direction * 1000.0f );
		};

		const auto center = projected_direction( view_angles );
		auto boundary_angles = view_angles;
		boundary_angles.x -= angle_degrees;
		const auto boundary = projected_direction( boundary_angles );
		if ( !game::camera().projection_valid( center )
			|| !game::camera().projection_valid( boundary ) )
		{
			return 0.0f;
		}
		return std::hypot( boundary.x - center.x, boundary.y - center.y );
	}

	void controller_t::draw_penetration_crosshair( zdraw::draw_list& draw_list, const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::resolved_config& cfg, float current_time )
	{
		foundation::vec3 forward{};
		view_angles.to_directions( &forward, nullptr, nullptr );

		const auto first_hit = game::collision().trace_ray( eye_pos, eye_pos + forward * ballistics().pen( ).get_weapon_data( ).range );
		if ( !first_hit.hit )
		{
			return;
		}

		auto pen_damage{ 0.0f };
		const auto can_pen = ballistics().pen( ).can( eye_pos, forward, pen_damage );

		if ( this->m_pen_color_can_pen != can_pen )
		{
			const auto& from_color = can_pen ? cfg.other.penetration_color_no : cfg.other.penetration_color_yes;
			const auto& to_color = can_pen ? cfg.other.penetration_color_yes : cfg.other.penetration_color_no;

			this->m_pen_color_tween.start( 0.0f, 1.0f, 0.15f, motion::curve::smooth );
			this->m_pen_color_can_pen = can_pen;
		}

		this->m_pen_color_tween.update( );

		const auto t = this->m_pen_color_tween.value( );
		const auto& color_a = this->m_pen_color_can_pen ? cfg.other.penetration_color_no : cfg.other.penetration_color_yes;
		const auto& color_b = this->m_pen_color_can_pen ? cfg.other.penetration_color_yes : cfg.other.penetration_color_no;

		const auto interp_r = static_cast<std::uint8_t>( color_a.r + ( color_b.r - color_a.r ) * t );
		const auto interp_g = static_cast<std::uint8_t>( color_a.g + ( color_b.g - color_a.g ) * t );
		const auto interp_b = static_cast<std::uint8_t>( color_a.b + ( color_b.b - color_a.b ) * t );
		const auto interp_a = static_cast<std::uint8_t>( color_a.a + ( color_b.a - color_a.a ) * t );

		const auto color = zdraw::rgba( interp_r, interp_g, interp_b, interp_a );

		const auto[w, h] = zdraw::get_display_size( );
		const auto cx = w * 0.5f;
		const auto cy = h * 0.5f;
		constexpr auto size{ 6.0f };

		draw_list.add_line( cx - size, cy, cx + size, cy, color, 2.0f );
		draw_list.add_line( cx, cy - size, cx, cy + size, color, 2.0f );
	}

	void controller_t::draw_fov_ring( zdraw::draw_list& draw_list,
		const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
		const config::combat_profile::aimbot& config )
	{
		const auto visibility = m_fov_alpha.value( );
		foundation::vec3 point{};
		std::chrono::steady_clock::time_point updated{};
		float selected_fov{};
		std::uintptr_t selected_pawn{};
		std::uintptr_t selected_bone_cache{};
		int selected_bone{ -1 };
		foundation::vec3 selected_offset{};
		{
			std::scoped_lock lock( this->m_indicator_mutex );
			point = this->m_indicator_point;
			updated = this->m_indicator_time;
			selected_fov = this->m_indicator_fov;
			selected_pawn = this->m_indicator_pawn;
			selected_bone_cache = this->m_indicator_bone_cache;
			selected_bone = this->m_indicator_bone;
			selected_offset = this->m_indicator_offset;
		}
		if ( selected_pawn && selected_bone >= 0 )
		{
			bool refreshed{};
			if ( const auto pose = game::render_poses( ).latest( ) )
			{
				for ( const auto& player : pose->players )
				{
					if ( player.pawn != selected_pawn
						|| player.bone_cache != selected_bone_cache ) continue;
					if ( selected_bone < 128 && player.bones.is_valid( ) )
					{
						const auto& bone = player.bones.bones[ selected_bone ];
						point = bone.position + bone.rotation.apply( selected_offset );
						refreshed = true;
					}
					break;
				}
			}
			if ( !refreshed && selected_bone_cache )
			{
				const auto bones = game::skeletons( ).get( selected_bone_cache );
				if ( selected_bone < 128 && bones.is_valid( ) )
				{
					const auto& bone = bones.bones[ selected_bone ];
					point = bone.position + bone.rotation.apply( selected_offset );
				}
			}
		}
		const auto target_recent = updated.time_since_epoch( ).count( ) != 0
			&& std::chrono::steady_clock::now( ) - updated <= std::chrono::milliseconds( 100 );

		const auto mode = config.fov_config.selection;
		if ( config.draw_fov && mode != config::combat_profile::fov_settings::target_distance )
		{
			auto degrees = static_cast<float>( config.fov );
			if ( mode == config::combat_profile::fov_settings::distance )
			{
				if ( target_recent && selected_fov > 0.0f )
					degrees = selected_fov;
				else
				{
					foundation::vec3 forward{};
					view_angles.to_directions( &forward, nullptr, nullptr );
					const auto max_distance = std::max( config.fov_config.far_distance_m
						* 52.4934f, 1.0f );
					const auto surface = game::collision().trace_ray(
						eye_pos, eye_pos + forward * max_distance );
					degrees = distance_fov( config,
						surface.hit ? surface.distance : max_distance );
				}
			}
			const auto radius = screen_radius_for_fov( eye_pos, view_angles, degrees );
			if ( radius > 0.5f )
			{
				const auto [ width, height ] = zdraw::get_display_size( );
				const auto color = zdraw::rgba{ config.fov_color.r, config.fov_color.g,
					config.fov_color.b, static_cast<std::uint8_t>( visibility * 125.0f ) };
				draw_list.add_circle( width * 0.5f, height * 0.5f, radius, color, 64 );
			}
		}

		if ( config.draw_fov && mode == config::combat_profile::fov_settings::target_distance
			&& target_recent )
		{
			const auto screen = game::camera().project( point );
			if ( !game::camera().projection_valid( screen ) ) return;
			const auto color = zdraw::rgba{ config.fov_color.r, config.fov_color.g,
				config.fov_color.b, static_cast<std::uint8_t>( visibility * 150.0f ) };
			const auto target_angles = foundation::look_at_angles( eye_pos, point );
			const auto radius = screen_radius_for_fov( eye_pos, target_angles,
				selected_fov > 0.0f ? selected_fov : distance_fov( config, ( point - eye_pos ).length( ) ) );
			if ( radius > 0.5f )
				draw_list.add_circle( screen.x, screen.y, radius, color, 64 );
		}
	}

	void controller_t::recoil_control(
		const config::combat_profile::aimbot& cfg, bool defer_input )
	{
		const auto clear_correction = [ this ]( bool clear_burst )
		{
			this->m_rcs_raw = {};
			this->m_rcs_target = {};
			this->m_rcs_velocity = {};
			this->m_rcs_applied = {};
			this->m_rcs_mouse_error = {};
			this->m_rcs_pending_mouse = {};
			this->m_rcs_last_input_sequence = -1;
			this->m_rcs_last_input_time = {};
			this->m_rcs_input_step = 24;
			this->m_rcs_active = false;
			if ( clear_burst )
			{
				this->m_rcs_weapon = 0;
				this->m_rcs_last_clip = -1;
				this->m_rcs_last_shot_time = -1.0f;
				this->m_rcs_burst_shots = 0;
			}
		};
		const auto pawn = game::local_player().pawn( );
		if ( !pawn || !cfg.rcs.enabled )
		{
			clear_correction( true );
			return;
		}

		const auto& weapon_ctx = ballistics().ctx( );
		const auto weapon = weapon_ctx.valid
			? weapon_ctx.weapon : game::local_player().weapon( );
		if ( weapon != this->m_rcs_weapon )
		{
			clear_correction( true );
			this->m_rcs_weapon = weapon;
		}

		if ( !weapon_ctx.valid || weapon_ctx.weapon != weapon
			|| !weapon_ctx.is_full_auto )
		{
			clear_correction( true );
			return;
		}
		const auto reported_shots = app::context().process.load<std::int32_t>(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) );
		if ( weapon_ctx.valid && weapon_ctx.weapon == weapon )
		{
			if ( this->m_rcs_last_clip >= 0 && weapon_ctx.clip >= 0
				&& weapon_ctx.clip < this->m_rcs_last_clip )
			{
				this->m_rcs_burst_shots +=
					this->m_rcs_last_clip - weapon_ctx.clip;
			}
			this->m_rcs_last_clip = weapon_ctx.clip;
			if ( weapon_ctx.last_shot_time > this->m_rcs_last_shot_time + 0.0001f )
			{
				this->m_rcs_last_shot_time = weapon_ctx.last_shot_time;
				this->m_rcs_burst_shots = std::max(
					this->m_rcs_burst_shots,
					std::max( 1, static_cast<int>( std::ceil( weapon_ctx.recoil_index ) ) ) );
			}
			this->m_rcs_burst_shots = std::max( this->m_rcs_burst_shots,
				std::max( 0, static_cast<int>( std::ceil( weapon_ctx.recoil_index ) ) ) );
		}
		this->m_rcs_burst_shots = std::max(
			this->m_rcs_burst_shots, std::max( reported_shots, 0 ) );

		const auto physical_attack = ( ::GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
		const auto attack_owned_by_trigger = this->m_trigger_held
			&& !this->m_seed_held_secondary && !this->m_seed_held_proxy;
		if ( !physical_attack && !attack_owned_by_trigger )
		{
			clear_correction( false );
			this->m_rcs_burst_shots = 0;
			return;
		}
		if ( this->m_rcs_burst_shots < std::max( cfg.rcs.start_bullet, 1 ) )
		{
			clear_correction( false );
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		auto dt = std::chrono::duration<float>( now - this->m_rcs_last_call ).count( );
		if ( dt <= 0.0f || dt > 0.1f ) dt = 0.004f;
		this->m_rcs_last_call = now;

		const auto sensitivity = game::variables().get<float>( CONVAR( "sensitivity"_id ) );
		const auto fov_adjust = app::context().process.load<float>(
			pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id ) );
		const auto degrees_per_pixel = sensitivity * 0.022f * fov_adjust;
		if ( degrees_per_pixel <= 0.0f ) return;

		const auto punch = read_aim_punch( pawn );
		this->m_rcs_raw = punch;

		if ( !this->m_rcs_active )
		{
			// Humanization varies both the small residual error and the time profile of
			// this burst. It must not turn a high setting into a wildly wrong endpoint.
			const auto spread = std::clamp(
				cfg.rcs.randomness, 0.0f, 30.0f ) * 0.001f;
			this->m_rcs_gain = this->m_rng.uniform( 1.0f - spread, 1.0f + spread );
			const auto timing_spread = std::clamp(
				cfg.rcs.randomness, 0.0f, 30.0f ) * 0.003f;
			this->m_rcs_response_scale = this->m_rng.uniform(
				1.0f - timing_spread, 1.0f + timing_spread );
			this->m_rcs_phase = this->m_rng.uniform( 0.0f, 2.0f * std::numbers::pi_v<float> );
			this->m_rcs_freq = this->m_rng.uniform( 1.5f, 3.5f );
			this->m_rcs_active = true;
		}
		this->m_rcs_phase += this->m_rcs_freq * dt;

		auto desired = punch * this->m_rcs_gain;
		desired.x *= std::clamp( cfg.rcs.pitch, 0.0f, 200.0f ) * 0.01f;
		desired.y *= std::clamp( cfg.rcs.yaw, 0.0f, 200.0f ) * 0.01f;
		const auto drift = std::clamp( cfg.rcs.drift, 0.0f, 30.0f ) * 0.002f;
		desired.x += punch.length_2d( ) * drift * std::sin( this->m_rcs_phase );
		desired.y += punch.length_2d( ) * drift * 0.8f
			* std::cos( this->m_rcs_phase * 1.27f );

		const auto smoothness = std::clamp( cfg.rcs.smoothness, 0.0f, 100.0f ) * 0.01f;
		const auto target_follow_time = std::lerp( 0.0f, 0.020f, smoothness );
		if ( target_follow_time <= 0.0001f )
		{
			this->m_rcs_target = desired;
		}
		else
		{
			const auto target_blend = 1.0f - std::exp(
				-dt / std::max( target_follow_time, 0.001f ) );
			this->m_rcs_target += ( desired - this->m_rcs_target ) * target_blend;
		}

		const auto follow_time = std::clamp(
			cfg.rcs.response_ms * this->m_rcs_response_scale
				* std::lerp( 1.0f, 1.35f, smoothness ),
			1.0f, 300.0f ) * 0.001f;
		const auto velocity_time = std::lerp( 0.0f, 0.018f, smoothness );
		const auto max_speed = std::lerp( 360.0f, 80.0f, smoothness );
		const auto smooth_axis = [ dt, follow_time, velocity_time, max_speed ](
			float current, float target, float& velocity )
		{
			const auto error = target - current;
			const auto desired_velocity = std::clamp(
				error / std::max( follow_time, 0.001f ), -max_speed, max_speed );
			const auto velocity_blend = velocity_time <= 0.0001f ? 1.0f
				: 1.0f - std::exp( -dt / velocity_time );
			velocity += ( desired_velocity - velocity ) * velocity_blend;
			auto next = current + velocity * dt;
			if ( ( target - current ) * ( target - next ) <= 0.0f )
			{
				next = target;
				velocity = 0.0f;
			}
			return next;
		};
		const auto previous = this->m_rcs_applied;
		this->m_rcs_applied.x = smooth_axis(
			this->m_rcs_applied.x, this->m_rcs_target.x, this->m_rcs_velocity.x );
		this->m_rcs_applied.y = smooth_axis(
			this->m_rcs_applied.y, this->m_rcs_target.y, this->m_rcs_velocity.y );
		this->m_rcs_applied.z = 0.0f;
		this->m_rcs_input_step = std::max( 1, static_cast<int>( std::lround(
			std::lerp( 24.0f, 5.0f, smoothness ) ) ) );
		const auto delta = this->m_rcs_applied - previous;
		this->m_rcs_mouse_error.x += delta.y / degrees_per_pixel;
		this->m_rcs_mouse_error.y -= delta.x / degrees_per_pixel;
		const auto dx = static_cast<int>( this->m_rcs_mouse_error.x );
		const auto dy = static_cast<int>( this->m_rcs_mouse_error.y );
		this->m_rcs_mouse_error.x -= static_cast<float>( dx );
		this->m_rcs_mouse_error.y -= static_cast<float>( dy );
		if ( dx || dy )
		{
			this->m_rcs_pending_mouse.x += static_cast<float>( dx );
			this->m_rcs_pending_mouse.y += static_cast<float>( dy );
		}
		if ( !defer_input ) this->flush_recoil_input( );
	}

	void controller_t::flush_recoil_input( )
	{
		const auto limit = std::max( this->m_rcs_input_step, 1 );
		const auto dx = std::clamp(
			static_cast<int>( this->m_rcs_pending_mouse.x ), -limit, limit );
		const auto dy = std::clamp(
			static_cast<int>( this->m_rcs_pending_mouse.y ), -limit, limit );
		if ( !dx && !dy ) return;
		const auto now = std::chrono::steady_clock::now( );
		if ( this->m_rcs_last_input_time.time_since_epoch( ).count( ) != 0
			&& now - this->m_rcs_last_input_time < std::chrono::milliseconds( 2 ) ) return;
		if ( ( dx || dy ) && app::context().input.pointer(
			dx, dy, platform::windows::pointer_action::relative_move ) )
		{
			this->m_rcs_pending_mouse.x -= static_cast<float>( dx );
			this->m_rcs_pending_mouse.y -= static_cast<float>( dy );
			this->m_rcs_last_input_sequence = -1;
			this->m_rcs_last_input_time = now;
		}
	}

	void controller_t::aimbot( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const target& tgt, const config::combat_profile::aimbot& cfg )
	{
		if ( !config::combat_profile::activation_active(
			cfg.activation_mode, cfg.key ) )
		{
			this->m_aim_error = {};
			this->m_aim_last_input_sequence = -1;
			this->m_aim_virtual_angles_valid = false;
			this->m_aim_last_call = {};
			this->m_aim_pawn = 0; // отпустили клавишу → следующий захват как новый
			this->m_aim_tracking_lag = 0.0f;
			this->m_aim_velocity_valid = false;
			return;
		}

		constexpr auto m_yaw{ 0.022f };
		const auto local_pawn = game::local_player().pawn( );
		if ( !local_pawn )
		{
			return;
		}
		if ( local_checks_blocked( cfg.checks )
			|| live_target_invulnerable( tgt.player ? tgt.player->pawn : 0,
				tgt.player ? tgt.player->invulnerable : true ) )
		{
			return;
		}
		const auto sensitivity = game::variables().get<float>( CONVAR( "sensitivity"_id ) );
		float fov_adjust{};
		const auto have_fov_adjust = app::context().process.copy( local_pawn
			+ SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id ),
			&fov_adjust, sizeof( fov_adjust ) );
		const auto sampled_degrees = sensitivity * m_yaw * fov_adjust;
		if ( have_fov_adjust && std::isfinite( sampled_degrees )
			&& sampled_degrees >= 0.0001f && sampled_degrees <= 1.0f )
		{
			if ( this->m_aim_degrees_per_pixel <= 0.0f
				|| sampled_degrees >= this->m_aim_degrees_per_pixel * 0.8f )
			{

				this->m_aim_degrees_per_pixel = sampled_degrees;
				this->m_aim_degrees_candidate = {};
				this->m_aim_degrees_confirmations = {};
			}
			else
			{

				const auto same_candidate = this->m_aim_degrees_candidate > 0.0f
					&& std::abs( sampled_degrees - this->m_aim_degrees_candidate )
						<= this->m_aim_degrees_candidate * 0.05f;
				this->m_aim_degrees_confirmations = same_candidate
					? this->m_aim_degrees_confirmations + 1 : 1;
				this->m_aim_degrees_candidate = sampled_degrees;
				if ( this->m_aim_degrees_confirmations >= 2 )
				{
					this->m_aim_degrees_per_pixel = sampled_degrees;
					this->m_aim_degrees_candidate = {};
					this->m_aim_degrees_confirmations = {};
				}
			}
		}
		const auto deg_per_pixel = this->m_aim_degrees_per_pixel;

		if ( !std::isfinite( deg_per_pixel ) || deg_per_pixel <= 0.0f )
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		int input_sequence{ -1 };
		foundation::vec3 raw_angles{};
		auto command_angles_valid = read_cmd_angles(
			game::local_player().controller( ), raw_angles, &input_sequence );
		if ( command_angles_valid )
		{
			const auto camera_gap = std::hypot(
				raw_angles.x - view_angles.x,
				foundation::wrap_yaw( raw_angles.y - view_angles.y ) );
			if ( !std::isfinite( camera_gap ) || camera_gap > 35.0f )
			{
				// Keep the validated sequence boundary but do not steer from a stale or
				// incorrectly resolved ring slot.
				command_angles_valid = false;
			}
		}
		const auto sequence_changed = input_sequence >= 0
			&& input_sequence != this->m_aim_last_input_sequence;

		auto freshest = game::skeletons().get( tgt.player->bone_cache );
		if ( !freshest.is_valid( ) ) freshest = tgt.bones;
		if ( !freshest.is_valid( ) ) return;

		// Reconstruct the picked point from the freshest complete bone transform.
		auto aim_point = tgt.aim_point;
		if ( tgt.bone >= 0 )
		{
			const auto& fresh_bone = freshest.bones[ tgt.bone ];
			const auto rotation_norm = fresh_bone.rotation.x * fresh_bone.rotation.x
				+ fresh_bone.rotation.y * fresh_bone.rotation.y
				+ fresh_bone.rotation.z * fresh_bone.rotation.z
				+ fresh_bone.rotation.w * fresh_bone.rotation.w;
			const auto fresh_point = fresh_bone.position
				+ fresh_bone.rotation.apply( tgt.aim_offset );
			const auto valid_point = std::isfinite( fresh_point.x )
				&& std::isfinite( fresh_point.y ) && std::isfinite( fresh_point.z )
				&& std::isfinite( rotation_norm )
				&& rotation_norm >= 0.5f && rotation_norm <= 1.5f
				&& fresh_point.distance_sqr( tgt.aim_point ) <= 128.0f * 128.0f;
			if ( valid_point ) aim_point = fresh_point;
		}

		// Smoothing 0 is an explicit snap mode. Humanizer path shaping must not turn
		// it back into a delayed move or manufacture an overshoot around the target.
		const auto snap = cfg.smoothing <= 0;
		const auto h = snap ? 0.0f
			: std::clamp( cfg.humanize, 0, 100 ) / 100.0f;

		auto dt = std::chrono::duration<float>( now - this->m_aim_last_call ).count( );
		if ( dt <= 0.0f || dt > 0.1f )
		{
			dt = game::rules::simulation_step; // first command after a pause
		}
		this->m_aim_last_call = now;

		auto sampled_control_angles = view_angles;
		if ( command_angles_valid ) sampled_control_angles = raw_angles;
		if ( this->m_rcs_active )
		{
			sampled_control_angles += this->m_rcs_applied;

			sampled_control_angles.x += this->m_rcs_pending_mouse.y * deg_per_pixel;
			sampled_control_angles.y -= this->m_rcs_pending_mouse.x * deg_per_pixel;
			sampled_control_angles.y = foundation::wrap_yaw( sampled_control_angles.y );
		}
		if ( !this->m_aim_virtual_angles_valid || sequence_changed
			|| now - this->m_aim_last_input_time > std::chrono::milliseconds( 50 ) )
		{
			this->m_aim_virtual_angles = sampled_control_angles;
			this->m_aim_virtual_angles_valid = true;
		}
		auto control_angles = this->m_aim_virtual_angles;
		this->m_aim_last_input_sequence = input_sequence;
		this->m_aim_last_input_view = view_angles;

		const auto target_angle = [ & ]( const foundation::vec3& point )
			{
				auto angle = foundation::look_at_angles( eye_pos, point );
				angle.y = foundation::wrap_yaw( angle.y );
				return angle;
			};

		auto desired = target_angle( aim_point );
		auto delta_x = desired.x - control_angles.x;
		auto delta_y = foundation::wrap_yaw( desired.y - control_angles.y );
		auto dist = std::sqrt( delta_x * delta_x + delta_y * delta_y );

		const auto reacquire = tgt.player->pawn != this->m_aim_pawn
			|| now - this->m_aim_last_seen > std::chrono::milliseconds( 400 );
		if ( reacquire )
		{
			this->m_aim_pawn = tgt.player->pawn;
			this->m_aim_tracking_lag = 0.0f;
			this->m_aim_velocity_valid = false;
			this->m_aim_relative_acceleration = {};
			this->m_aim_simulation_tick = -1;
			this->m_aim_simulation_time = 0.0f;
			this->m_aim_velocity_samples = 0;
			if ( h > 0.0f )
			{
				const auto reaction_min = std::clamp(
					cfg.humanizer.reaction_min_ms, 0, 500 );
				const auto reaction_max = std::clamp(
					cfg.humanizer.reaction_max_ms, reaction_min, 750 );
				this->m_aim_reaction_until = now + std::chrono::milliseconds(
					static_cast<int>( h * this->m_rng.uniform(
						static_cast<float>( reaction_min ),
						static_cast<float>( reaction_max ) ) ) );
				this->m_aim_initial_dist = std::max( dist, 0.5f );
				this->m_aim_curve = ( this->m_rng.uniform( 0.0f, 1.0f ) < 0.5f ? -1.0f : 1.0f )
					* h * std::clamp( cfg.humanizer.curve, 0.0f, 1.0f );
				this->m_aim_overshoot = this->m_rng.uniform( 0.0f, 100.0f )
					< std::clamp( cfg.humanizer.overshoot_chance, 0.0f, 100.0f ) * h
					? 1.0f + h * std::clamp(
						cfg.humanizer.overshoot_amount, 0.0f, 1.0f ) : 1.0f;
				this->m_aim_ramp = 0.0f;
				this->m_aim_wander_phase = this->m_rng.uniform( 0.0f, 6.28f );
				this->m_aim_wander_freq = this->m_rng.uniform( 0.35f, 0.85f );
			}
		}
		this->m_aim_last_seen = now;

		if ( h > 0.0f && now < this->m_aim_reaction_until ) return;

		auto factor = cfg.smoothing > 1 ? static_cast<float>( cfg.smoothing ) : 1.0f;
		const auto overshoot_response = h > 0.0f ? this->m_aim_overshoot : 1.0f;
		if ( h > 0.0f )
			this->m_aim_ramp = std::min( 1.0f, this->m_aim_ramp + dt / 0.055f );
		const auto response_ramp = h > 0.0f
			? 0.75f + 0.25f * this->m_aim_ramp : 1.0f;
		const auto command_response = std::clamp(
			response_ramp / factor, 0.001f, 1.0f );
		const auto command_fraction = std::clamp(
			dt / game::rules::simulation_step, 0.01f, 4.0f );
		const auto response_alpha = command_response >= 1.0f
			? 1.0f : 1.0f - std::pow( 1.0f - command_response, command_fraction );
		const auto prediction_response = std::clamp(
			response_alpha * overshoot_response, 0.001f, 1.0f );

		if ( cfg.prediction.enabled )
		{
			this->m_aim_tracking_lag = std::clamp(
				( 1.0f - prediction_response ) * ( this->m_aim_tracking_lag + dt ), 0.0f, 0.12f );

			auto target_velocity = tgt.player->velocity;
			auto local_velocity = app::context().process.load<foundation::vec3>(
				local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
			const auto valid_velocity = []( const foundation::vec3& velocity )
				{
					return std::isfinite( velocity.x ) && std::isfinite( velocity.y ) &&
						std::isfinite( velocity.z ) && velocity.length_sqr( ) < 4'000'000.0f;
				};
			if ( !valid_velocity( target_velocity ) ) target_velocity = {};
			if ( !valid_velocity( local_velocity ) ) local_velocity = {};
			const auto target_flags = app::context().process.load<std::uint32_t>(
				tgt.player->pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
			const auto local_flags = app::context().process.load<std::uint32_t>(
				local_pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
			const auto target_airborne = ( target_flags & 1u ) == 0u;
			const auto local_airborne = ( local_flags & 1u ) == 0u;
			if ( !target_airborne && !local_airborne )
			{
				target_velocity.z = 0.0f;
				local_velocity.z = 0.0f;
			}

			const auto relative_velocity = target_velocity - local_velocity;
			const auto new_simulation_sample = tgt.player->simulation_tick >= 0
				&& tgt.player->simulation_tick != this->m_aim_simulation_tick;
			if ( !this->m_aim_velocity_valid )
			{
				this->m_aim_previous_relative_velocity = relative_velocity;
				this->m_aim_relative_acceleration = {};
				this->m_aim_velocity_valid = true;
				this->m_aim_velocity_samples = 1;
				this->m_aim_simulation_tick = tgt.player->simulation_tick;
				this->m_aim_simulation_time = tgt.player->simulation_time;
			}
			else if ( new_simulation_sample )
			{
				const auto sample_dt = std::max(
					tgt.player->simulation_time - this->m_aim_simulation_time,
					game::rules::simulation_step );
				auto observed_acceleration = ( relative_velocity - this->m_aim_previous_relative_velocity ) /
					sample_dt;
				const auto acceleration_length = observed_acceleration.length( );
				const auto direction_break = relative_velocity.length_2d( ) > 40.0f
					&& this->m_aim_previous_relative_velocity.length_2d( ) > 40.0f
					&& relative_velocity.normalized( ).dot(
						this->m_aim_previous_relative_velocity.normalized( ) ) < -0.25f;
				if ( acceleration_length > 3500.0f || direction_break )
				{
					this->m_aim_relative_acceleration = {};
					this->m_aim_velocity_samples = 1;
				}
				else
				{
					const auto acceleration_blend = 1.0f - std::exp( -sample_dt / 0.04f );
					this->m_aim_relative_acceleration =
						this->m_aim_relative_acceleration * ( 1.0f - acceleration_blend )
						+ observed_acceleration * acceleration_blend;
					++this->m_aim_velocity_samples;
				}
				this->m_aim_previous_relative_velocity = relative_velocity;
				this->m_aim_simulation_tick = tgt.player->simulation_tick;
				this->m_aim_simulation_time = tgt.player->simulation_time;
			}

			const auto gravity =
				game::variables().get<float>( CONVAR( "sv_gravity"_id ) );
			if ( std::isfinite( gravity ) && gravity > 0.0f && gravity <= 2000.0f )
			{
				this->m_aim_relative_acceleration.z = target_airborne == local_airborne
					? 0.0f : ( target_airborne ? -gravity : gravity );
			}

			const auto lead = std::clamp(
				dt + this->m_aim_tracking_lag,
				0.0f, std::clamp( cfg.prediction.max_horizon_ms, 0.0f, 120.0f ) * 0.001f );
			aim_point += relative_velocity * lead +
				( cfg.prediction.acceleration && this->m_aim_velocity_samples >= 3
					? this->m_aim_relative_acceleration * ( 0.5f * lead * lead )
					: foundation::vec3{} );

			desired = target_angle( aim_point );
			delta_x = desired.x - control_angles.x;
			delta_y = foundation::wrap_yaw( desired.y - control_angles.y );
			dist = std::sqrt( delta_x * delta_x + delta_y * delta_y );
		}
		else
		{
			this->m_aim_tracking_lag = 0.0f;
			this->m_aim_velocity_valid = false;
			this->m_aim_velocity_samples = 0;
		}
		// Prediction is allowed to refine the selected solution, but it may not pull
		// the input outside the FOV policy that selected this target.
		if ( this->get_fov( view_angles, eye_pos, aim_point )
			> selection_fov( cfg, eye_pos, aim_point ) )
		{
			return;
		}
		if ( h > 0.0f )
		{
			if ( dist < h * std::clamp(
				cfg.humanizer.deadzone, 0.0f, 2.0f ) ) return;

			this->m_aim_wander_phase += this->m_aim_wander_freq * dt;
			const auto settle = std::clamp( ( dist - 0.20f ) / 0.90f, 0.0f, 1.0f );
			const auto wander_amp = h * std::clamp(
				cfg.humanizer.wind, 0.0f, 20.0f ) * 0.004f
				* std::min( dist, 2.0f ) * settle * settle;
			const auto micro_amp = h * std::clamp(
				cfg.humanizer.jitter, 0.0f, 3.0f ) * 0.0025f
				* settle * settle;
			delta_x += wander_amp * std::sin( this->m_aim_wander_phase )
				+ micro_amp * std::sin( this->m_aim_wander_phase * 1.73f + 0.7f );
			delta_y += wander_amp * 0.7f * std::cos(
				this->m_aim_wander_phase * 1.3f )
				+ micro_amp * std::cos( this->m_aim_wander_phase * 1.47f );
		}

		delta_x *= response_alpha;
		delta_y *= response_alpha;
		const auto max_step = std::clamp( cfg.humanizer.max_step, 1.0f, 90.0f );
		const auto sampled_max_step = max_step * command_fraction;
		const auto step_length = std::sqrt( delta_x * delta_x + delta_y * delta_y );
		if ( h > 0.0f && step_length > sampled_max_step )
		{
			delta_x *= sampled_max_step / step_length;
			delta_y *= sampled_max_step / step_length;
		}

		if ( h > 0.0f )
		{
			// Пролёт мимо цели с коррекцией (иногда, на быстрых доводках).
			delta_x *= this->m_aim_overshoot;
			delta_y *= this->m_aim_overshoot;
			if ( this->m_aim_overshoot > 1.0f && dist < 1.0f )
			{
				this->m_aim_overshoot = std::max( 1.0f, this->m_aim_overshoot - dt * 2.0f );
			}

			// Дуга: путь к цели не прямая — шаг подворачивается вбок,
			// максимум в середине пути, ноль в начале и в конце.
			if ( this->m_aim_initial_dist > 0.5f )
			{
				const auto progress = 1.0f - std::clamp( dist / this->m_aim_initial_dist, 0.0f, 1.0f );
				const auto arc = this->m_aim_curve * std::sin( progress * std::numbers::pi_v<float> );
				const auto base_x = delta_x;
				const auto base_y = delta_y;
				const auto curved_x = base_x - base_y * arc;
				const auto curved_y = base_y + base_x * arc;
				const auto bounded_component = []( const float direct,
					const float curved )
				{

					return std::clamp( curved,
						std::min( direct, 0.0f ), std::max( direct, 0.0f ) );
				};
				delta_x = bounded_component( base_x, curved_x );
				delta_y = bounded_component( base_y, curved_y );
			}
		}

		auto move_x = -delta_y / deg_per_pixel;
		auto move_y = delta_x / deg_per_pixel;

		const auto accumulated_x = ( snap ? 0.0f : this->m_aim_error.x ) + move_x;
		const auto accumulated_y = ( snap ? 0.0f : this->m_aim_error.y ) + move_y;
		const auto dx = snap ? static_cast<int>( std::lround( accumulated_x ) )
			: static_cast<int>( accumulated_x );
		const auto dy = snap ? static_cast<int>( std::lround( accumulated_y ) )
			: static_cast<int>( accumulated_y );
		const foundation::vec2 remainder{
			snap ? 0.0f : accumulated_x - static_cast<float>( dx ),
			snap ? 0.0f : accumulated_y - static_cast<float>( dy ) };
		if ( dx == 0 && dy == 0
			&& std::abs( this->m_rcs_pending_mouse.x ) < 1.0f
			&& std::abs( this->m_rcs_pending_mouse.y ) < 1.0f )
		{
			this->m_aim_error = remainder;
			return;
		}

		if ( dx != 0 || dy != 0
			|| std::abs( this->m_rcs_pending_mouse.x ) >= 1.0f
			|| std::abs( this->m_rcs_pending_mouse.y ) >= 1.0f )
		{

			if ( !game::collision().valid( )
				|| !config::combat_profile::activation_active(
				cfg.activation_mode, cfg.key )
				|| local_checks_blocked( cfg.checks )
				|| live_target_invulnerable( tgt.player ? tgt.player->pawn : 0,
					tgt.player ? tgt.player->invulnerable : true )
				|| ( cfg.checks.smoke
					&& line_through_smoke( eye_pos, aim_point ) ) )
			{
				return;
			}
			const auto live_trace = game::collision().trace_ray( eye_pos, aim_point );

			const auto live_visible = !live_trace.hit;
			if ( !live_visible )
			{
				if ( cfg.checks.walls == config::combat_profile::wall_policy::block )
					return;
				if ( cfg.checks.walls == config::combat_profile::wall_policy::penetration )
				{
					ballistics_t::penetration::result penetration{};
					if ( !simulation::ballistics().pen().run(
						eye_pos, aim_point, *tgt.player, freshest, penetration )
						|| penetration.damage < cfg.min_damage )
						return;
				}
			}
			const auto rcs_limit = std::max( this->m_rcs_input_step, 1 );
			const auto rcs_dx = std::clamp(
				static_cast<int>( this->m_rcs_pending_mouse.x ), -rcs_limit, rcs_limit );
			const auto rcs_dy = std::clamp(
				static_cast<int>( this->m_rcs_pending_mouse.y ), -rcs_limit, rcs_limit );
			if ( app::context().input.pointer(
				dx + rcs_dx, dy + rcs_dy,
				platform::windows::pointer_action::relative_move ) )
			{
				auto submitted_raw = command_angles_valid ? raw_angles : view_angles;
				submitted_raw.x += static_cast<float>( dy + rcs_dy ) * deg_per_pixel;
				submitted_raw.y = foundation::wrap_yaw( submitted_raw.y
					- static_cast<float>( dx + rcs_dx ) * deg_per_pixel );
				publish_aim_command_prediction(
					input_sequence, submitted_raw, now );
				this->m_aim_error = remainder;
				this->m_aim_virtual_angles.x += static_cast<float>( dy ) * deg_per_pixel;
				this->m_aim_virtual_angles.y = foundation::wrap_yaw(
					this->m_aim_virtual_angles.y - static_cast<float>( dx ) * deg_per_pixel );
				this->m_aim_last_input_sequence = input_sequence;
				this->m_aim_last_input_view = view_angles;
				this->m_aim_last_input_time = now;
				this->m_rcs_last_input_sequence = input_sequence;
				this->m_rcs_last_input_time = now;
				this->m_rcs_pending_mouse.x -= static_cast<float>( rcs_dx );
				this->m_rcs_pending_mouse.y -= static_cast<float>( rcs_dy );
			}
		}
	}

	controller_t::trigger_result controller_t::trace_direction( const foundation::vec3& eye_pos, const foundation::vec3& direction, const std::vector<game::player_snapshot>& players, const config::combat_profile::triggerbot& cfg ) const
	{
		constexpr auto max_range{ 8192.0f };
		// Trigger prediction follows the sampled target state only. Network ping is not
		// an aim-ahead horizon; server lag compensation consumes the historical sample.
		const auto prediction_time = cfg.predictive
			? std::clamp( static_cast<float>( cfg.delay ) * 0.001f
				+ game::rules::simulation_step, 0.0f, 0.12f ) : 0.0f;
		auto local_velocity = foundation::vec3{};
		if ( cfg.predictive )
		{
			const auto pawn = game::local_player( ).pawn( );
			if ( pawn )
			{
				local_velocity = app::context( ).process.load<foundation::vec3>(
					pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
				if ( !std::isfinite( local_velocity.x )
					|| !std::isfinite( local_velocity.y )
					|| !std::isfinite( local_velocity.z ) )
				{
					local_velocity = {};
				}
			}
		}


		struct direct_candidate
		{
			const game::player_snapshot* player{};
			game::skeleton_reader::data bones{};
			float distance{ max_range };
			int hitbox{ -1 };
			int hitgroup{ -1 };
		} best{};

		for ( const auto& player : players )
		{
			if ( !game::local_player().is_enemy( player.team ) )
			{
				continue;
			}

			if ( player.invulnerable || player.hitboxes.count <= 0 )
			{
				continue;
			}

			const auto simulation_tick_offset =
				SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id );
			const auto tick_before = app::context( ).process.load<std::int32_t>(
				player.pawn + simulation_tick_offset );
			auto bones = game::skeletons( ).get( player.bone_cache );
			const auto tick_after = app::context( ).process.load<std::int32_t>(
				player.pawn + simulation_tick_offset );
			if ( tick_before != tick_after || !bones.is_valid( ) ) bones = player.bones;
			if ( !bones.is_valid( ) )
			{
				continue;
			}

			if ( cfg.predictive )
			{
				auto target_velocity = app::context( ).process.load<foundation::vec3>(
					player.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
				if ( !std::isfinite( target_velocity.x )
					|| !std::isfinite( target_velocity.y )
					|| !std::isfinite( target_velocity.z ) )
				{
					target_velocity = player.velocity;
				}
				const auto offset =
					( target_velocity - local_velocity ) * prediction_time;
				for ( auto& bone : bones.bones )
				{
					bone.position += offset;
				}
			}

			for ( const auto& hitbox : player.hitboxes )
			{
				const auto hitgroup = game::hitbox_data( ).hitgroup_from_hitbox(
					hitbox.index );
				if ( hitbox.index < 0 || hitbox.bone < 0 || hitbox.bone >= 128 )
				{
					continue;
				}
				const auto& bone = bones.bones[ hitbox.bone ];
				const auto rotation_length = bone.rotation.x * bone.rotation.x
					+ bone.rotation.y * bone.rotation.y
					+ bone.rotation.z * bone.rotation.z
					+ bone.rotation.w * bone.rotation.w;
				if ( !std::isfinite( bone.position.x )
					|| !std::isfinite( bone.position.y )
					|| !std::isfinite( bone.position.z )
					|| !std::isfinite( rotation_length )
					|| rotation_length < 0.5f || rotation_length > 1.5f
					|| bone.position.distance_sqr( player.origin ) > 512.0f * 512.0f )
				{
					continue;
				}
				const auto start = bone.position + bone.rotation.apply( hitbox.mins );
				const auto end = bone.position + bone.rotation.apply( hitbox.maxs );
				float distance{};
				if ( foundation::ray_capsule_entry( eye_pos, direction, start, end,
					hitbox.radius, distance )
					&& distance < best.distance )
				{
					best = { &player, bones, distance, hitbox.index, hitgroup };
				}
			}
		}

		if ( !best.player || best.hitbox < 0 || best.distance >= max_range ) return {};

		trigger_result result{};
		result.player = best.player;
		result.bones = best.bones;
		result.hitbox = best.hitbox;
		result.hitgroup = best.hitgroup;
		result.point = eye_pos + direction * best.distance;
        ballistics_t::penetration::result passage{};
        if (!simulation::ballistics().pen().run_seed(eye_pos, direction, *best.player,
            best.bones, cfg.hitbox_parts,
            cfg.checks.walls == config::combat_profile::wall_policy::penetration,
            1.0f, best.hitbox, passage)) return {};
        result.damage = passage.damage;
        result.penetrated = passage.penetrated;

		if ( cfg.checks.smoke && line_through_smoke( eye_pos, result.point ) )
			return {};
		return result;
	}

	void controller_t::triggerbot( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, const config::combat_profile::triggerbot& cfg )
	{
		auto now = std::chrono::steady_clock::now( );
		const auto cancel_scheduled = [ this ]
		{
			this->m_trigger_shot_scheduled = false;
			this->m_trigger_fire_time = {};
			this->m_trigger_target_valid = false;
			features::misc::auto_stop( ).cancel_request(
				features::misc::auto_stop_source::advanced_trigger );
		};

		if ( !config::combat_profile::activation_active(
			cfg.activation_mode, cfg.key ) )
		{
			cancel_scheduled( );
			return;
		}

		const auto& ctx = ballistics().ctx( );

		if ( this->m_trigger_held )
		{
			VESTA_SEED_COUNT( held_skip );
			return;
		}
		if ( now < this->m_trigger_cooldown_until )
		{
			cancel_scheduled( );
			return;
		}
		constexpr std::uint16_t revolver_id{ 64 };
		const auto revolver = ctx.item_def_idx == revolver_id;
		if ( ( !ctx.weapon_ready && !revolver )
			|| ctx.is_reloading || ctx.clip == 0 )
		{
			cancel_scheduled( );
			return;
		}

		const auto pawn = game::local_player().pawn( );
		if ( !this->m_trigger_shot_scheduled )
		{
			if ( local_checks_blocked( cfg.checks ) )
			{
				features::visuals::event_log( ).push_throttled(
					0x54524301u, "Trigger blocked: legit check",
					features::visuals::event_kind::blocked,
					std::chrono::milliseconds( 750 ) );
				return;
			}

			// CViewRender already contains the final rendered camera orientation. Adding
			// AimPunch here a second time makes the miss grow with target distance.
			const auto trace_angles = view_angles;
			foundation::vec3 forward{};
			trace_angles.to_directions( &forward, nullptr, nullptr );
			const auto result = this->trace_direction(
				eye_pos, forward, players, cfg );
			if ( !result.player
				|| ( result.penetrated && result.damage < cfg.min_damage ) )
			{
				return;
			}

			constexpr auto head_hitgroup = 1;
			if ( cfg.lethal_only && result.hitgroup != head_hitgroup
				&& result.damage + 0.001f < static_cast<float>( result.player->health ) )
			{
				return;
			}

			now = std::chrono::steady_clock::now( );
			this->m_trigger_shot_scheduled = true;
			const auto jitter = cfg.randomize_ms > 0
				? static_cast<int>( std::round( this->m_rng.uniform(
					-static_cast<float>( cfg.randomize_ms ),
					static_cast<float>( cfg.randomize_ms ) ) ) ) : 0;
			const auto outlier = this->m_rng.uniform( 0.0f, 100.0f )
				< std::clamp( cfg.outlier_chance, 0.0f, 100.0f )
				? std::max( cfg.outlier_delay_ms, 0 ) : 0;
			const auto committed_delay = std::max( 0, cfg.delay + jitter + outlier );
			this->m_trigger_fire_time = now
				+ std::chrono::milliseconds( committed_delay );
			this->m_trigger_target = *result.player;
			this->m_trigger_target_bones = result.bones;
			this->m_trigger_target_eye = eye_pos;
			this->m_trigger_target_angles = trace_angles;
			this->m_trigger_target_parts = cfg.hitbox_parts;
			this->m_trigger_target_valid = true;
			const auto weapon_max_speed = ctx.fire_mode
				? ctx.debug.max_speed.second : ctx.debug.max_speed.first;
			const auto required_speed = std::max( 0.5f,
				( std::isfinite( weapon_max_speed ) && weapon_max_speed > 0.0f
					? weapon_max_speed : 250.0f )
				* config::general_settings.m_auto_stop.required_shoot_speed * 0.01f );
			features::misc::auto_stop( ).request_stop(
				features::misc::auto_stop_source::advanced_trigger,
				this->m_trigger_fire_time, required_speed );
		}

		if ( now < this->m_trigger_fire_time ) return;

		if ( !features::misc::auto_stop( ).ready_to_fire(
			features::misc::auto_stop_source::advanced_trigger ) ) return;

		if ( cfg.hitchance > 0.0f && this->m_trigger_target_valid
			&& ballistics().estimate_hit_probability(
				this->m_trigger_target_eye, this->m_trigger_target_angles,
				this->m_trigger_target, this->m_trigger_target_bones,
				this->m_trigger_target_parts ) < cfg.hitchance / 100.0f )
		{
			features::visuals::event_log( ).push_throttled(
				0x54524302u, "Trigger waiting: hitchance",
				features::visuals::event_kind::blocked,
				std::chrono::milliseconds( 750 ) );
			return;
		}

		// Once committed, do not consult the target again. A failed syscall is not a
		// shot: retain the schedule and retry on the next 250 Hz pass.
		const auto shots_before = pawn
			? app::context( ).process.load<std::int32_t>( pawn
				+ SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) ) : -1;
		const auto press_delivered = this->m_revolver_pre_cock_down
			|| app::context().input.pointer(
				0, 0, platform::windows::pointer_action::primary_down );
		if ( !press_delivered )
		{
			return;
		}
		this->m_revolver_pre_cock_down = false;

		features::misc::auto_stop( ).notify_shot(
			features::misc::auto_stop_source::advanced_trigger );
		features::visuals::event_log( ).begin_trigger_shot(
			revolver ? "R8 Trigger" : "Trigger", false );
		this->m_trigger_held = true;
		this->m_seed_press_active = false;
		this->m_revolver_committed = revolver;
		this->m_trigger_press_shots = shots_before;

		const auto hold_ms = std::clamp(
			static_cast<int>( std::ceil(
				2.0f * game::rules::simulation_step * 1000.0f ) ) + 4,
			32, 48 );
		this->m_trigger_release_time = now + std::chrono::milliseconds(
			revolver ? 1200 : hold_ms );
		this->m_trigger_cooldown_until = now
			+ std::chrono::milliseconds( std::max( cfg.delay_after_ms, 0 ) );

		this->m_trigger_shot_scheduled = false;
		this->m_trigger_fire_time = {};
		this->m_trigger_target_valid = false;
	}

#if 0 // Replaced by features::misc::auto_stop_t; kept out of the binary during migration.
	void controller_t::engage_counter_movement( )
	{
		std::scoped_lock movement_lock( g_counter_movement.mutex );
		const auto clear_locked = [ & ]
		{

			std::vector<std::uint16_t> releases = g_counter_movement.gated_keys;
			for ( const auto key : g_counter_movement.keys )
			{
				if ( std::ranges::find( releases, key ) == releases.end( ) )
					releases.push_back( key );
			}
			for ( const auto key : releases ) app::context().input.key( key, false );
			g_counter_movement.keys.clear( );
			app::context().input.set_movement_gate( {}, false );
			g_counter_movement.gated_keys.clear( );
			g_counter_movement.state = shared_counter_movement::phase::idle;
			g_counter_movement.started = {};
		};
		const auto pawn = game::local_player().pawn( );
		if ( !pawn )
		{
			clear_locked( );
			return;
		}
		const auto grounded = ( app::context().process.load<std::uint32_t>(
			pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) ) & 1u ) != 0;
		const auto velocity = app::context().process.load<foundation::vec3>(
			pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		if ( !grounded )
		{
			clear_locked( );
			return;
		}

		auto view = game::camera().angles( );
		foundation::vec3 forward{}, right{};
		view.to_directions( &forward, &right, nullptr );
		forward.z = 0.0f;
		right.z = 0.0f;
		forward.normalize( );
		right.normalize( );

		auto& bindings = game::input_bindings( );
		const auto forward_key = bindings.resolve( game::input_action::forward );
		const auto back_key = bindings.resolve( game::input_action::back );
		const auto left_key = bindings.resolve( game::input_action::left );
		const auto right_key = bindings.resolve( game::input_action::right );
		std::vector<std::uint16_t> movement_keys{};
		for ( const auto& binding : { forward_key, back_key, left_key, right_key } )
		{
			if ( binding && binding.virtual_key
				&& std::ranges::find( movement_keys, binding.virtual_key )
					== movement_keys.end( ) )
				movement_keys.push_back( binding.virtual_key );
		}
		struct movement_action { int forward_axis; int side_axis; };
		static constexpr std::array actions{
			movement_action{ 0, 0 }, movement_action{ 1, 0 },
			movement_action{ -1, 0 }, movement_action{ 0, -1 },
			movement_action{ 0, 1 }, movement_action{ 1, -1 },
			movement_action{ 1, 1 }, movement_action{ -1, -1 },
			movement_action{ -1, 1 } };

		const auto movement_services = app::context().process.load<std::uintptr_t>(
			pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_id ) );
		const auto surface_friction = movement_services
			? app::context().process.load<float>( movement_services
				+ SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_id ) )
			: 1.0f;
		const auto movement_max_speed = movement_services
			? app::context().process.load<float>( movement_services
				+ SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_id ) )
			: 250.0f;
		const auto pawn_friction = app::context().process.load<float>(
			pawn + SCHEMA( "C_BaseEntity", "m_flFriction"_id ) );
		const auto friction = std::max(
			game::variables().get<float>( CONVAR( "sv_friction"_id ) ), 0.1f );
		const auto stop_speed = std::max(
			game::variables().get<float>( CONVAR( "sv_stopspeed"_id ) ), 1.0f );
		const auto accelerate = std::max(
			game::variables().get<float>( CONVAR( "sv_accelerate"_id ) ), 0.1f );
		const auto ground_grip = std::clamp(
			std::isfinite( surface_friction ) ? surface_friction : 1.0f,
			0.05f, 4.0f );
		const auto entity_friction = std::clamp(
			std::isfinite( pawn_friction ) ? pawn_friction : 1.0f,
			0.05f, 4.0f );
		const auto max_speed = std::clamp(
			std::isfinite( movement_max_speed ) ? movement_max_speed : 250.0f,
			1.0f, 1000.0f );
		const auto ctx = ballistics().ctx( );
		const auto accuracy_speed = std::max(
			ctx.debug.max_speed.first, ctx.debug.max_speed.second ) * 0.34f;
		const auto threshold = std::max( accuracy_speed, 5.0f );
		const auto step = game::rules::simulation_step;

		const auto simulate_tick = [ & ]( foundation::vec3 value,
			const movement_action action )
		{
			value.z = 0.0f;
			const auto speed = value.length_2d( );
			if ( speed > 0.1f )
			{
				const auto control = std::max( speed, stop_speed );
				const auto adjusted = std::max( 0.0f, speed
					- control * friction * ground_grip * entity_friction * step );
				if ( adjusted < speed ) value *= adjusted / speed;
			}
			if ( action.forward_axis || action.side_axis )
			{
				auto wish = forward * static_cast<float>( action.forward_axis )
					+ right * static_cast<float>( action.side_axis );
				const auto length = wish.length_2d( );
				if ( length > 0.0001f ) wish *= 1.0f / length;
				const auto current = value.dot( wish );
				const auto available = std::max( 0.0f, max_speed - current );
				const auto gain = std::min( available,
					accelerate * step * max_speed * ground_grip );
				value += wish * gain;
			}
			return value;
		};

		struct plan_node
		{
			foundation::vec3 velocity{};
			float cost{};
			int first_action{};
			int last_action{};
		};
		std::vector<plan_node> beam{ { velocity, 0.0f, 0, 0 } };
		const auto estimated_ticks = static_cast<int>( std::ceil(
			estimated_counter_movement_seconds( ) / std::max( step, 0.001f ) ) );
		const auto horizon = std::clamp( estimated_ticks + 2, 3, 8 );
		const auto initial_direction = velocity.length_2d( ) > 0.1f
			? velocity.normalized( ) : foundation::vec3{};
		for ( int depth = 0; depth < horizon; ++depth )
		{
			std::vector<plan_node> expanded{};
			expanded.reserve( beam.size( ) * actions.size( ) );
			for ( const auto& node : beam )
			{
				for ( int action_index = 0;
					action_index < static_cast<int>( actions.size( ) ); ++action_index )
				{
					auto next = simulate_tick( node.velocity, actions[ action_index ] );
					const auto speed = next.length_2d( );
					const auto reverse_speed = std::max(
						0.0f, -next.dot( initial_direction ) );
					auto cost = node.cost + speed * 0.05f
						+ reverse_speed * 4.0f;
					if ( action_index != node.last_action ) cost += 0.15f;
					if ( depth + 1 == horizon && speed > threshold )
						cost += ( speed - threshold ) * 25.0f;
					expanded.push_back( { next, cost,
						depth == 0 ? action_index : node.first_action,
						action_index } );
				}
			}
			std::ranges::sort( expanded, {}, &plan_node::cost );
			if ( expanded.size( ) > 24 ) expanded.resize( 24 );
			beam = std::move( expanded );
		}
		const auto selected = beam.empty( ) ? 0 : beam.front( ).first_action;
		const auto action = actions[ selected ];
		std::vector<std::uint16_t> desired{};
		const auto add_desired = [ & ]( const game::input_binding& binding )
		{
			if ( binding && binding.virtual_key
				&& std::ranges::find( desired, binding.virtual_key ) == desired.end( ) )
				desired.push_back( binding.virtual_key );
		};
		if ( action.forward_axis > 0 ) add_desired( forward_key );
		else if ( action.forward_axis < 0 ) add_desired( back_key );
		if ( action.side_axis > 0 ) add_desired( right_key );
		else if ( action.side_axis < 0 ) add_desired( left_key );

		app::context().input.set_movement_gate( movement_keys, true );
		g_counter_movement.gated_keys = movement_keys;
		if ( !app::context().input.key_gate_ready( ) ) return;

		for ( const auto key : g_counter_movement.keys )
		{
			if ( std::ranges::find( desired, key ) == desired.end( ) )
				app::context().input.key( key, false );
		}
		for ( const auto key : desired )
		{
			if ( std::ranges::find( g_counter_movement.keys, key )
				== g_counter_movement.keys.end( ) )
				app::context().input.key( key, true );
		}
		g_counter_movement.keys = std::move( desired );
		g_counter_movement.state = shared_counter_movement::phase::braking;
		if ( g_counter_movement.started.time_since_epoch( ).count( ) == 0 )
			g_counter_movement.started = std::chrono::steady_clock::now( );
	}

	void controller_t::release_counter_movement( bool force )
	{
		std::scoped_lock movement_lock( g_counter_movement.mutex );
		if ( g_counter_movement.state == shared_counter_movement::phase::idle
			&& g_counter_movement.gated_keys.empty( ) )
			return;

		if ( !force )
		{
			const auto pawn = game::local_player().pawn( );
			const auto velocity = pawn
				? app::context().process.load<foundation::vec3>(
					pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) )
				: foundation::vec3{};
			const auto age = std::chrono::duration<float>(
				std::chrono::steady_clock::now( ) - g_counter_movement.started ).count( );
			const auto ctx = ballistics().ctx( );
			const auto accuracy_speed = std::max(
				ctx.debug.max_speed.first, ctx.debug.max_speed.second ) * 0.34f;
			if ( pawn && velocity.length_2d( ) > std::max( accuracy_speed, 5.0f )
				&& age < 0.15f )
				return;
		}

		std::vector<std::uint16_t> releases = g_counter_movement.gated_keys;
		for ( const auto key : g_counter_movement.keys )
		{
			if ( std::ranges::find( releases, key ) == releases.end( ) )
				releases.push_back( key );
		}

		for ( const auto key : releases ) app::context().input.key( key, false );
		g_counter_movement.keys.clear( );
		g_counter_movement.state = shared_counter_movement::phase::release;
		app::context().input.set_movement_gate( {}, false );
		g_counter_movement.gated_keys.clear( );
		g_counter_movement.state = shared_counter_movement::phase::idle;
		g_counter_movement.started = {};
	}
#endif

void grenade_aim_t::tick( )
{
	const auto& ctx = ballistics().ctx( );
	const auto cfg = config::combat_settings.global.grenade_aim;

	if ( !cfg.enabled || !( GetAsyncKeyState( cfg.key ) & 0x8000 ) )
	{
		this->m_is_active = false;
		this->m_aim_error = {};
		this->m_current_target = {};
		return;
	}

	if ( !ctx.valid || !ctx.weapon || !game::collision().valid( )
		|| ctx.weapon_type != game::rules::equipment_class::throwable )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	const auto pin_pulled = app::context().process.load<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
	const auto throw_time = app::context().process.load<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_id ) );

	if ( !pin_pulled || throw_time > 0.0f )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	const auto kind = this->resolve_grenade_kind( ctx.weapon_vdata );
	if ( kind == grenade_kind::unknown )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	float throw_vel_vdata{}, throw_strength{};
	if ( !app::context().process.copy( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_id ),
			&throw_vel_vdata, sizeof( throw_vel_vdata ) )
		|| !app::context().process.copy( ctx.weapon
			+ SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_id ),
			&throw_strength, sizeof( throw_strength ) )
		|| !std::isfinite( throw_vel_vdata ) || throw_vel_vdata <= 0.0f
		|| !std::isfinite( throw_strength ) || throw_strength < 0.0f
		|| throw_strength > 1.0f )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	if ( std::fabsf( throw_strength - 0.5f ) <= 0.1f ) throw_strength = 0.5f;

	foundation::vec3 eye_pos{}, view_angles{};
	if ( !game::camera().sample( eye_pos, view_angles ) )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	const auto players = game::world().players( );
	if ( !players )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	const auto previous_pawn = this->m_current_target.pawn;
	this->find_target( eye_pos, view_angles, *players, static_cast<float>( cfg.fov ) );
	if ( !this->m_current_target.pawn )
	{
		this->m_is_active = false;
		return;
	}

	this->m_is_active = true;
	const auto now = std::chrono::steady_clock::now( );
	const auto state_changed = previous_pawn != this->m_current_target.pawn ||
		ctx.weapon != this->m_last_weapon || kind != this->m_last_kind;
	if ( state_changed || now - this->m_last_calculation >= std::chrono::milliseconds( 24 ) )
	{
		this->m_last_calculation = now;
		this->m_last_weapon = ctx.weapon;
		this->m_last_kind = kind;
		this->calculate_trajectory( eye_pos, throw_vel_vdata, throw_strength, kind );
	}

	if ( this->m_current_target.found_trajectory )
	{
		this->smooth_aim( eye_pos, view_angles, cfg );
	}
}

void grenade_aim_t::find_target( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, float max_fov )
{
	const auto previous = this->m_current_target;
	target_info selected{};
	float best_fov = max_fov;

	for ( const auto& player : players )
	{
		if ( !game::local_player().is_enemy( player.team ) || player.health <= 0 || player.invulnerable )
		{
			continue;
		}

		if ( !player.bones.is_valid( ) ) continue;
		const auto head = player.bones.get_position( game::rules::joint_id::head );
		const auto fov = foundation::angular_distance( view_angles, eye_pos, head );
		if ( fov < best_fov )
		{
			best_fov = fov;
			selected.player = player;
			selected.pawn = player.pawn;
			selected.head_pos = head;
		}
	}

	if ( selected.pawn && selected.pawn == previous.pawn )
	{
		selected.optimal_angles = previous.optimal_angles;
		selected.predicted_pos = previous.predicted_pos;
		selected.score = previous.score;
		selected.found_trajectory = previous.found_trajectory;
	}
	this->m_current_target = std::move( selected );
}

grenade_aim_t::grenade_kind grenade_aim_t::resolve_grenade_kind( std::uintptr_t weapon_vdata ) const
{
	if ( !weapon_vdata ) return grenade_kind::unknown;
	const auto name_ptr = app::context().process.load<std::uintptr_t>(
		weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
	if ( !name_ptr ) return grenade_kind::unknown;
	char name[ 64 ]{};
	if ( !app::context().process.copy( name_ptr, name, sizeof( name ) - 1 ) ) return grenade_kind::unknown;
	switch ( identity::of( name ) )
	{
	case "weapon_hegrenade"_id:    return grenade_kind::he;
	case "weapon_flashbang"_id:    return grenade_kind::flash;
	case "weapon_smokegrenade"_id: return grenade_kind::smoke;
	case "weapon_molotov"_id:
	case "weapon_incgrenade"_id:   return grenade_kind::molotov;
	case "weapon_decoy"_id:        return grenade_kind::decoy;
	default:                           return grenade_kind::unknown;
	}
}

grenade_aim_t::trajectory_result grenade_aim_t::simulate_fast( const foundation::vec3& start,
	const foundation::vec3& view_angles, const foundation::vec3& player_vel, float throw_vel_vdata,
	float throw_strength, float sv_gravity, float molotov_floor_normal, grenade_kind kind,
	const foundation::vec3& target_head ) const
{
	trajectory_result result{};
	auto angles = view_angles;
	if ( angles.x > 90.0f ) angles.x -= 360.0f;
	else if ( angles.x < -90.0f ) angles.x += 360.0f;
	angles.x -= ( 90.0f - std::fabsf( angles.x ) ) * 10.0f / 90.0f;

	foundation::vec3 forward{};
	angles.to_directions( &forward, nullptr, nullptr );

	auto pos = start;
	pos.z += throw_strength * 12.0f - 12.0f;
	const auto start_trace = game::collision().sweep_hull(
		pos, pos + forward * 22.0f, simulation::grenade_collision_half_extents );
	pos = start_trace.hit ? start_trace.end_pos - forward * 6.0f : pos + forward * 16.0f;

	const auto throw_vel = std::clamp( throw_vel_vdata * 0.9f, 15.0f, 750.0f );
	const auto throw_speed = ( throw_strength * 0.7f + 0.3f ) * throw_vel;
	auto vel = forward * throw_speed + player_vel * 1.25f;
	const auto gravity = sv_gravity * 0.4f;
	auto closest_head_sqr = pos.distance_sqr( target_head );

	for ( int tick = 0; tick < 256; ++tick )
	{
		const auto new_z = vel.z - gravity * game::rules::simulation_step;
		const foundation::vec3 move{ vel.x * game::rules::simulation_step, vel.y * game::rules::simulation_step,
			( vel.z + new_z ) * 0.5f * game::rules::simulation_step };
		vel.z = new_z;

		const auto trace = game::collision().sweep_hull(
			pos, pos + move, simulation::grenade_collision_half_extents );
		pos = trace.end_pos;
		closest_head_sqr = std::min( closest_head_sqr, pos.distance_sqr( target_head ) );

		if ( trace.hit )
		{
			++result.bounces;
			if ( kind == grenade_kind::molotov && trace.normal.z >= molotov_floor_normal )
			{
				result.valid = true;
				result.end_pos = pos;
				result.duration = static_cast<float>( tick + 1 ) * game::rules::simulation_step;
				result.closest_head_distance = std::sqrt( closest_head_sqr );
				return result;
			}

			auto reflected = ( vel - trace.normal * ( vel.dot( trace.normal ) * 2.0f ) ) * 0.45f;
			if ( trace.normal.z > 0.7f )
			{
				const auto speed_sqr = reflected.length_sqr( );
				if ( speed_sqr > 96000.0f )
				{
					const auto incidence = reflected.normalized( ).dot( trace.normal );
					if ( incidence > 0.5f ) reflected *= 1.5f - incidence;
				}
				if ( speed_sqr < 400.0f ) reflected = {};
			}
			vel = reflected;
			const auto remaining = 1.0f - trace.fraction;
			if ( remaining > 0.0f && vel.length_sqr( ) > 0.0f )
			{
				const auto post_origin = pos + trace.normal * ( 1.0f / 32.0f );
				const auto post_trace = game::collision().sweep_hull(
					post_origin, post_origin + vel * ( remaining * game::rules::simulation_step ),
					simulation::grenade_collision_half_extents );
				pos = post_trace.end_pos;
			}
		}

		const auto elapsed = static_cast<float>( tick ) * game::rules::simulation_step;
		const auto stopped = vel.length_sqr( ) < 400.0f;
		const auto timed_detonation = ( kind == grenade_kind::he || kind == grenade_kind::flash )
			? static_cast<float>( tick - 8 ) * game::rules::simulation_step > 1.5f
			: kind == grenade_kind::molotov && elapsed > 2.0f;
		const auto resting_detonation = ( kind == grenade_kind::smoke || kind == grenade_kind::decoy ) && stopped;
		if ( timed_detonation || resting_detonation || result.bounces > 20 )
		{
			result.valid = true;
			result.end_pos = pos;
			result.duration = static_cast<float>( tick + 1 ) * game::rules::simulation_step;
			result.closest_head_distance = std::sqrt( closest_head_sqr );
			return result;
		}
	}

	return result;
}

void grenade_aim_t::calculate_trajectory( const foundation::vec3& eye_pos, float throw_vel_vdata,
	float throw_strength, grenade_kind kind )
{
	this->m_current_target.found_trajectory = false;
	const auto local_pawn = game::local_player().pawn( );
	foundation::vec3 local_vel{}, target_vel{};
	if ( !local_pawn || !game::collision().valid( )
		|| !app::context().process.copy( local_pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&local_vel, sizeof( local_vel ) )
		|| !app::context().process.copy( this->m_current_target.pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&target_vel, sizeof( target_vel ) ) ) return;
	if ( !std::isfinite( local_vel.x ) || !std::isfinite( local_vel.y )
		|| !std::isfinite( local_vel.z ) || local_vel.length_sqr( ) > 4'000'000.0f ) return;
	if ( !std::isfinite( target_vel.x ) || !std::isfinite( target_vel.y ) ||
		!std::isfinite( target_vel.z ) || target_vel.length_sqr( ) > 4'000'000.0f ) return;
	this->m_current_target.velocity = target_vel;

	const auto sv_gravity = game::variables().get<float>( CONVAR( "sv_gravity"_id ) );
	const auto molotov_slope = game::variables().get<float>( CONVAR( "weapon_molotov_maxdetonateslope"_id ) );
	if ( !std::isfinite( sv_gravity ) || sv_gravity <= 0.0f || sv_gravity > 2000.0f
		|| !std::isfinite( molotov_slope ) || molotov_slope < 0.0f
		|| molotov_slope > 90.0f ) return;
	const auto molotov_floor_normal = std::cos( foundation::to_radians( molotov_slope ) );
	const auto gravity = std::clamp( sv_gravity, 0.0f, 2000.0f ) * 0.4f;

	auto target_base = this->m_current_target.player.origin;
	if ( kind == grenade_kind::he )
	{
		target_base = this->m_current_target.head_pos;
	}
	else if ( kind == grenade_kind::flash )
	{
		foundation::vec3 target_forward{};
		this->m_current_target.player.eye_angles.to_directions( &target_forward, nullptr, nullptr );
		const auto requested = this->m_current_target.head_pos + target_forward * 96.0f;
		const auto visibility = game::collision().trace_ray( this->m_current_target.head_pos, requested );
		target_base = visibility.hit ? visibility.end_pos - target_forward * 8.0f : requested;
	}

	struct seed
	{
		foundation::vec3 angles{};
		float speed_error{};
	};
	std::array<seed, 24> seeds{};
	std::size_t seed_count{};
	const auto throw_vel = std::clamp( throw_vel_vdata * 0.9f, 15.0f, 750.0f );
	const auto launch_speed = ( throw_strength * 0.7f + 0.3f ) * throw_vel;
	auto analytic_start = eye_pos;
	analytic_start.z += throw_strength * 12.0f - 12.0f;

	for ( int sample = 0; sample < 24; ++sample )
	{
		const auto flight_time = 0.35f + static_cast<float>( sample ) * 0.095f;
		const auto future_target = target_base + target_vel * flight_time;
		auto required = ( future_target - analytic_start - local_vel * ( 1.25f * flight_time ) +
			foundation::vec3{ 0.0f, 0.0f, 0.5f * gravity * flight_time * flight_time } ) / flight_time;
		const auto required_speed = required.length( );
		if ( required_speed < 1.0f ) continue;
		required /= required_speed;
		auto effective = foundation::look_at_angles( {}, required );
		const auto view_pitch = effective.x >= -10.0f
			? 0.9f * ( effective.x + 10.0f )
			: 1.125f * ( effective.x + 10.0f );
		if ( view_pitch < -89.0f || view_pitch > 89.0f ) continue;
		seeds[ seed_count++ ] = { { view_pitch, effective.y, 0.0f }, std::fabsf( required_speed - launch_speed ) };
	}
	std::sort( seeds.begin( ), seeds.begin( ) + seed_count,
		[]( const seed& lhs, const seed& rhs ) { return lhs.speed_error < rhs.speed_error; } );

	foundation::vec3 best_angles{};
	auto best_score = std::numeric_limits<float>::max( );
	auto best_endpoint_distance = std::numeric_limits<float>::max( );
	float best_duration{};
	const auto evaluate = [ & ]( const foundation::vec3& angles )
		{
			const auto head_at_arrival = this->m_current_target.head_pos + target_vel * 1.2f;
			const auto result = this->simulate_fast( eye_pos, angles, local_vel, throw_vel_vdata,
				throw_strength, sv_gravity, molotov_floor_normal, kind, head_at_arrival );
			if ( !result.valid ) return;
			const auto desired = target_base + target_vel * result.duration;
			const auto endpoint_distance = result.end_pos.distance( desired );
			auto score = endpoint_distance + static_cast<float>( result.bounces ) * 1.5f + result.duration * 0.5f;
			if ( kind == grenade_kind::smoke || kind == grenade_kind::molotov || kind == grenade_kind::decoy )
				score += result.closest_head_distance * 0.08f;
			if ( score >= best_score ) return;
			best_score = score;
			best_endpoint_distance = endpoint_distance;
			best_duration = result.duration;
			best_angles = angles;
		};

	for ( std::size_t i = 0; i < std::min<std::size_t>( seed_count, 8 ); ++i ) evaluate( seeds[ i ].angles );
	if ( best_score == std::numeric_limits<float>::max( ) )
		evaluate( foundation::look_at_angles( eye_pos, target_base ) );

	for ( const auto step : { 3.0f, 1.25f, 0.45f } )
	{
		const auto center = best_angles;
		for ( int pitch = -1; pitch <= 1; ++pitch )
			for ( int yaw = -1; yaw <= 1; ++yaw )
				if ( pitch || yaw ) evaluate( { std::clamp( center.x + pitch * step, -89.0f, 89.0f ),
					foundation::wrap_yaw( center.y + yaw * step ), 0.0f } );
	}

	const auto acceptance = kind == grenade_kind::molotov ? 110.0f :
		( kind == grenade_kind::smoke || kind == grenade_kind::decoy ? 130.0f : 150.0f );
	this->m_current_target.score = best_score;
	this->m_current_target.predicted_pos = target_base + target_vel * best_duration;
	this->m_current_target.optimal_angles = best_angles;
	this->m_current_target.found_trajectory = best_score < std::numeric_limits<float>::max( ) &&
		best_endpoint_distance <= acceptance;
}

void grenade_aim_t::smooth_aim( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::global_settings::grenade_aim_config& cfg )
{
	constexpr auto m_yaw{ 0.022f };
	const auto sensitivity = game::variables().get<float>( CONVAR( "sensitivity"_id ) );
	const auto fov_adjust = app::context().process.load<float>( game::local_player().pawn( ) + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id ) );
	const auto deg_per_pixel = sensitivity * m_yaw * fov_adjust;

	if ( deg_per_pixel <= 0.0f )
	{
		return;
	}

	auto delta_x = this->m_current_target.optimal_angles.x - view_angles.x;
	auto delta_y = foundation::wrap_yaw( this->m_current_target.optimal_angles.y - view_angles.y );

	if ( cfg.smoothing > 1 )
	{
		const auto factor = static_cast< float >( cfg.smoothing );
		delta_x /= factor;
		delta_y /= factor;
	}

	const auto move_x = -delta_y / deg_per_pixel;
	const auto move_y = delta_x / deg_per_pixel;

	this->m_aim_error.x += move_x;
	this->m_aim_error.y += move_y;

	const auto dx = static_cast< int >( this->m_aim_error.x );
	const auto dy = static_cast< int >( this->m_aim_error.y );

	this->m_aim_error.x -= static_cast< float >( dx );
	this->m_aim_error.y -= static_cast< float >( dy );

	if ( dx != 0 || dy != 0 )
	{
		app::context().input.pointer( dx, dy, platform::windows::pointer_action::relative_move );
	}
}

} // namespace features::legitbot
