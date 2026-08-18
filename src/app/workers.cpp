#include <stdafx.hpp>
#include <app/context.hpp>
#include <app/workers.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/misc.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/trigger/seed_trigger.hpp>
#include <features/visuals/visuals.hpp>

namespace app::workers {

	namespace {

		constexpr const char* k_step_names[ ]{
			"local.update", "auto_accept.tick", "entities.refresh", "collector.run",
			"map-name read", "parse dispatch", "bomb_damage.parse", "heartbeat", "sleep",
		};

		std::atomic<int> g_game_step{ -1 };
		std::atomic<std::uint64_t> g_game_iter{ 0 };

		std::atomic<std::shared_ptr<const std::string>> g_current_map{
			std::make_shared<const std::string>( ) };

		[[nodiscard]] bool shared_ballistics_requested( )
		{
			const auto key_down = []( const int key )
			{
				return key > 0 && ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0;
			};
			const auto& visuals = config::visual_settings;
			const auto& misc = config::general_settings;
			if ( misc.m_bullet_tracers.enabled || misc.m_hitmarker.enabled || misc.m_hitsound.enabled
				|| misc.m_hitsound.show_damage || misc.m_grenades.enabled
				|| ( visuals.m_crosshair.enabled
					&& ( visuals.m_crosshair.sync
						|| visuals.m_crosshair.penetration_enabled ) ) )
			{
				return true;
			}

			const auto config = config::combat_settings.get(
				game::local_player().weapon_type( ) );
			bool rcs_requested{};
			if ( config.aimbot.rcs.enabled )
			{
				const auto pawn = game::local_player().pawn( );
				const auto shots = pawn
					? app::context().process.load<std::int32_t>( pawn
						+ SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_id ) ) : 0;
				rcs_requested = key_down( VK_LBUTTON )
					|| shots >= std::max( config.aimbot.rcs.start_bullet, 1 );
			}
			if ( rcs_requested || config.other.penetration_crosshair
				|| ( config.aimbot.enabled && config::combat_profile::activation_active(
					config.aimbot.activation_mode, config.aimbot.key ) ) )
			{
				return true;
			}
			const auto independent_seed = config.triggerbot.seed_type
				!= config::combat_profile::seed_mode::none;
			if ( config.triggerbot.enabled && !independent_seed
				&& config::combat_profile::activation_active(
					config.triggerbot.activation_mode, config.triggerbot.key ) )
			{
				return true;
			}
			const auto& grenade =
				config::combat_settings.global.grenade_aim;
			return grenade.enabled && key_down( grenade.key );
		}

	}

	std::shared_ptr<const std::string> current_map( )
	{
		return g_current_map.load( std::memory_order_acquire );
	}

	namespace {

		std::string read_map_name( std::uintptr_t global_vars )
		{
			static std::uintptr_t cached_offset{ 0 };
			static auto last_probe = std::chrono::steady_clock::now( ) - std::chrono::seconds( 5 );

			const auto try_read = [ ]( std::uintptr_t addr ) -> std::string
			{
				const auto ptr = app::context().process.load<std::uintptr_t>( addr );
				if ( ptr < 0x10000 )
				{
					return {};
				}

				const auto value = app::context().process.load_text( ptr, 64 );
				if ( value.size( ) < 3 )
				{
					return {};
				}

				auto has_separator = false;
				for ( const char c : value )
				{
					const auto ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' )
						|| c == '_' || c == '-' || c == '.' || c == '/' || c == '\\';
					if ( !ok )
					{
						return {};
					}

					has_separator |= ( c == '_' || c == '/' );
				}

				return has_separator ? value : std::string{};
			};

			if ( cached_offset )
			{
				return try_read( global_vars + cached_offset );
			}

			const auto now = std::chrono::steady_clock::now( );
			if ( now - last_probe < std::chrono::seconds( 1 ) )
			{
				return {};
			}
			last_probe = now;

			for ( std::uintptr_t off = 0x150; off <= 0x1f0; off += 8 )
			{
				const auto value = try_read( global_vars + off );
				if ( value.empty( ) )
				{
					continue;
				}

				cached_offset = off;
				app::context().diagnostics.info( "[diag] map name slot found at global_vars+{:#x} (\"{}\")", off, value );
				return value;
			}

			return {};
		}

		void dump_global_vars_once( std::uintptr_t global_vars )
		{
			static bool done = false;
			if ( done )
			{
				return;
			}
			done = true;

			for ( std::uintptr_t off = 0x140; off <= 0x1f8; off += 8 )
			{
				const auto val = app::context().process.load<std::uintptr_t>( global_vars + off );

				std::string preview{};
				if ( val > 0x10000 )
				{
					preview = app::context().process.load_text( val, 32 );
					for ( std::size_t i = 0; i < preview.size( ); ++i )
					{
						if ( preview[ i ] < 0x20 || preview[ i ] > 0x7e )
						{
							preview.resize( i );
							break;
						}
					}
				}

				app::context().diagnostics.info( "[gv] +{:#x} = {:#x} \"{}\"", off, val, preview );
			}
		}

	}

	void game( )
	{
		constexpr auto update_interval = std::chrono::nanoseconds( 1'000'000'000 / 64 );
		auto next_update = std::chrono::steady_clock::now( );
		auto next_idle_entity_refresh = std::chrono::steady_clock::time_point{};
		auto next_map_probe = std::chrono::steady_clock::time_point{};
		std::string last_map{};

		std::jthread parse_worker{};

		std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

		std::uint64_t iterations{ 0 };
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
		auto last_heartbeat = std::chrono::steady_clock::now( );
#endif

		while ( true )
		{
			std::optional<platform::performance::scope> game_profile{};
			game_profile.emplace( platform::performance::zone::game_loop );
			++iterations;
			g_game_iter.store( iterations, std::memory_order_relaxed );

			g_game_step.store( 0, std::memory_order_relaxed );
			{
				VESTA_PERF_SCOPE( local_update );
				game::local_player().update( );
			}

			g_game_step.store( 1, std::memory_order_relaxed );
			{
				VESTA_PERF_SCOPE( auto_accept );
				features::misc::auto_accept().tick( );
			}

			std::uintptr_t global_vars{ 0 };
			std::string current_map{};

			if ( game::local_player().valid( ) )
			{
				const auto loop_now = std::chrono::steady_clock::now( );
				const auto directory_requested =
					game::world().entity_directory_requested( );
				if ( directory_requested || loop_now >= next_idle_entity_refresh )
				{
					g_game_step.store( 2, std::memory_order_relaxed );
					{
						VESTA_PERF_SCOPE( entity_refresh );
						game::entity_index().refresh( );
					}
					next_idle_entity_refresh = loop_now + std::chrono::milliseconds( 500 );
				}

				g_game_step.store( 3, std::memory_order_relaxed );
				{
					VESTA_PERF_SCOPE( world_update );
					game::world().run( );
				}
				{
					VESTA_PERF_SCOPE( bomb_update );
					features::visuals::bomb().tick( );
				}
				{
					VESTA_PERF_SCOPE( radar_update );
					features::visuals::radar().tick( );
				}

				g_game_step.store( 4, std::memory_order_relaxed );
				if ( loop_now >= next_map_probe )
				{
					next_map_probe = loop_now + std::chrono::milliseconds( 250 );
					global_vars = app::context().process.load<std::uintptr_t>(
						app::context().addresses.global_vars );
					if ( global_vars )
					{
						current_map = read_map_name( global_vars );

						if ( !current_map.empty( ) && current_map != last_map )
						{
						app::context().diagnostics.info( "map change: {} -> {}", last_map.empty( ) ? "none" : last_map, current_map );
						last_map = current_map;
						g_current_map.store(
							std::make_shared<const std::string>( current_map ),
							std::memory_order_release );

						g_game_step.store( 5, std::memory_order_relaxed );

						if ( parse_worker.joinable( ) )
						{
							parse_worker.request_stop( );
							parse_worker.join( );
						}

						game::collision().clear( );
						game::blast_damage().clear( );

						parse_worker = std::jthread( [ map = current_map ]( std::stop_token stop )
						{

							const auto background_mode = ::SetThreadPriority(
								::GetCurrentThread( ), THREAD_MODE_BACKGROUND_BEGIN ) != FALSE;
							if ( !background_mode )
							{
								::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_BELOW_NORMAL );
							}
							app::context().diagnostics.info( "parsing bvh for {}...", map );

							const auto from_file = game::collision().build_from_map_file( map );
							if ( !from_file )
							{
								app::context().diagnostics.warning( "bvh: file geometry unavailable, using live physics." );
								game::collision().parse( );
							}
							app::context().diagnostics.success( "bvh parsed." );
							game::blast_damage().parse( );

							if ( from_file )
							{
								while ( !stop.stop_requested( ) )
								{
									const auto deadline = std::chrono::steady_clock::now( )
										+ std::chrono::milliseconds( 100 );
									while ( !stop.stop_requested( )
										&& std::chrono::steady_clock::now( ) < deadline )
									{
										std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
									}
									if ( stop.stop_requested( ) ) break;
									game::collision().refresh_map_entities( );
								}
							}
							if ( background_mode )
							{
								::SetThreadPriority( ::GetCurrentThread( ), THREAD_MODE_BACKGROUND_END );
							}
						} );
						}
					}
				}
			}
			else
			{
				if ( !last_map.empty( ) )
				{
					last_map = {};
					g_current_map.store(
						std::make_shared<const std::string>( ),
						std::memory_order_release );

					if ( parse_worker.joinable( ) )
					{
						parse_worker.request_stop( );
						parse_worker.join( );
					}
					game::collision().clear( );
					game::blast_damage().clear( );
				}
			}

			g_game_step.store( 7, std::memory_order_relaxed );
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
			const auto now = std::chrono::steady_clock::now( );
			if ( now - last_heartbeat >= std::chrono::seconds( 3 ) )
			{
				last_heartbeat = now;
				app::context().diagnostics.info(
					"[heartbeat] iter={} valid={} controller={:#x} pawn={:#x} entity_list={:#x} entities={} players={} global_vars={:#x} map=\"{}\" bvh={} bomb_dmg={}",
					iterations, game::local_player().valid( ), game::local_player().controller( ), game::local_player().pawn( ),
					game::entity_index().raw_entity_list_for_diag( ), game::entity_index().all( )->size( ),
					game::world().players( )->size( ), global_vars, current_map, game::collision().valid( ), game::blast_damage().point_count( ) );

				if ( game::local_player().valid( ) && global_vars && current_map.empty( ) && last_map.empty( ) )
				{
					dump_global_vars_once( global_vars );
				}
			}
#endif

			g_game_step.store( 8, std::memory_order_relaxed );
			game_profile.reset( );
			next_update += update_interval;
			const auto after_work = std::chrono::steady_clock::now( );
			if ( next_update <= after_work )
			{
				next_update = after_work;
			}
			else
			{
				std::this_thread::sleep_until( next_update );
			}
		}
	}

	void watchdog( )
	{
		std::uint64_t last_iter{ 0 };

		while ( true )
		{
			std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

			const auto iter = g_game_iter.load( std::memory_order_relaxed );
			const auto step = g_game_step.load( std::memory_order_relaxed );
			const auto* step_name = ( step >= 0 && step < static_cast< int >( std::size( k_step_names ) ) ) ? k_step_names[ step ] : "not-started";

			if ( iter == last_iter )
			{
				app::context().diagnostics.warning( "[watchdog] game thread STUCK at \"{}\" (iter={})", step_name, iter );
			}
			else
			{
				app::context().diagnostics.info( "[watchdog] game thread alive: iter={} step=\"{}\"", iter, step_name );
			}

			last_iter = iter;
		}
	}

	void combat( )
	{

		constexpr auto target_tps{ 250 };
		constexpr auto tick_interval = std::chrono::nanoseconds( 1'000'000'000 / target_tps );
		auto next_tick = std::chrono::steady_clock::now( );

		while ( true )
		{
			std::optional<platform::performance::scope> combat_profile{};
			combat_profile.emplace( platform::performance::zone::combat_loop );
			if ( game::local_player().valid( ) && game::collision().valid( )
				&& app::context().overlay.combat_input_ready( )
				&& !app::context().menu.is_open( ) )
			{
				if ( shared_ballistics_requested( ) )
				{
					VESTA_PERF_SCOPE( ballistics_tick );
					simulation::ballistics().tick( );
				}
				{
					VESTA_PERF_SCOPE( aim_tick );
					features::aimbot::aim().tick( );
				}
				{
					VESTA_PERF_SCOPE( grenade_aim_tick );
					features::aimbot::grenade_aim().tick( );
				}
			}
			else
			{
				features::aimbot::aim().reset( );
			}

			combat_profile.reset( );
			next_tick += tick_interval;

			const auto now = std::chrono::steady_clock::now( );
			if ( next_tick <= now )
			{

				next_tick = now + tick_interval;
			}

			std::this_thread::sleep_until( next_tick );
		}
	}

	void pose_sampler( )
	{
		game::render_poses( ).run( );
	}

	void movement( )
	{

		constexpr auto interval = std::chrono::milliseconds( 1 );
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_ABOVE_NORMAL );

		while ( true )
		{
			{
				VESTA_PERF_SCOPE( movement_loop );
				{
					VESTA_PERF_SCOPE( bhop_tick );
					features::misc::bhop( ).tick( );
				}
				{
					VESTA_PERF_SCOPE( auto_stop_tick );
					features::misc::auto_stop( ).tick( );
				}
			}
			std::this_thread::sleep_for( interval );
		}
	}

	void nade_helper( )
	{

		constexpr auto active_interval = std::chrono::milliseconds( 1 );
		constexpr auto idle_interval = std::chrono::milliseconds( 8 );
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_ABOVE_NORMAL );

		while ( true )
		{
			{
				VESTA_PERF_SCOPE( nade_helper_tick );
				features::misc::nade_helper().tick( );
			}
			std::this_thread::sleep_for(
				config::general_settings.m_nade_helper.enabled
					? active_interval : idle_interval );
		}
	}

	void seed_trigger( )
	{

		auto latency_priority = false;
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_NORMAL );
		THREAD_POWER_THROTTLING_STATE power_state{
			THREAD_POWER_THROTTLING_CURRENT_VERSION,
			THREAD_POWER_THROTTLING_EXECUTION_SPEED,
			0 };
		( void )::SetThreadInformation( ::GetCurrentThread( ),
			ThreadPowerThrottling, &power_state, sizeof( power_state ) );

		HANDLE timer = ::CreateWaitableTimerExW( nullptr, nullptr,
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
		if ( !timer ) timer = ::CreateWaitableTimerW( nullptr, FALSE, nullptr );
		const auto precise_wait = [ timer ]( std::chrono::microseconds duration )
		{
			if ( duration <= std::chrono::microseconds::zero( ) )
			{
				::SwitchToThread( );
				return;
			}
			if ( timer )
			{
				LARGE_INTEGER due{};
				due.QuadPart = -std::max<LONGLONG>(
					1, static_cast<LONGLONG>( duration.count( ) ) * 10 );
				if ( ::SetWaitableTimer( timer, &due, 0, nullptr, nullptr, FALSE ) )
				{
					( void )::WaitForSingleObject( timer, INFINITE );
					return;
				}
			}
			::Sleep( duration >= std::chrono::milliseconds( 1 )
				? static_cast<DWORD>( duration.count( ) / 1000 ) : 0 );
		};

		int previous_tick{ -1 };
		auto previous_boundary = std::chrono::steady_clock::time_point{};
		auto command_period = std::chrono::microseconds( 15625 );

		while ( true )
		{
			auto& runtime = features::trigger::seed_trigger( );
			bool can_run{};
			bool configured{};
			bool latency_requested{};
			{
				VESTA_PERF_SCOPE( seed_trigger_tick );
				can_run = app::context().overlay.combat_input_ready( )
					&& !app::context().menu.is_open( );
				configured = config::combat_settings.seed_trigger_configured( );
				if ( !can_run ) runtime.reset( );

				if ( can_run && configured )
				{
					const auto observed_at = std::chrono::steady_clock::now( );
					const auto controller = app::context().process.load<std::uintptr_t>(
						app::context().addresses.local_player_controller );
					const auto binding = game::resolve_local_pawn( controller );
					const auto simulation_tick = binding.pawn
						? app::context().process.load<int>( binding.pawn
							+ SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id ) )
						: -1;
					const auto tick = simulation_tick >= 0 ? simulation_tick + 1 : -1;
					if ( tick > 0 && tick != previous_tick )
					{
						if ( previous_tick > 0 && previous_boundary
							!= std::chrono::steady_clock::time_point{} )
						{
							const auto measured = std::chrono::duration_cast<
								std::chrono::microseconds>( observed_at - previous_boundary );
							if ( tick == previous_tick + 1
								&& measured >= std::chrono::microseconds( 12000 )
								&& measured <= std::chrono::microseconds( 20000 ) )
							{
								command_period = std::chrono::microseconds(
									( command_period.count( ) * 7 + measured.count( ) ) / 8 );
							}
						}
						previous_tick = tick;
						previous_boundary = observed_at;
					}
					runtime.sync_phase( tick, observed_at );
				}
				latency_requested = can_run ? runtime.poll( ) : false;
				if ( latency_requested != latency_priority )
				{
					latency_priority = latency_requested;
					::SetThreadPriority( ::GetCurrentThread( ), latency_priority
						? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL );
				}
			}

			if ( !configured || !can_run )
			{
				precise_wait( std::chrono::milliseconds( 2 ) );
				continue;
			}
			if ( runtime.input_pending( ) )
			{

				precise_wait( std::chrono::microseconds( 500 ) );
				continue;
			}

			if ( previous_boundary == std::chrono::steady_clock::time_point{} )
			{
				precise_wait( std::chrono::microseconds( 500 ) );
				continue;
			}
			const auto now = std::chrono::steady_clock::now( );
			const auto prewake = previous_boundary + command_period
				- std::chrono::microseconds( 750 );
			if ( now < prewake )
			{
				precise_wait( std::chrono::duration_cast<
					std::chrono::microseconds>( prewake - now ) );
			}
			else
			{

				precise_wait( std::chrono::microseconds( 100 ) );
			}
		}
	}

}
