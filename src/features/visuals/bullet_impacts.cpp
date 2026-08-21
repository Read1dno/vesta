#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/visuals/event_log.hpp>
#include <render/chams/renderer.hpp>

namespace {

	struct remote_vector
	{
		std::int32_t size{};
		std::int32_t padding{};
		std::uintptr_t data{};
		std::int32_t capacity{};
		std::uint32_t flags{};
	};

	struct bullet_service_impact
	{
		foundation::vec3 position{};
		float timestamp{};
		float expiry{};
	};

	struct shot_history
	{
		foundation::vec3 angles{};
		foundation::vec3 shoot_position{};
		int render_tick{};
		float render_fraction{};
		int player_tick{};
		float player_fraction{};
		int sequence{};
		int history_index{};
		int attack_history{};
		float target_error{};
		float backtrack_delta{};
		bool selected_by_backtrack{};
		bool from_weapon_history{};
		bool has_shoot_position{};
		bool valid{};
	};

	struct history_candidate
	{
		foundation::vec3 angles{};
		foundation::vec3 shoot_position{};
		int index{};
		int render_tick{};
		float render_fraction{};
		int player_tick{};
		float player_fraction{};
		std::uint32_t bits{};
		bool has_shoot_position{};
	};

	[[nodiscard]] constexpr bool is_command_entry( std::uint32_t bits )
	{
		return ( bits & 0x1eu ) == 0x1eu;
	}

	[[nodiscard]] bool finite_vector( const foundation::vec3& value );

	[[nodiscard]] bool plausible_pointer( std::uintptr_t value );

	[[nodiscard]] double normalized_timestamp( int tick, float fraction )
	{
		if ( !std::isfinite( fraction ) )
		{
			return static_cast<double>( tick );
		}

		return static_cast<double>( tick ) + static_cast<double>( fraction );
	}

	[[nodiscard]] shot_history weapon_shot_history( std::uintptr_t pawn,
		std::uintptr_t weapon, float fire_time, const simulation::ballistics_t::context& pre_shot_ctx )
	{
		if ( !plausible_pointer( pawn ) || !plausible_pointer( weapon ) ||
			!std::isfinite( fire_time ) || fire_time <= 0.0f )
		{
			return {};
		}

		const auto weapon_services = app::context().process.load<std::uintptr_t>(
			pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_id ) );
		if ( !plausible_pointer( weapon_services ) )
		{
			return {};
		}

		const auto begin = app::context().process.load<int>( weapon_services + 0x1280 );
		const auto count = app::context().process.load<int>( weapon_services + 0x1284 );
		if ( count <= 0 || count > 32 )
		{
			return {};
		}

		std::array<history_candidate, 32> candidates{};
		std::size_t candidate_count{};
		for ( int index = 0; index < count; ++index )
		{
			auto slot = ( begin + index ) & 31;
			if ( slot < 0 )
			{
				slot += 32;
			}

			const auto entry = weapon_services + 0x380 +
				static_cast<std::uintptr_t>( slot ) * 0x78;
			const auto entry_bits = app::context().process.load<std::uint32_t>( entry + 0x10 );
			const auto angle_message = app::context().process.load<std::uintptr_t>( entry + 0x18 );
			const auto player_tick = app::context().process.load<int>( entry + 0x68 );
			const auto player_fraction = app::context().process.load<float>( entry + 0x6c );
			if ( ( entry_bits & 0x1u ) == 0 || !plausible_pointer( angle_message ) ||
				player_tick <= 0 || !std::isfinite( player_fraction ) )
			{
				continue;
			}

			const auto angles = app::context().process.load<foundation::vec3>( angle_message + 0x18 );
			if ( !finite_vector( angles ) || std::abs( angles.x ) > 180.0f ||
				std::abs( angles.y ) > 720.0f )
			{
				continue;
			}

			const auto shoot_message = app::context().process.load<std::uintptr_t>( entry + 0x40 );
			foundation::vec3 shoot_position{};
			const auto has_shoot_position = ( entry_bits & 0x20u ) != 0 &&
				plausible_pointer( shoot_message ) &&
				( shoot_position = app::context().process.load<foundation::vec3>( shoot_message + 0x18 ),
					finite_vector( shoot_position ) && shoot_position.length_sqr( ) > 1.0f );

			const auto render_tick = app::context().process.load<int>( entry + 0x60 );
			const auto render_fraction = app::context().process.load<float>( entry + 0x64 );

			candidates[ candidate_count++ ] = { angles, shoot_position, index,
				render_tick, render_fraction,
				player_tick, player_fraction, entry_bits, has_shoot_position };
		}

		if ( !candidate_count )
		{
			return {};
		}

		const auto make_result = [ ]( const history_candidate& value, float fire_error,
			float backtrack_delta, bool selected_by_backtrack )
		{
			shot_history result{};
			result.angles = value.angles;
			result.shoot_position = value.shoot_position;
			result.render_tick = value.render_tick;
			result.render_fraction = value.render_fraction;
			result.player_tick = value.player_tick;
			result.player_fraction = value.player_fraction;
			result.sequence = -1;
			result.history_index = value.index;
			result.attack_history = -1;
			result.target_error = fire_error;
			result.backtrack_delta = backtrack_delta;
			result.selected_by_backtrack = selected_by_backtrack;
			result.from_weapon_history = true;
			result.has_shoot_position = value.has_shoot_position;
			result.valid = true;
			return result;
		};

		(void)pre_shot_ctx;
		const auto fire_target = static_cast<double>( fire_time ) * 64.0;
		const auto ft_tick = static_cast<int>( std::llround( fire_target ) );

		const history_candidate* chosen{};
		for ( const auto cand_tick : { ft_tick, ft_tick - 1 } )
		{
			for ( const auto& value : std::span{ candidates }.first( candidate_count ) )
			{

				if ( value.player_tick == cand_tick && is_command_entry( value.bits ) &&
					( !chosen || value.index > chosen->index ) )
				{
					chosen = &value;
				}
			}
			if ( chosen )
			{
				break;
			}
		}
		if ( chosen )
		{
			return make_result( *chosen, 0.0f, 0.0f, true );
		}

		const history_candidate* nearest{};
		auto nearest_error = std::numeric_limits<double>::max( );
		for ( const auto& value : std::span{ candidates }.first( candidate_count ) )
		{
			const auto error = std::abs( normalized_timestamp(
				value.player_tick, value.player_fraction ) - fire_target );
			if ( error < nearest_error )
			{
				nearest_error = error;
				nearest = &value;
			}
		}

		return nearest && nearest_error <= 1.0
			? make_result( *nearest, static_cast<float>( nearest_error ), 0.0f, false )
			: shot_history{};
	}

	[[nodiscard]] foundation::vec3 read_full_aim_punch( std::uintptr_t pawn )
	{
		if ( !plausible_pointer( pawn ) )
		{
			return {};
		}

		const auto services = app::context().process.load<std::uintptr_t>(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_id ) );
		if ( !plausible_pointer( services ) )
		{
			return {};
		}

		foundation::vec3 punch{};
		for ( const auto track_offset : { std::uintptr_t{ 0x68 }, std::uintptr_t{ 0xb0 } } )
		{
			const auto samples = app::context().process.load<remote_vector>(
				services + track_offset + 0x20 );
			if ( samples.size <= 0 || samples.size > 4096 ||
				samples.capacity < samples.size || samples.capacity > 8192 ||
				!plausible_pointer( samples.data ) )
			{
				continue;
			}

			const auto sample = app::context().process.load<foundation::vec3>(
				samples.data + static_cast<std::uintptr_t>( samples.size - 1 ) *
					sizeof( foundation::vec3 ) );
			if ( finite_vector( sample ) && std::abs( sample.x ) < 45.0f &&
				std::abs( sample.y ) < 45.0f && std::abs( sample.z ) < 45.0f )
			{
				punch += sample;
			}
		}

		punch *= 2.0f;
		return finite_vector( punch ) ? punch : foundation::vec3{};
	}

	[[nodiscard]] bool finite_vector( const foundation::vec3& value )
	{
		return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
	}

	[[nodiscard]] bool plausible_segment( const foundation::vec3& start, const foundation::vec3& end )
	{
		if ( !finite_vector( start ) || !finite_vector( end ) )
		{
			return false;
		}

		const auto distance = start.distance( end );
		return distance >= 8.0f && distance <= 16384.0f;
	}

	[[nodiscard]] bool plausible_pointer( std::uintptr_t value )
	{
		return value >= 0x10000 && value <= 0x00007fffffffffffULL;
	}

	[[nodiscard]] float point_segment_distance_sqr( const foundation::vec3& point,
		const foundation::vec3& start, const foundation::vec3& end )
	{
		const auto segment = end - start;
		const auto length_sqr = segment.length_sqr( );
		if ( length_sqr <= 0.0001f )
		{
			return point.distance_sqr( start );
		}
		const auto t = std::clamp( ( point - start ).dot( segment ) / length_sqr, 0.0f, 1.0f );
		return point.distance_sqr( start + segment * t );
	}

	[[nodiscard]] bool impact_touches_player( const foundation::vec3& impact,
		const game::player_snapshot& player, const game::skeleton_reader::data& bones )
	{
		bool tested_hitbox{};
		for ( const auto& hitbox : player.hitboxes )
		{
			if ( hitbox.index < 0 || hitbox.bone < 0 || hitbox.bone >= 128 )
			{
				continue;
			}
			const auto& bone = bones.bones[ static_cast<std::size_t>( hitbox.bone ) ];
			if ( !finite_vector( bone.position ) )
			{
				continue;
			}
			tested_hitbox = true;
			const auto start = bone.position + bone.rotation.apply( hitbox.mins );
			const auto end = bone.position + bone.rotation.apply( hitbox.maxs );
			if ( hitbox.radius > 0.0f )
			{

				const auto radius = hitbox.radius + 5.0f;
				if ( point_segment_distance_sqr( impact, start, end ) <= radius * radius )
				{
					return true;
				}
			}
			else
			{

				const auto center = bone.position + bone.rotation.apply( ( hitbox.mins + hitbox.maxs ) * 0.5f );
				const auto radius = ( hitbox.maxs - hitbox.mins ).length( ) * 0.5f + 5.0f;
				if ( impact.distance_sqr( center ) <= radius * radius )
				{
					return true;
				}
			}
		}

		if ( tested_hitbox )
		{
			return false;
		}

		const auto delta = impact - player.collision_center;
		return delta.x * delta.x + delta.y * delta.y <= 36.0f * 36.0f
			&& std::abs( delta.z ) <= 54.0f;
	}

}

namespace features::visuals {

	void bullet_impacts_t::capture_shot( )
	{
		const auto pawn = game::local_player().pawn( );
		const auto weapon_type = game::local_player().weapon_type( );
		const auto current_ctx = simulation::ballistics().ctx( );
		const auto cache_current_ctx = [ & ]( )
		{
			if ( current_ctx.valid )
			{
				this->m_cached_weapon_ctx = current_ctx;
				this->m_has_cached_weapon_ctx = true;
			}
		};
		const auto supported_weapon = game::rules::is_firearm( weapon_type ) || weapon_type == game::rules::electroshock;
		if ( !pawn || !game::local_player().weapon( ) || !supported_weapon )
		{
			this->m_last_fire_time = -1.0f;
			this->m_last_shot_render_tick = -1;
			this->m_has_cached_weapon_ctx = false;
			this->m_pending_shots.clear( );
			return;
		}

		const auto fire_time = app::context().process.load<float>( pawn + SCHEMA( "C_CSPlayerPawn", "m_flLastFiredWeaponTime"_id ) );
		if ( !std::isfinite( fire_time ) || fire_time <= 0.0f )
		{
			cache_current_ctx( );
			return;
		}

		if ( this->m_last_fire_time < 0.0f )
		{
			const auto globals = app::context().process.load<std::uintptr_t>( app::context().addresses.global_vars );
			const auto current_time = globals ? app::context().process.load<float>( globals + 0x30 ) : 0.0f;
			if ( !std::isfinite( current_time ) || std::abs( current_time - fire_time ) > 0.20f )
			{
				this->m_last_fire_time = fire_time;
				cache_current_ctx( );
				return;
			}
		}
		else if ( std::abs( fire_time - this->m_last_fire_time ) < 0.0001f )
		{
			cache_current_ctx( );
			return;
		}

		const auto weapon = game::local_player().weapon( );
		const auto post_shot_time = app::context().process.load<float>(
			weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_id ) );
		const auto historical_ctx = simulation::ballistics().shot_ctx(
			fire_time, weapon, post_shot_time );

		auto ctx = historical_ctx.valid
			? historical_ctx
			: ( this->m_has_cached_weapon_ctx &&
				this->m_cached_weapon_ctx.weapon == current_ctx.weapon &&
				this->m_cached_weapon_ctx.on_ground == current_ctx.on_ground
				? this->m_cached_weapon_ctx
				: current_ctx );

		if ( ctx.weapon == current_ctx.weapon && !historical_ctx.valid )
		{

			auto dbg = ctx.debug;
			ctx.inaccuracy = simulation::ballistics().get_inaccuracy_preshot(
				ctx.weapon, ctx.weapon_vdata, ctx.weapon_type,
				ctx.velocity,
				ctx.on_ground,
				ctx.debug.accuracy_penalty,
				ctx.recoil_index,
				ctx.is_walking,
				dbg );
			ctx.debug = dbg;
		}
		const auto history = weapon_shot_history( pawn, weapon, fire_time, ctx );
		if ( !history.valid )
		{
			return;
		}
		foundation::vec3 sampled_start{};
		foundation::vec3 sampled_angles{};
		if ( !game::camera().sample( sampled_start, sampled_angles ) )
		{
			return;
		}

		const auto start = history.has_shoot_position
			? history.shoot_position
			: sampled_start;
		auto pre_shot_punch = app::context().process.load<foundation::vec3>( weapon + 0x1b1c );
		const auto exact_recoil = finite_vector( pre_shot_punch ) &&
			std::abs( pre_shot_punch.x ) < 89.0f &&
			std::abs( pre_shot_punch.y ) < 89.0f &&
			std::abs( pre_shot_punch.z ) < 89.0f;
		if ( !exact_recoil )
		{

			const auto fallback_ctx = this->m_has_cached_weapon_ctx
				? this->m_cached_weapon_ctx
				: current_ctx;
			pre_shot_punch = fallback_ctx.recoil_index <= 1.0f
				? foundation::vec3{}
				: read_full_aim_punch( pawn );
		}

		const auto seed_angles = history.angles;
		const auto shot_angles = seed_angles + pre_shot_punch;

		foundation::vec3 forward{};
		foundation::vec3 right{};
		foundation::vec3 up{};
		shot_angles.to_directions( &forward, &right, &up );
		if ( !finite_vector( start ) || !finite_vector( forward ) || forward.length_sqr( ) < 0.9f )
		{
			return;
		}
		const auto bullet_count = weapon_type == game::rules::electroshock ? 1 : std::clamp( ctx.num_bullets, 1, 32 );

		const auto seed_tick = history.player_tick > 0
			? history.player_tick
			: history.render_tick;

		const auto seed = simulation::ballistics().derive_command_seed(
			seed_angles, seed_tick ) + 1u;
		const auto weapon_vdata = game::local_player().weapon_vdata( );
		const auto raw_range = weapon_vdata
			? app::context().process.load<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_id ) )
			: 0.0f;
		const auto range = std::isfinite( raw_range ) && raw_range >= 64.0f
			? std::min( raw_range, 8192.0f )
			: ( weapon_type == game::rules::electroshock ? 220.0f : 8192.0f );
		const auto now = std::chrono::steady_clock::now( );

		for ( int bullet = 0; bullet < bullet_count; ++bullet )
		{
			auto direction = forward;
			if ( ctx.valid && weapon_type != game::rules::electroshock )
			{
				const auto spread = simulation::ballistics().sample_spread_offset(
					static_cast<int>( seed ), ctx.inaccuracy, ctx.spread, ctx.recoil_index,
					ctx.item_def_idx, ctx.fire_mode, ctx.num_bullets, bullet );

				direction = ( forward + right * spread.x + up * spread.y ).normalized( );
			}
			else
			{
				direction = direction.normalized( );
			}

			foundation::vec3 traced_end{};
			if ( game::collision().valid( ) && ( weapon_type == game::rules::electroshock || ctx.valid ) )
			{
				const auto trace = game::collision().trace_ray( start, start + direction * range );
				if ( trace.hit ) traced_end = trace.end_pos;
			}
			this->m_pending_shots.push_back( { start, direction, traced_end, now, false } );
		}

		this->m_last_fire_time = fire_time;
		this->m_last_shot_render_tick = history.render_tick;

		cache_current_ctx( );
	}

	bool bullet_impacts_t::select_pending_shot( const foundation::vec3& end, foundation::vec3& start, const foundation::vec3* observed_start )
	{
		const auto now = std::chrono::steady_clock::now( );
		auto best_alignment{ 0.965f };
		pending_shot_t* best{};

		for ( auto& shot : this->m_pending_shots )
		{
			if ( shot.resolved ) continue;
			if ( now - shot.timestamp > std::chrono::milliseconds( 700 ) )
			{
				continue;
			}
			if ( observed_start && shot.start.distance_sqr( *observed_start ) > 9216.0f )
			{
				continue;
			}

			const auto delta = end - shot.start;
			if ( delta.length_sqr( ) < 64.0f )
			{
				continue;
			}

			const auto alignment = delta.normalized( ).dot( shot.direction );
			if ( alignment > best_alignment )
			{
				best_alignment = alignment;
				best = &shot;
			}
		}

		if ( !best )
		{
			return false;
		}

		start = best->start;
		best->resolved = true;
		return true;
	}

	void bullet_impacts_t::resolve_pending_traces( )
	{

	}

	void bullet_impacts_t::poll_server_hits( )
	{
		const auto pawn = game::local_player().pawn( );

		if ( !pawn ) return;
		if ( pawn != this->m_hit_counter_pawn )
		{
			this->m_hit_counter_pawn = pawn;
			this->m_last_total_hits = -1;
			this->m_hit_confirmations.clear( );
			this->m_hit_candidates.clear( );
		}
		std::uintptr_t services{};
		if ( !app::context().process.copy(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_pBulletServices"_id ),
			&services, sizeof( services ) ) ) return;
		if ( !plausible_pointer( services ) )
		{
			return;
		}

		std::int32_t total{};
		if ( !app::context().process.copy(
			services + SCHEMA( "CCSPlayer_BulletServices", "m_totalHitsOnServer"_id ),
			&total, sizeof( total ) ) ) return;
		if ( total < 0 || total > 1'000'000 )
		{
			return;
		}
		if ( this->m_last_total_hits < 0 || total < this->m_last_total_hits )
		{
			this->m_last_total_hits = total;
			return;
		}

		const auto delta = total - this->m_last_total_hits;
		this->m_last_total_hits = total;
		if ( delta <= 0 || delta > 32 ) return;

		const auto now = std::chrono::steady_clock::now( );
		for ( int hit = 0; hit < delta && this->m_hit_confirmations.size( ) < 32; ++hit )
		{
			this->m_hit_confirmations.push_back( now );
		}
	}

	void bullet_impacts_t::poll_action_tracking( )
	{
		const auto controller = game::local_player().controller( );
		if ( !controller ) return;

		std::uintptr_t services{};
		if ( !app::context().process.copy( controller
			+ SCHEMA( "CCSPlayerController", "m_pActionTrackingServices"_id ),
			&services, sizeof( services ) ) ) return;
		if ( !plausible_pointer( services ) )
			return;
		if ( services != this->m_action_tracking_services )
		{
			this->m_action_tracking_services = services;
			this->m_action_tracking_initialized = false;
			this->m_action_feedback.clear( );
		}

		float total_damage{};
		std::int32_t round_kills{};
		if ( !app::context().process.copy( services
			+ SCHEMA( "CCSPlayerController_ActionTrackingServices",
				"m_flTotalRoundDamageDealt"_id ), &total_damage, sizeof( total_damage ) )
			|| !app::context().process.copy( services
				+ SCHEMA( "CCSPlayerController_ActionTrackingServices",
					"m_iNumRoundKills"_id ), &round_kills, sizeof( round_kills ) ) )
			return;
		if ( !std::isfinite( total_damage ) || total_damage < 0.0f
			|| total_damage > 1'000'000.0f || round_kills < 0 || round_kills > 128 )
			return;

		if ( !this->m_action_tracking_initialized
			|| total_damage + 0.25f < this->m_last_round_damage
			|| round_kills < this->m_last_round_kills )
		{
			this->m_last_round_damage = total_damage;
			this->m_last_round_kills = round_kills;
			this->m_action_tracking_initialized = true;
			this->m_action_feedback.clear( );
			return;
		}

		const auto damage_delta = static_cast<int>( std::lround(
			total_damage - this->m_last_round_damage ) );
		const auto kill_delta = round_kills - this->m_last_round_kills;
		this->m_last_round_damage = total_damage;
		this->m_last_round_kills = round_kills;
		if ( damage_delta <= 0 && kill_delta <= 0 ) return;

		this->m_action_feedback.push_back( {
			std::clamp( damage_delta, 0, 10000 ), std::clamp( kill_delta, 0, 16 ),
			std::chrono::steady_clock::now( ) } );
		if ( this->m_action_feedback.size( ) > 32 )
			this->m_action_feedback.erase( this->m_action_feedback.begin( ),
				this->m_action_feedback.end( ) - 32 );
	}

	bullet_impacts_t::confirmed_hit bullet_impacts_t::latest_confirmed_hit( ) const
	{
		const std::scoped_lock lock{ this->m_confirmed_hits_mutex };
		return this->m_latest_confirmed_hit;
	}

	std::vector<bullet_impacts_t::confirmed_hit> bullet_impacts_t::confirmed_hits_since(
		const std::uint64_t sequence ) const
	{
		const std::scoped_lock lock{ this->m_confirmed_hits_mutex };
		std::vector<confirmed_hit> result{};
		result.reserve( this->m_confirmed_hits.size( ) );
		for ( const auto& hit : this->m_confirmed_hits )
			if ( hit.sequence > sequence ) result.push_back( hit );
		return result;
	}

	void bullet_impacts_t::emit_confirmed_hit( const std::uintptr_t pawn,
		const foundation::vec3& position, const int damage, const bool killed,
		const bool correlate_trigger )
	{
		if ( !pawn || damage <= 0 || !finite_vector( position ) ) return;
		const auto now = std::chrono::steady_clock::now( );
		this->m_recent_bullet_hits[ pawn ] = now;
		{
			const std::scoped_lock lock{ this->m_confirmed_hits_mutex };
			this->m_latest_confirmed_hit = { pawn,
				this->m_latest_confirmed_hit.sequence + 1, damage, killed, now };
			this->m_confirmed_hits.push_back( this->m_latest_confirmed_hit );
			if ( this->m_confirmed_hits.size( ) > 64 )
				this->m_confirmed_hits.erase( this->m_confirmed_hits.begin( ),
					this->m_confirmed_hits.end( ) - 64 );
		}
		const auto correlated = correlate_trigger
			&& features::visuals::event_log( ).resolve_latest_trigger_shot(
				damage, killed );
		if ( !correlated )
			features::visuals::event_log( ).push(
				killed ? std::format( "Kill: {} damage", damage )
					: std::format( "Hit: -{} HP", damage ),
				killed ? features::visuals::event_kind::kill
					: features::visuals::event_kind::hit,
				killed ? features::visuals::event_category::kill
					: features::visuals::event_category::hit );
		if ( config::general_settings.m_hitmarker.enabled )
		{
			this->m_hitmarkers.push_back( { position, now } );
			if ( this->m_hitmarkers.size( ) > 32 )
				this->m_hitmarkers.erase( this->m_hitmarkers.begin( ) );
		}
		const auto& feedback = config::general_settings.m_hitsound;
		if ( feedback.enabled )
			features::visuals::hitsounds().play( feedback.style, feedback.volume );
		if ( feedback.show_damage )
		{
			this->m_damage_popups.push_back( {
				position, std::clamp( damage, 1, 1000 ), now } );
			if ( this->m_damage_popups.size( ) > 32 )
				this->m_damage_popups.erase( this->m_damage_popups.begin( ) );
		}
	}

	void bullet_impacts_t::collect_server_damage( )
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto players = game::world().players( );
		static thread_local std::vector<std::uint32_t> current{};
		current.clear( );
		if ( players && current.capacity( ) < players->size( ) )
			current.reserve( players->size( ) );
		if ( players )
		{
			for ( const auto& player : *players )
			{
				if ( !game::local_player().is_enemy( player.team )
					|| !player.pawn_handle ) continue;
				current.push_back( player.pawn_handle );
				const auto position = finite_vector( player.collision_center )
					? player.collision_center : player.origin;
				const auto previous = this->m_server_health.find( player.pawn_handle );
				if ( previous != this->m_server_health.end( )
					&& previous->second.pawn == player.pawn
					&& player.health < previous->second.health )
				{
					auto hit_position = position;
					if ( const auto impact = this->m_recent_hit_positions.find( player.pawn );
						impact != this->m_recent_hit_positions.end( )
						&& now - impact->second.timestamp < std::chrono::milliseconds( 700 ) )
						hit_position = impact->second.position;
					this->m_pending_server_damage.push_back( { player.pawn, hit_position,
						previous->second.health - player.health, player.health <= 0, now } );
				}
				this->m_server_health[ player.pawn_handle ] = {
					player.pawn, player.pawn_handle, player.health, position, now };
			}
		}

		std::size_t recent_missing{};
		for ( const auto& [ handle, cached ] : this->m_server_health )
			if ( std::ranges::find( current, handle ) == current.end( )
				&& now - cached.seen <= std::chrono::milliseconds( 700 ) )
				++recent_missing;
		const auto authoritative_kill = std::ranges::any_of(
			this->m_action_feedback, [ & ]( const action_feedback_t& feedback )
			{
				return feedback.kills > 0
					&& now - feedback.timestamp <= std::chrono::milliseconds( 900 );
			} );

		for ( auto it = this->m_server_health.begin( ); it != this->m_server_health.end( ); )
		{
			if ( std::ranges::find( current, it->first ) != current.end( ) )
			{
				++it;
				continue;
			}
			if ( now - it->second.seen > std::chrono::milliseconds( 700 ) )
			{
				it = this->m_server_health.erase( it );
				continue;
			}
			const auto evidence = this->m_local_victim_evidence.find( it->second.pawn );
			const auto position_evidence = this->m_recent_hit_positions.find( it->second.pawn );
			const auto strong = evidence != this->m_local_victim_evidence.end( )
				&& now - evidence->second.timestamp < std::chrono::milliseconds( 700 );
			const auto positional = position_evidence != this->m_recent_hit_positions.end( )
				&& now - position_evidence->second.timestamp < std::chrono::milliseconds( 250 );
			const auto confirmed = std::ranges::any_of( this->m_hit_confirmations,
				[ & ]( const auto confirmation )
				{
					return now - confirmation < std::chrono::milliseconds( 900 );
				} );
			if ( ( confirmed && ( strong || positional ) )
				|| ( authoritative_kill && recent_missing == 1 ) )
			{
				auto position = it->second.position;
				if ( const auto impact = this->m_recent_hit_positions.find( it->second.pawn );
					impact != this->m_recent_hit_positions.end( )
					&& now - impact->second.timestamp < std::chrono::milliseconds( 700 ) )
					position = impact->second.position;
				this->m_pending_server_damage.push_back( { it->second.pawn, position,
					std::max( it->second.health, 1 ), true, now } );
				it = this->m_server_health.erase( it );
				continue;
			}
			++it;
		}
		if ( this->m_pending_server_damage.size( ) > 32 )
			this->m_pending_server_damage.erase( this->m_pending_server_damage.begin( ),
				this->m_pending_server_damage.end( ) - 32 );
		std::erase_if( this->m_recent_hit_positions, [ & ]( const auto& item )
			{ return now - item.second.timestamp > std::chrono::seconds( 1 ); } );
		std::erase_if( this->m_local_victim_evidence, [ & ]( const auto& item )
			{ return now - item.second.timestamp > std::chrono::seconds( 1 ); } );
	}

	void bullet_impacts_t::resolve_action_feedback( )
	{
		const auto now = std::chrono::steady_clock::now( );
		constexpr auto window = std::chrono::milliseconds( 900 );
		std::erase_if( this->m_action_feedback,
			[ & ]( const action_feedback_t& feedback )
			{ return now - feedback.timestamp > window; } );

		for ( auto feedback = this->m_action_feedback.begin( );
			feedback != this->m_action_feedback.end( ); )
		{
			auto best = this->m_pending_server_damage.end( );
			auto best_delta = std::chrono::steady_clock::duration{ window };
			int best_rank{ 99 };
			std::size_t equally_ranked{};
			for ( auto candidate = this->m_pending_server_damage.begin( );
				candidate != this->m_pending_server_damage.end( ); ++candidate )
			{
				const auto delta = candidate->timestamp > feedback->timestamp
					? candidate->timestamp - feedback->timestamp
					: feedback->timestamp - candidate->timestamp;
				if ( delta > window ) continue;
				const auto evidence = this->m_local_victim_evidence.find( candidate->pawn );
				const auto identified = evidence != this->m_local_victim_evidence.end( )
					&& now - evidence->second.timestamp <= window;
				const auto exact_damage = feedback->damage > 0
					&& std::abs( candidate->damage - feedback->damage ) <= 1;
				const auto kill_match = feedback->kills > 0 && candidate->killed;
				const auto rank = kill_match ? 0 : ( identified && exact_damage ? 1
					: ( identified ? 2 : ( exact_damage ? 3 : 4 ) ) );
				if ( rank < best_rank || ( rank == best_rank && delta < best_delta ) )
				{
					best = candidate;
					best_rank = rank;
					best_delta = delta;
					equally_ranked = 1;
				}
				else if ( rank == best_rank )
				{
					++equally_ranked;
				}
			}

			const auto unambiguous = best != this->m_pending_server_damage.end( )
				&& ( best_rank <= 2 || ( best_rank == 3 && equally_ranked == 1 ) );
			if ( !unambiguous )
			{
				++feedback;
				continue;
			}

			this->emit_confirmed_hit( best->pawn, best->position, best->damage,
				best->killed );
			this->m_pending_server_damage.erase( best );
			if ( !this->m_hit_confirmations.empty( ) )
			{
				auto closest = std::ranges::min_element( this->m_hit_confirmations,
					[ & ]( const auto left, const auto right )
					{
						return std::chrono::abs( left - feedback->timestamp )
							< std::chrono::abs( right - feedback->timestamp );
					} );
				if ( closest != this->m_hit_confirmations.end( )
					&& std::chrono::abs( *closest - feedback->timestamp ) <= window )
					this->m_hit_confirmations.erase( closest );
			}
			feedback = this->m_action_feedback.erase( feedback );
		}
	}

	void bullet_impacts_t::resolve_server_damage( )
	{
		const auto now = std::chrono::steady_clock::now( );

		if ( this->m_action_tracking_initialized )
		{
			std::erase_if( this->m_hit_confirmations, [ & ]( const auto timestamp )
				{ return now - timestamp > std::chrono::milliseconds( 300 ); } );
			return;
		}

		constexpr auto window = std::chrono::milliseconds( 300 );
		std::erase_if( this->m_hit_confirmations, [ & ]( const auto timestamp )
			{ return now - timestamp > window; } );
		std::erase_if( this->m_pending_server_damage,
			[ & ]( const pending_server_damage_t& damage )
			{ return now - damage.timestamp > window; } );
		while ( !this->m_hit_confirmations.empty( )
			&& !this->m_pending_server_damage.empty( ) )
		{
			const auto confirmation = this->m_hit_confirmations.front( );
			auto best = this->m_pending_server_damage.end( );
			auto best_delta = std::chrono::steady_clock::duration{ window };
			bool best_has_identity{};
			std::size_t narrow_candidates{};
			auto narrow = this->m_pending_server_damage.end( );
			for ( auto candidate = this->m_pending_server_damage.begin( );
				candidate != this->m_pending_server_damage.end( ); ++candidate )
			{
				const auto delta = candidate->timestamp > confirmation
					? candidate->timestamp - confirmation : confirmation - candidate->timestamp;
				const auto evidence = this->m_local_victim_evidence.find( candidate->pawn );
				const auto strong = evidence != this->m_local_victim_evidence.end( )
					&& ( evidence->second.timestamp > confirmation
						? evidence->second.timestamp - confirmation
						: confirmation - evidence->second.timestamp ) <= window;
				const auto impact = this->m_recent_hit_positions.find( candidate->pawn );
				const auto positional = impact != this->m_recent_hit_positions.end( )
					&& ( impact->second.timestamp > confirmation
						? impact->second.timestamp - confirmation
						: confirmation - impact->second.timestamp )
						<= std::chrono::milliseconds( 250 );
				const auto identified = strong || positional;
				if ( identified && ( !best_has_identity || delta < best_delta ) )
				{
					best = candidate;
					best_delta = delta;
					best_has_identity = true;
				}
				if ( delta <= std::chrono::milliseconds( 120 ) )
				{
					++narrow_candidates;
					narrow = candidate;
				}
			}

			if ( !best_has_identity )
				best = narrow_candidates == 1 ? narrow : this->m_pending_server_damage.end( );
			if ( best == this->m_pending_server_damage.end( ) ) break;
			this->emit_confirmed_hit(
				best->pawn, best->position, best->damage, best->killed );
			this->m_pending_server_damage.erase( best );
			this->m_hit_confirmations.erase( this->m_hit_confirmations.begin( ) );
		}
	}

	void bullet_impacts_t::resolve_hitmarkers( )
	{
		const auto now = std::chrono::steady_clock::now( );
		constexpr auto pairing_window = std::chrono::milliseconds( 900 );
		std::erase_if( this->m_hit_candidates, [ & ]( const hit_candidate_t& candidate )
			{ return now - candidate.timestamp > pairing_window; } );
		std::erase_if( this->m_hit_confirmations, [ & ]( const auto confirmation )
			{ return now - confirmation > pairing_window; } );

		while ( !this->m_hit_candidates.empty( ) && !this->m_hit_confirmations.empty( ) )
		{
			auto candidate = this->m_hit_candidates.front( );
			const auto confirmation = this->m_hit_confirmations.front( );
			const auto separation = candidate.timestamp > confirmation
				? candidate.timestamp - confirmation : confirmation - candidate.timestamp;
			if ( separation > pairing_window )
			{
				if ( candidate.timestamp < confirmation ) this->m_hit_candidates.erase( this->m_hit_candidates.begin( ) );
				else this->m_hit_confirmations.erase( this->m_hit_confirmations.begin( ) );
				continue;
			}

			this->m_hitmarkers.push_back( { candidate.position, now } );
			this->m_hit_candidates.erase( this->m_hit_candidates.begin( ) );
			this->m_hit_confirmations.erase( this->m_hit_confirmations.begin( ) );
			if ( this->m_hitmarkers.size( ) > 32 ) this->m_hitmarkers.erase( this->m_hitmarkers.begin( ) );
		}
	}

	void bullet_impacts_t::render_hitmarkers( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::general_settings.m_hitmarker;
		if ( !cfg.enabled )
		{
			this->m_hitmarkers.clear( );
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		const auto duration = std::clamp( cfg.duration, 0.05f, 2.0f );
		std::erase_if( this->m_hitmarkers, [ & ]( const hitmarker_t& marker )
			{ return std::chrono::duration<float>( now - marker.timestamp ).count( ) >= duration; } );

		const auto smoothstep = [ ]( float value )
		{
			value = std::clamp( value, 0.0f, 1.0f );
			return value * value * ( 3.0f - 2.0f * value );
		};
		static constexpr std::array<foundation::vec2, 4> directions{
			foundation::vec2{ -0.70710678f, -0.70710678f },
			foundation::vec2{ 0.70710678f, -0.70710678f },
			foundation::vec2{ 0.70710678f, 0.70710678f },
			foundation::vec2{ -0.70710678f, 0.70710678f } };

		for ( const auto& marker : this->m_hitmarkers )
		{
			const auto screen = game::camera().project( marker.position );
			if ( !game::camera().projection_valid( screen ) ) continue;

			const auto elapsed = std::chrono::duration<float>( now - marker.timestamp ).count( );
			const auto phase = std::clamp( elapsed / duration, 0.0f, 1.0f );
			const auto appear = smoothstep( phase / 0.16f );
			const auto fade = 1.0f - smoothstep( ( phase - 0.52f ) / 0.48f );
			const auto scale = 0.72f + appear * 0.28f;
			const auto alpha = static_cast<std::uint8_t>( std::clamp(
				static_cast<float>( cfg.color.a ) * fade, 0.0f, 255.0f ) );
			if ( alpha == 0 ) continue;

			const auto inner = std::max( 0.0f, cfg.gap ) * scale;
			const auto outer = inner + std::max( 1.0f, cfg.size ) * scale;
			const auto thickness = std::clamp( cfg.thickness, 1.0f, 5.0f );
			const zdraw::rgba outline{ 0, 0, 0, static_cast<std::uint8_t>( alpha * 0.82f ) };
			const zdraw::rgba color{ cfg.color.r, cfg.color.g, cfg.color.b, alpha };
			for ( const auto& direction : directions )
			{
				const auto x0 = screen.x + direction.x * inner;
				const auto y0 = screen.y + direction.y * inner;
				const auto x1 = screen.x + direction.x * outer;
				const auto y1 = screen.y + direction.y * outer;
				draw_list.add_line( x0, y0, x1, y1, outline, thickness + 2.0f );
				draw_list.add_line( x0, y0, x1, y1, color, thickness );
			}
		}
	}

	void bullet_impacts_t::render_damage_numbers( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::general_settings.m_hitsound;
		if ( !cfg.show_damage )
		{
			this->m_damage_popups.clear( );
			return;
		}

		const auto* base_font = app::context().overlay.fonts().esp_text_11;
		if ( !base_font ) return;
		const auto now = std::chrono::steady_clock::now( );
		const auto duration = std::clamp( cfg.damage_duration, 0.15f, 2.0f );
		std::erase_if( this->m_damage_popups, [ & ]( const damage_popup_t& popup )
		{
			return std::chrono::duration<float>( now - popup.timestamp ).count( ) >= duration;
		} );

		for ( const auto& popup : this->m_damage_popups )
		{
			const auto projected = game::camera().project( popup.position );
			if ( !game::camera().projection_valid( projected ) ) continue;
			const auto elapsed = std::chrono::duration<float>( now - popup.timestamp ).count( );
			const auto phase = std::clamp( elapsed / duration, 0.0f, 1.0f );
			const auto eased = 1.0f - ( 1.0f - phase ) * ( 1.0f - phase );
			const auto fade = 1.0f - std::clamp( ( phase - 0.55f ) / 0.45f, 0.0f, 1.0f );
			const auto alpha = static_cast<std::uint8_t>( std::clamp(
				static_cast<float>( cfg.damage_color.a ) * fade, 0.0f, 255.0f ) );
			if ( !alpha ) continue;

			auto font = *base_font;
			font.font_size = std::clamp( cfg.damage_size, 8.0f, 28.0f ) *
				( 1.08f - 0.08f * std::min( phase / 0.2f, 1.0f ) );
			const auto text = "-" + std::to_string( popup.damage );
			const auto [ width, height ] = zdraw::measure_text( text, &font );
			const auto x = std::floor( projected.x - width * 0.5f + eased * 5.0f );
			const auto y = std::floor( projected.y - height * 0.5f - eased *
				std::clamp( cfg.damage_rise, 0.0f, 100.0f ) );
			draw_list.add_text( x, y, text, &font,
				zdraw::rgba{ cfg.damage_color.r, cfg.damage_color.g, cfg.damage_color.b, alpha },
				zdraw::text_style::outlined );
		}
	}

	bool bullet_impacts_t::bullet_hit_endpoint( std::uintptr_t entity, const foundation::vec3& start, foundation::vec3& end ) const
	{
		const auto local_matrix = app::context().process.load<foundation::affine3>( entity + SCHEMA( "C_BulletHitModel", "m_matLocal"_id ) );
		const foundation::vec3 local_point{ local_matrix[ 0 ][ 3 ], local_matrix[ 1 ][ 3 ], local_matrix[ 2 ][ 3 ] };
		const auto parent_handle = app::context().process.load<std::uint32_t>( entity + SCHEMA( "C_BulletHitModel", "m_hPlayerParent"_id ) );
		const auto bone_index = app::context().process.load<std::int32_t>( entity + SCHEMA( "C_BulletHitModel", "m_iBoneIndex"_id ) );

		if ( parent_handle && parent_handle != 0xffffffff && bone_index >= 0 && bone_index < 128 && finite_vector( local_point ) )
		{
			const auto parent = game::entity_index().lookup( parent_handle );
			const auto node = parent ? app::context().process.load<std::uintptr_t>( parent + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) ) : 0;
			if ( node )
			{
				const auto model_state = node + SCHEMA( "CSkeletonInstance", "m_modelState"_id );
				const auto bone_cache = app::context().process.load<std::uintptr_t>( model_state + 0x80 );
				const auto bones = game::skeletons().get( bone_cache );
				if ( bones.is_valid( ) )
				{
					const auto bone = bones.bones[ static_cast<std::size_t>( bone_index ) ];
					const auto candidate = bone.position + bone.rotation.apply( local_point );
					if ( plausible_segment( start, candidate ) )
					{
						end = candidate;
						return true;
					}
				}
			}
		}

		const auto node = app::context().process.load<std::uintptr_t>( entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
		if ( node )
		{
			const auto candidate = app::context().process.load<foundation::vec3>( node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
			if ( plausible_segment( start, candidate ) )
			{
				end = candidate;
				return true;
			}
		}

		return false;
	}

	void bullet_impacts_t::add_tracer( const foundation::vec3& start, const foundation::vec3& end, bool exact, float shot_time )
	{
		if ( !plausible_segment( start, end ) )
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now( );

		if ( shot_time != 0.0f )
		{
			for ( auto& tracer : this->m_tracers )
			{
				if ( tracer.shot_time != shot_time )
				{
					continue;
				}
				const auto duplicate = std::any_of( tracer.impacts.begin(), tracer.impacts.end(),
					[ & ]( const foundation::vec3& p ) { return p.distance_sqr( end ) < 1.0f; } );
				if ( !duplicate )
				{
					tracer.impacts.push_back( end );
				}
				return;
			}
		}

		this->m_tracers.push_back( { start, { end }, now, shot_time, exact } );
	}

	void bullet_impacts_t::collect_bullet_hit_models( std::uintptr_t pawn, bool enemy_pawn )
	{
		if ( !pawn )
		{
			return;
		}

		const auto vector_address = pawn + SCHEMA( "C_CSPlayerPawn", "m_vecBulletHitModels"_id );
		const auto vector = app::context().process.load<remote_vector>( vector_address );

		std::uintptr_t data{};
		std::size_t count{};

		if ( vector.size > 0 && vector.size <= 128 && vector.capacity >= vector.size &&
			vector.capacity <= 128 && plausible_pointer( vector.data ) )
		{
			count = static_cast<std::size_t>( vector.size );
			data = vector.data;
		}

		if ( !data || !count )
		{
			return;
		}

		std::array<std::uintptr_t, 128> models{};
		if ( !app::context().process.copy( data, models.data( ), count * sizeof( std::uintptr_t ) ) )
		{
			return;
		}

		for ( std::size_t i = 0; i < count; ++i )
		{
			const auto model = models[ i ];
			if ( !plausible_pointer( model ) || !app::context().process.load<bool>( model + SCHEMA( "C_BulletHitModel", "m_bIsHit"_id ) ) )
			{
				continue;
			}

			const auto created = app::context().process.load<float>( model + SCHEMA( "C_BulletHitModel", "m_flTimeCreated"_id ) );
			if ( !std::isfinite( created ) )
			{
				continue;
			}

			if ( const auto seen = this->m_seen_impacts.find( model );
				seen != this->m_seen_impacts.end( ) && std::abs( seen->second - created ) < 0.0001f )
			{
				continue;
			}

			const auto observed_start = app::context().process.load<foundation::vec3>( model + SCHEMA( "C_BulletHitModel", "m_vecStartPos"_id ) );
			if ( !finite_vector( observed_start ) )
			{
				continue;
			}

			foundation::vec3 end{};
			if ( !this->bullet_hit_endpoint( model, observed_start, end ) )
			{
				continue;
			}

			if ( enemy_pawn )
			{
				const auto parent_handle = app::context().process.load<std::uint32_t>(
					model + SCHEMA( "C_BulletHitModel", "m_hPlayerParent"_id ) );
				const auto parent = parent_handle && parent_handle != 0xffffffffu
					? game::entity_index().lookup( parent_handle ) : 0;
				if ( parent != pawn ) continue;
			}

			foundation::vec3 start{};
			if ( !this->select_pending_shot( end, start, &observed_start ) )
			{
				continue;
			}

			if ( config::general_settings.m_bullet_tracers.enabled )
			{
				this->add_tracer( start, end, true );
			}
			if ( enemy_pawn )
			{
				const auto timestamp = std::chrono::steady_clock::now( );
				this->m_local_victim_evidence[ pawn ] = { end, timestamp };
				this->m_recent_hit_positions[ pawn ] = { end, timestamp };
			}
			this->m_seen_impacts[ model ] = created;
		}
	}

	void bullet_impacts_t::collect_bullet_service_impacts( std::uintptr_t pawn )
	{
		if ( !pawn )
		{
			return;
		}

		const auto services = app::context().process.load<std::uintptr_t>(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_pBulletServices"_id ) );
		if ( !plausible_pointer( services ) )
		{
			return;
		}

		const auto entries = app::context().process.load<remote_vector>( services + 0x50 );

		if ( entries.size < 0 || entries.size > 1000000 ||
			entries.capacity < entries.size || entries.capacity > 4000000 ||
			( entries.size && !plausible_pointer( entries.data ) ) )
		{
			return;
		}

		const auto total = static_cast<std::size_t>( std::max( entries.size, 0 ) );

		const auto impact_key = [ ]( const bullet_service_impact& v ) -> std::uint64_t
		{
			auto bits = [ ]( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
			std::uint64_t h = 1469598103934665603ull;
			for ( const auto u : { bits( v.position.x ), bits( v.position.y ),
				bits( v.position.z ), bits( v.timestamp ) } )
			{
				h = ( h ^ u ) * 1099511628211ull;
			}
			return h;
		};

		if ( this->m_last_bullet_service_count >= 0 && total > 0 )
		{
			const auto tail = app::context().process.load<bullet_service_impact>(
				entries.data + ( total - 1 ) * sizeof( bullet_service_impact ) );
			if ( finite_vector( tail.position ) && std::isfinite( tail.timestamp )
				&& std::isfinite( tail.expiry )
				&& this->m_seen_bullet_service.contains( impact_key( tail ) ) )
			{
				return;
			}
		}

		constexpr std::size_t k_window = 512;
		std::array<bullet_service_impact, k_window> impacts{};
		const auto count = std::min( total, k_window );
		const auto begin_index = total - count;
		if ( count && !app::context().process.copy(
			entries.data + begin_index * sizeof( bullet_service_impact ),
			impacts.data( ), count * sizeof( bullet_service_impact ) ) )
		{
			return;
		}

		if ( this->m_last_bullet_service_count < 0 )
		{
			for ( std::size_t index = 0; index < count; ++index )
			{
				if ( finite_vector( impacts[ index ].position ) )
				{
					this->m_seen_bullet_service.insert( impact_key( impacts[ index ] ) );
				}
			}
			this->m_last_bullet_service_count = 0;
			return;
		}

		struct fresh_impact { std::uint64_t key; bullet_service_impact data; };
		std::array<fresh_impact, k_window> fresh{};
		std::size_t fresh_count{};
		for ( std::size_t index = 0; index < count; ++index )
		{
			const auto& impact = impacts[ index ];
			if ( !finite_vector( impact.position ) ||
				!std::isfinite( impact.timestamp ) || !std::isfinite( impact.expiry ) )
			{
				continue;
			}
			const auto key = impact_key( impact );
			if ( !this->m_seen_bullet_service.insert( key ).second )
			{
				continue;
			}
			fresh[ fresh_count++ ] = { key, impact };
		}

		if ( !fresh_count )
		{
			return;
		}

		std::sort( fresh.begin( ), fresh.begin( ) + fresh_count,
			[ ]( const fresh_impact& a, const fresh_impact& b )
			{ return a.data.timestamp < b.data.timestamp; } );

		foundation::vec3 eye_pos{};
		foundation::vec3 eye_ang{};
		const auto have_eye = game::camera().sample( eye_pos, eye_ang );

		const auto now = std::chrono::steady_clock::now( );
		for ( std::size_t i = 0; i < fresh_count; ++i )
		{
			const auto& impact = fresh[ i ].data;

			pending_shot_t* shot{};
			for ( auto& candidate : this->m_pending_shots )
			{
				if ( candidate.resolved || now - candidate.timestamp > std::chrono::milliseconds( 700 ) )
				{
					continue;
				}
				if ( !shot || candidate.timestamp < shot->timestamp )
				{
					shot = &candidate;
				}
			}

			foundation::vec3 start{};
			if ( shot )
			{
				shot->resolved = true;
				start = shot->start;
			}
			else if ( have_eye )
			{
				start = eye_pos;
			}
			else
			{
				continue;
			}

			this->add_tracer( start, impact.position, true, impact.timestamp );
		}

		if ( this->m_seen_bullet_service.size( ) > 4096 )
		{
			this->m_seen_bullet_service.clear( );
			this->m_last_bullet_service_count = -1;
		}
	}

	void bullet_impacts_t::collect_nonbullet_damage_feedback( )
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto local_pawn = game::local_player().pawn( );
		const auto local_handle = game::local_player().pawn_handle( );
		if ( !plausible_pointer( local_pawn ) || !local_handle )
		{
			this->m_tracked_health.clear( );
			this->m_local_utility.clear( );
			this->m_utility_damage_sources.clear( );
			this->m_pending_nonbullet_damage.clear( );
			return;
		}

		const auto emit = [ & ]( const std::uintptr_t pawn,
			const foundation::vec3& position, const int damage, const bool killed )
		{
			if ( damage <= 0 || !finite_vector( position ) ) return;
			std::erase_if( this->m_pending_server_damage,
				[ & ]( const pending_server_damage_t& pending )
				{
					return pending.pawn == pawn
						&& now - pending.timestamp < std::chrono::milliseconds( 500 );
				} );
			this->emit_confirmed_hit( pawn, position, damage, killed, false );

			auto closest = this->m_action_feedback.end( );
			auto closest_delta = std::chrono::steady_clock::duration{
				std::chrono::milliseconds( 900 ) };
			for ( auto feedback = this->m_action_feedback.begin( );
				feedback != this->m_action_feedback.end( ); ++feedback )
			{
				const auto delta = feedback->timestamp > now
					? feedback->timestamp - now : now - feedback->timestamp;
				if ( delta <= closest_delta && ( feedback->damage > 0
					|| ( killed && feedback->kills > 0 ) ) )
				{
					closest = feedback;
					closest_delta = delta;
				}
			}
			if ( closest != this->m_action_feedback.end( ) )
			{
				closest->damage = std::max( 0, closest->damage - damage );
				if ( killed && closest->kills > 0 ) --closest->kills;
				if ( closest->damage == 0 && closest->kills == 0 )
					this->m_action_feedback.erase( closest );
			}
		};

		static thread_local std::vector<std::uintptr_t> observed{};
		observed.clear( );
		const auto projectiles = game::world().projectiles( );
		if ( projectiles && observed.capacity( ) < projectiles->size( ) )
			observed.reserve( projectiles->size( ) );
		if ( projectiles )
		{
			for ( const auto& projectile : *projectiles )
			{
				if ( projectile.subtype != game::projectile_kind::he_grenade &&
					projectile.subtype != game::projectile_kind::molotov &&
					projectile.subtype != game::projectile_kind::decoy ) continue;
				const auto thrower = game::entity_index().lookup( projectile.thrower_handle );
				if ( projectile.thrower_handle != local_handle && thrower != local_pawn ) continue;
				if ( !finite_vector( projectile.origin ) ) continue;

				observed.push_back( projectile.entity );
				auto& tracked = this->m_local_utility[ projectile.entity ];
				tracked.position = projectile.origin;
				tracked.kind = projectile.subtype;
				tracked.seen = now;
				if ( projectile.subtype == game::projectile_kind::he_grenade &&
					projectile.detonated && !tracked.triggered )
				{
					this->m_utility_damage_sources.push_back( {
						projectile.origin, 520.0f, now + std::chrono::milliseconds( 600 ) } );
					tracked.triggered = true;
				}
				else if ( projectile.subtype == game::projectile_kind::molotov && !tracked.triggered )
				{
					for ( const auto& fire : *projectiles )
					{
						if ( fire.subtype != game::projectile_kind::molotov_fire ||
							!finite_vector( fire.origin ) ||
							fire.origin.distance_sqr( projectile.origin ) > 300.0f * 300.0f ) continue;
						this->m_utility_damage_sources.push_back( {
							fire.origin, 390.0f, now + std::chrono::milliseconds( 8000 ) } );
						tracked.triggered = true;
						break;
					}
				}
			}

			for ( auto it = this->m_local_utility.begin( ); it != this->m_local_utility.end( ); )
			{
				if ( std::ranges::find( observed, it->first ) != observed.end( ) ||
					now - it->second.seen <= std::chrono::milliseconds( 120 ) )
				{
					++it;
					continue;
				}

				if ( !it->second.triggered )
				{
					auto source_position = it->second.position;
					auto radius = 520.0f;
					auto lifetime = std::chrono::milliseconds( 600 );
					if ( it->second.kind == game::projectile_kind::molotov )
					{
						radius = 390.0f;
						lifetime = std::chrono::milliseconds( 8000 );
						float closest = 300.0f * 300.0f;
						for ( const auto& fire : *projectiles )
						{
							if ( fire.subtype != game::projectile_kind::molotov_fire ||
								!finite_vector( fire.origin ) ) continue;
							const auto distance = fire.origin.distance_sqr( source_position );
							if ( distance < closest )
							{
								closest = distance;
								source_position = fire.origin;
							}
						}
					}
					else if ( it->second.kind == game::projectile_kind::decoy )
					{
						radius = 240.0f;
						lifetime = std::chrono::milliseconds( 600 );
					}
					this->m_utility_damage_sources.push_back( {
						source_position, radius, now + lifetime } );
				}
				it = this->m_local_utility.erase( it );
			}
		}

		std::erase_if( this->m_utility_damage_sources,
			[ & ]( const utility_damage_source_t& source ) { return now > source.expires; } );
		std::erase_if( this->m_recent_bullet_hits,
			[ & ]( const auto& entry ) { return now - entry.second > std::chrono::milliseconds( 500 ); } );

		const auto near_local_utility = [ & ]( const foundation::vec3& position )
		{
			return std::ranges::any_of( this->m_utility_damage_sources,
				[ & ]( const utility_damage_source_t& source )
				{
					return position.distance_sqr( source.position ) <= source.radius * source.radius;
				} );
		};
		const auto near_local_activity = [ & ]( const foundation::vec3& position )
		{
			if ( near_local_utility( position ) ) return true;
			return std::ranges::any_of( this->m_local_utility,
				[ & ]( const auto& entry )
				{
					const auto radius = entry.second.kind == game::projectile_kind::decoy
						? 240.0f : 520.0f;
					return position.distance_sqr( entry.second.position ) <= radius * radius;
				} );
		};

		for ( auto it = this->m_pending_nonbullet_damage.begin( );
			it != this->m_pending_nonbullet_damage.end( ); )
		{
			if ( now - it->timestamp > std::chrono::milliseconds( 400 ) ||
				this->m_recent_bullet_hits.contains( it->pawn ) )
			{
				it = this->m_pending_nonbullet_damage.erase( it );
				continue;
			}
			if ( near_local_utility( it->position ) )
			{
				emit( it->pawn, it->position, it->damage, it->killed );
				it = this->m_pending_nonbullet_damage.erase( it );
				continue;
			}
			++it;
		}
		const auto record_drop = [ & ]( std::uintptr_t pawn,
			const foundation::vec3& position, int damage, bool killed )
		{
			if ( damage <= 0 || this->m_recent_bullet_hits.contains( pawn ) ) return;
			if ( near_local_utility( position ) )
			{
				emit( pawn, position, damage, killed );
				return;
			}
			this->m_pending_nonbullet_damage.push_back( {
				pawn, position, damage, killed, now } );
			if ( this->m_pending_nonbullet_damage.size( ) > 32 )
				this->m_pending_nonbullet_damage.erase( this->m_pending_nonbullet_damage.begin( ) );
		};

		const auto players = game::world().players( );
		static thread_local std::vector<std::uint32_t> observed_players{};
		observed_players.clear( );
		if ( players && observed_players.capacity( ) < players->size( ) )
			observed_players.reserve( players->size( ) );
		if ( players )
		{
			for ( const auto& player : *players )
			{
				if ( !game::local_player().is_enemy( player.team )
					|| !player.pawn_handle ) continue;
				auto position = finite_vector( player.collision_center )
					? player.collision_center
					: player.origin + foundation::vec3{ 0.0f, 0.0f, 48.0f };
				if ( !finite_vector( position ) ) continue;
				observed_players.push_back( player.pawn_handle );

				auto previous = this->m_tracked_health.find( player.pawn_handle );
				if ( previous != this->m_tracked_health.end( ) &&
					previous->second.pawn == player.pawn &&
					player.health < previous->second.health )
				{
					record_drop( player.pawn, position,
						previous->second.health - player.health, player.health <= 0 );
				}
				this->m_tracked_health[ player.pawn_handle ] = {
					player.pawn, player.pawn_handle, player.health, position, now };
			}
		}

		for ( auto it = this->m_tracked_health.begin( ); it != this->m_tracked_health.end( ); )
		{
			if ( std::ranges::find( observed_players, it->first )
				!= observed_players.end( ) )
			{
				++it;
				continue;
			}
			if ( now - it->second.seen <= std::chrono::milliseconds( 500 ) &&
				near_local_activity( it->second.position ) &&
				!this->m_recent_bullet_hits.contains( it->second.pawn ) )
			{
				record_drop( it->second.pawn, it->second.position,
					std::max( it->second.health, 1 ), true );
				it = this->m_tracked_health.erase( it );
				continue;
			}
			if ( now - it->second.seen > std::chrono::seconds( 1 ) )
				it = this->m_tracked_health.erase( it );
			else
				++it;
		}
	}

	void bullet_impacts_t::collect_impact_hitmarkers( std::uintptr_t pawn )
	{
		if ( !pawn ) return;

		const auto services = app::context().process.load<std::uintptr_t>(
			pawn + SCHEMA( "C_CSPlayerPawn", "m_pBulletServices"_id ) );
		if ( !plausible_pointer( services ) ) return;

		const auto entries = app::context().process.load<remote_vector>( services + 0x50 );
		if ( entries.size < 0 || entries.size > 1'000'000 ||
			entries.capacity < entries.size || entries.capacity > 4'000'000 ||
			( entries.size && !plausible_pointer( entries.data ) ) )
		{
			return;
		}

		const auto impact_key = [ ]( const bullet_service_impact& value )
		{
			auto bits = [ ]( float f ) { std::uint32_t result; std::memcpy( &result, &f, 4 ); return result; };
			std::uint64_t hash = 1469598103934665603ull;
			for ( const auto value_bits : { bits( value.position.x ), bits( value.position.y ),
				bits( value.position.z ), bits( value.timestamp ) } )
			{
				hash = ( hash ^ value_bits ) * 1099511628211ull;
			}
			return hash;
		};

		const auto total = static_cast<std::size_t>( std::max( entries.size, 0 ) );
		const auto players = game::world().players( );
		const auto pose_frame = game::render_poses().latest( );
		const auto now = std::chrono::steady_clock::now( );
		const auto& feedback_cfg = config::general_settings.m_hitsound;
		const auto emit_damage = [ & ]( const foundation::vec3& position, int damage )
		{
			if ( !feedback_cfg.show_damage || damage <= 0 ) return;
			this->m_damage_popups.push_back( { position, std::clamp( damage, 1, 1000 ), now } );
			if ( this->m_damage_popups.size( ) > 32 ) this->m_damage_popups.erase( this->m_damage_popups.begin( ) );
		};

		static thread_local std::vector<std::uintptr_t> resolved_damage{};
		resolved_damage.clear( );
		if ( resolved_damage.capacity( ) < this->m_pending_damage.size( ) )
			resolved_damage.reserve( this->m_pending_damage.size( ) );
		for ( auto it = this->m_pending_damage.begin( ); it != this->m_pending_damage.end( ); )
		{
			if ( std::ranges::find( resolved_damage, it->pawn )
					!= resolved_damage.end( ) ||
				now - it->timestamp > std::chrono::milliseconds( 300 ) )
			{
				it = this->m_pending_damage.erase( it );
				continue;
			}
			const auto current = players ? std::ranges::find_if( *players,
				[ & ]( const game::player_snapshot& player ) { return player.pawn == it->pawn; } )
				: std::vector<game::player_snapshot>::const_iterator{};
			if ( players && current != players->end( ) && current->health < it->health_before )
			{
				emit_damage( it->position, it->health_before - current->health );
				resolved_damage.push_back( it->pawn );
				it = this->m_pending_damage.erase( it );
				continue;
			}
			++it;
		}
		const auto update_known_health = [ & ]
		{
			if ( !players ) return;
			for ( const auto& player : *players ) this->m_known_health[ player.pawn ] = player.health;
			if ( this->m_known_health.size( ) > 128 ) this->m_known_health.clear( );
		};

		if ( this->m_last_hitmarker_impact_count >= 0 && total > 0 )
		{
			const auto tail = app::context().process.load<bullet_service_impact>(
				entries.data + ( total - 1 ) * sizeof( bullet_service_impact ) );
			if ( finite_vector( tail.position ) && std::isfinite( tail.timestamp ) &&
				std::isfinite( tail.expiry ) && this->m_seen_hitmarker_impacts.contains( impact_key( tail ) ) )
			{
				update_known_health( );
				return;
			}
		}

		constexpr std::size_t window = 512;
		std::array<bullet_service_impact, window> impacts{};
		const auto count = std::min( total, window );
		const auto begin = total - count;
		if ( count && !app::context().process.copy( entries.data + begin * sizeof( bullet_service_impact ),
			impacts.data( ), count * sizeof( bullet_service_impact ) ) )
		{
			update_known_health( );
			return;
		}

		if ( this->m_last_hitmarker_impact_count < 0 )
		{
			for ( std::size_t index = 0; index < count; ++index )
			{
				if ( finite_vector( impacts[ index ].position ) )
				{
					this->m_seen_hitmarker_impacts.insert( impact_key( impacts[ index ] ) );
				}
			}
			this->m_last_hitmarker_impact_count = 0;
			update_known_health( );
			return;
		}

		for ( std::size_t index = 0; index < count; ++index )
		{
			const auto& impact = impacts[ index ];
			if ( !finite_vector( impact.position ) || !std::isfinite( impact.timestamp ) ||
				!std::isfinite( impact.expiry ) ||
				!this->m_seen_hitmarker_impacts.insert( impact_key( impact ) ).second )
			{
				continue;
			}

			const game::player_snapshot* hit_player{};
			if ( players )
			{
				for ( const auto& player : *players )
				{
					if ( player.invulnerable || !game::local_player().is_enemy( player.team ) ) continue;
					const auto* bones = &player.bones;
					if ( pose_frame )
					{
						const auto pose = std::ranges::find_if( pose_frame->players,
							[ & ]( const game::sampled_player_pose& sample ) { return sample.pawn == player.pawn; } );
						if ( pose != pose_frame->players.end( ) && pose->bones.is_valid( ) ) bones = &pose->bones;
					}
					if ( impact_touches_player( impact.position, player, *bones ) )
					{
						hit_player = &player;
						break;
					}
				}
			}

			if ( hit_player && ( this->m_last_hitmarker_shot_time < 0.0f ||
				std::abs( impact.timestamp - this->m_last_hitmarker_shot_time ) > 0.0001f ) )
			{

				this->m_recent_hit_positions[ hit_player->pawn ] = {
					impact.position, now };
				this->m_last_hitmarker_shot_time = impact.timestamp;
			}
		}
		update_known_health( );

		if ( this->m_seen_hitmarker_impacts.size( ) > 4096 )
		{
			this->m_seen_hitmarker_impacts.clear( );
			this->m_last_hitmarker_impact_count = -1;
		}
	}

	void bullet_impacts_t::collect_exact_impacts( )
	{
		const auto now = std::chrono::steady_clock::now( );
		std::erase_if( this->m_pending_shots, [ & ]( const pending_shot_t& shot ) {
			return now - shot.timestamp > std::chrono::milliseconds( 700 );
		} );

		if ( config::general_settings.m_bullet_tracers.enabled )
		{
			this->collect_bullet_service_impacts( game::local_player().pawn( ) );
		}

		const auto needs_hit_model = std::ranges::any_of( this->m_pending_shots,
			[ & ]( const pending_shot_t& shot )
			{
				return !shot.resolved &&
					now - shot.timestamp <= std::chrono::milliseconds( 700 );
			} );
		const auto impact_feedback = config::general_settings.m_hitmarker.enabled
			|| config::general_settings.m_hitsound.enabled
			|| config::general_settings.m_hitsound.show_damage
			|| config::visual_settings.m_chams.on_shot.enabled
			|| config::visual_settings.m_chams.kill_effect.enabled;
		if ( needs_hit_model && ( config::general_settings.m_bullet_tracers.enabled
			|| impact_feedback ) )
		{
			const auto players = game::world().players( );

			if ( impact_feedback )
			{
				for ( const auto& player : *players )
				{
					if ( game::local_player().is_enemy( player.team ) )
						this->collect_bullet_hit_models( player.pawn, true );
				}
			}
			if ( config::general_settings.m_bullet_tracers.enabled )
			{
				this->collect_bullet_hit_models( game::local_player().pawn( ), false );
				for ( const auto& player : *players )
					this->collect_bullet_hit_models( player.pawn, false );
			}
		}

		if ( this->m_seen_impacts.size( ) > 512 )
		{
			this->m_seen_impacts.clear( );
		}
	}

	void bullet_impacts_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::general_settings.m_bullet_tracers;
		const auto& hitmarker_cfg = config::general_settings.m_hitmarker;
		const auto& hitsound_cfg = config::general_settings.m_hitsound;
		const auto impact_feedback = hitmarker_cfg.enabled || hitsound_cfg.enabled
			|| hitsound_cfg.show_damage
			|| config::visual_settings.m_chams.on_shot.enabled
			|| config::visual_settings.m_chams.kill_effect.enabled
			|| config::general_settings.m_event_log.enabled;
		if ( !cfg.enabled && !impact_feedback )
		{
			this->m_tracers.clear( );
			this->m_pending_shots.clear( );
			this->m_seen_impacts.clear( );
			this->m_seen_bullet_service.clear( );
			this->m_seen_hitmarker_impacts.clear( );
			this->m_hit_candidates.clear( );
			this->m_hitmarkers.clear( );
			this->m_pending_damage.clear( );
			this->m_damage_popups.clear( );
			this->m_known_health.clear( );
			this->m_tracked_health.clear( );
			this->m_local_utility.clear( );
			this->m_utility_damage_sources.clear( );
			this->m_pending_nonbullet_damage.clear( );
			this->m_recent_bullet_hits.clear( );
			this->m_server_health.clear( );
			this->m_recent_hit_positions.clear( );
			this->m_local_victim_evidence.clear( );
			this->m_pending_server_damage.clear( );
			this->m_hit_confirmations.clear( );
			this->m_action_feedback.clear( );
			this->m_action_tracking_services = 0;
			this->m_action_tracking_initialized = false;
			this->m_last_bullet_service_count = -1;
			this->m_last_hitmarker_impact_count = -1;
			this->m_last_total_hits = -1;
			this->m_hit_counter_pawn = 0;
			this->m_last_hitmarker_shot_time = -1.0f;
			this->m_last_fire_time = -1.0f;
			this->m_last_shot_render_tick = -1;
			this->m_next_capture = {};
			this->m_has_cached_weapon_ctx = false;
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		if ( now >= this->m_next_capture )
		{
			VESTA_PERF_SCOPE( bullet_feedback_capture );

			this->m_next_capture = now + std::chrono::milliseconds( 8 );
			this->capture_shot( );
			this->collect_exact_impacts( );
			if ( impact_feedback )
			{
				this->collect_impact_hitmarkers( game::local_player().pawn( ) );

				this->poll_server_hits( );
				this->poll_action_tracking( );
				this->collect_server_damage( );
				this->resolve_action_feedback( );
				this->resolve_server_damage( );
				this->collect_nonbullet_damage_feedback( );
			}
			this->resolve_pending_traces( );
		}

		this->render_hitmarkers( draw_list );
		this->render_damage_numbers( draw_list );
		if ( !hitmarker_cfg.enabled )
		{
			this->m_hit_candidates.clear( );
			this->m_hitmarkers.clear( );
		}
		if ( !impact_feedback )
		{
			this->m_last_hitmarker_shot_time = -1.0f;
			this->m_seen_hitmarker_impacts.clear( );
			this->m_last_hitmarker_impact_count = -1;
			this->m_known_health.clear( );
			this->m_tracked_health.clear( );
			this->m_local_utility.clear( );
			this->m_utility_damage_sources.clear( );
			this->m_pending_nonbullet_damage.clear( );
			this->m_recent_bullet_hits.clear( );
			this->m_server_health.clear( );
			this->m_recent_hit_positions.clear( );
			this->m_local_victim_evidence.clear( );
			this->m_pending_server_damage.clear( );
			this->m_hit_confirmations.clear( );
			this->m_action_feedback.clear( );
			this->m_action_tracking_services = 0;
			this->m_action_tracking_initialized = false;
		}
		if ( !hitsound_cfg.show_damage )
		{
			this->m_pending_damage.clear( );
			this->m_damage_popups.clear( );
		}
		if ( !cfg.enabled )
		{
			this->m_tracers.clear( );
			return;
		}

		while ( static_cast<int>( this->m_tracers.size( ) ) > cfg.max_count )
		{
			this->m_tracers.erase( this->m_tracers.begin( ) );
		}

		std::erase_if( this->m_tracers, [ & ]( const tracer_t& tracer ) {
			return std::chrono::duration<float>( now - tracer.timestamp ).count( ) >= cfg.duration;
		} );

		if ( this->m_tracers.empty( ) || !cfg.enabled )
		{
			return;
		}

		const auto& matrix = game::camera().matrix( );
		const auto cam_origin = game::camera().origin( );
		const auto [ scr_w, scr_h ] = zdraw::get_display_size( );

		const auto project = [ & ]( const foundation::vec3& p, foundation::vec2& out ) -> bool
		{
			const auto x = matrix[ 0 ][ 0 ] * p.x + matrix[ 0 ][ 1 ] * p.y + matrix[ 0 ][ 2 ] * p.z + matrix[ 0 ][ 3 ];
			const auto y = matrix[ 1 ][ 0 ] * p.x + matrix[ 1 ][ 1 ] * p.y + matrix[ 1 ][ 2 ] * p.z + matrix[ 1 ][ 3 ];
			const auto w = matrix[ 3 ][ 0 ] * p.x + matrix[ 3 ][ 1 ] * p.y + matrix[ 3 ][ 2 ] * p.z + matrix[ 3 ][ 3 ];
			if ( w < 0.01f )
			{
				return false;
			}
			out.x = scr_w * 0.5f * ( 1.0f + x / w );
			out.y = scr_h * 0.5f * ( 1.0f - y / w );
			return true;
		};

		const auto clip_line = [ & ]( const foundation::vec3& p0, const foundation::vec3& p1,
			foundation::vec2& s0, foundation::vec2& s1 ) -> bool
		{
			auto tf = [ & ]( const foundation::vec3& p, float& x, float& y, float& w )
			{
				x = matrix[ 0 ][ 0 ] * p.x + matrix[ 0 ][ 1 ] * p.y + matrix[ 0 ][ 2 ] * p.z + matrix[ 0 ][ 3 ];
				y = matrix[ 1 ][ 0 ] * p.x + matrix[ 1 ][ 1 ] * p.y + matrix[ 1 ][ 2 ] * p.z + matrix[ 1 ][ 3 ];
				w = matrix[ 3 ][ 0 ] * p.x + matrix[ 3 ][ 1 ] * p.y + matrix[ 3 ][ 2 ] * p.z + matrix[ 3 ][ 3 ];
			};
			float x0{}, y0{}, w0{}, x1{}, y1{}, w1{};
			tf( p0, x0, y0, w0 );
			tf( p1, x1, y1, w1 );
			if ( w0 < 0.01f && w1 < 0.01f ) return false;
			if ( w0 < 0.01f ) { const auto t = ( 0.01f - w0 ) / ( w1 - w0 ); x0 += t * ( x1 - x0 ); y0 += t * ( y1 - y0 ); w0 = 0.01f; }
			else if ( w1 < 0.01f ) { const auto t = ( 0.01f - w1 ) / ( w0 - w1 ); x1 += t * ( x0 - x1 ); y1 += t * ( y0 - y1 ); w1 = 0.01f; }
			s0 = { scr_w * 0.5f * ( 1.0f + x0 / w0 ), scr_h * 0.5f * ( 1.0f - y0 / w0 ) };
			s1 = { scr_w * 0.5f * ( 1.0f + x1 / w1 ), scr_h * 0.5f * ( 1.0f - y1 / w1 ) };
			return true;
		};

		for ( const auto& tracer : this->m_tracers )
		{
			const auto elapsed = std::chrono::duration<float>( now - tracer.timestamp ).count( );
			const auto fade = std::pow( std::clamp( 1.0f - elapsed / cfg.duration, 0.0f, 1.0f ), 2.0f );
			const auto alpha = static_cast<std::uint8_t>( static_cast<float>( cfg.color.a ) * fade );
			if ( alpha == 0 || tracer.impacts.empty( ) ) continue;

			const auto color = zdraw::rgba( cfg.color.r, cfg.color.g, cfg.color.b, alpha );
			const auto glow = zdraw::rgba( cfg.color.r, cfg.color.g, cfg.color.b, static_cast<std::uint8_t>( alpha / 5 ) );
			const auto thickness = std::max( 1.5f, cfg.thickness );

			if ( cfg.draw_cubes )
			{
				for ( const auto& hole : tracer.impacts )
				{
					std::array<foundation::vec2, 8> screen{};
					bool ok = true;
					for ( int corner = 0; corner < 8 && ok; ++corner )
					{
						const foundation::vec3 world = hole + foundation::vec3{
							( corner & 1 ) ? cfg.cube_half : -cfg.cube_half,
							( corner & 2 ) ? cfg.cube_half : -cfg.cube_half,
							( corner & 4 ) ? cfg.cube_half : -cfg.cube_half };
						ok = project( world, screen[ corner ] );
					}
					if ( ok )
					{
						const auto face_alpha = static_cast<std::uint8_t>( std::clamp(
							cfg.cube_face_alpha * fade, 0.0f, 255.0f ) );
						const auto edge_alpha = static_cast<std::uint8_t>( std::clamp(
							static_cast<float>( cfg.cube_edge_color.a ) * fade, 0.0f, 255.0f ) );
						const zdraw::rgba face_color{ cfg.cube_edge_color.r, cfg.cube_edge_color.g,
							cfg.cube_edge_color.b, face_alpha };
						const zdraw::rgba edge_color{ cfg.cube_edge_color.r, cfg.cube_edge_color.g,
							cfg.cube_edge_color.b, edge_alpha };

						const std::array<std::array<int, 4>, 6> faces{
							std::array<int, 4>{ 0, 4, 6, 2 }, std::array<int, 4>{ 1, 3, 7, 5 },
							std::array<int, 4>{ 0, 1, 5, 4 }, std::array<int, 4>{ 2, 6, 7, 3 },
							std::array<int, 4>{ 0, 2, 3, 1 }, std::array<int, 4>{ 4, 5, 7, 6 } };
						const std::array<int, 3> visible_faces{
							cam_origin.x >= hole.x ? 1 : 0,
							cam_origin.y >= hole.y ? 3 : 2,
							cam_origin.z >= hole.z ? 5 : 4 };
						if ( face_alpha > 0 )
						{
							for ( const auto face_index : visible_faces )
							{
								std::array<float, 8> polygon{};
								for ( int vertex = 0; vertex < 4; ++vertex )
								{
									const auto& point = screen[ faces[ face_index ][ vertex ] ];
									polygon[ vertex * 2 ] = point.x;
									polygon[ vertex * 2 + 1 ] = point.y;
								}
								draw_list.add_convex_poly_filled( polygon, face_color );
							}
						}

						static constexpr std::array<std::array<int, 2>, 12> edges{
							std::array<int, 2>{ 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 },
							{ 1, 5 }, { 2, 3 }, { 2, 6 }, { 3, 7 },
							{ 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 } };
						const auto edge_thickness = std::clamp( cfg.thickness * 0.75f, 1.0f, 3.0f );
						for ( const auto& edge : edges )
						{
							const auto& from = screen[ edge[ 0 ] ];
							const auto& to = screen[ edge[ 1 ] ];
							draw_list.add_line( from.x, from.y, to.x, to.y, edge_color, edge_thickness );
						}
					}
				}
			}

			const foundation::vec3* furthest = &tracer.impacts.front( );
			auto best = furthest->distance_sqr( tracer.start );
			for ( const auto& hole : tracer.impacts )
			{
				const auto d = hole.distance_sqr( tracer.start );
				if ( d > best ) { best = d; furthest = &hole; }
			}
			if ( best <= 1.0f ) continue;

			if ( cfg.draw_line )
			{
				constexpr int steps = 24;
				auto prev = tracer.start;
				for ( int i = 1; i <= steps; ++i )
				{
					const auto t = static_cast<float>( i ) / static_cast<float>( steps );
					const auto cur = tracer.start + ( *furthest - tracer.start ) * t;
					const auto seg_dist = ( ( prev + cur ) * 0.5f - cam_origin ).length( );
					const auto k = std::clamp( ( seg_dist - cfg.fade_near ) / ( cfg.fade_far - cfg.fade_near ), 0.0f, 1.0f );
					const auto seg_alpha = static_cast<std::uint8_t>( static_cast<float>( alpha ) * k );
					if ( seg_alpha > 0 )
					{
						foundation::vec2 s0{};
						foundation::vec2 s1{};
						if ( clip_line( prev, cur, s0, s1 ) )
						{
							if ( cfg.bloom )
							{
								const auto seg_glow = zdraw::rgba( cfg.color.r, cfg.color.g, cfg.color.b,
									static_cast<std::uint8_t>( seg_alpha / 2 ) );
								chams::g_renderer.add_2d_bloom_segment( s0.x, s0.y, s1.x, s1.y,
									thickness, std::max( 2.0f, thickness * 2.0f ), seg_glow );
							}
							const auto seg_color = zdraw::rgba( cfg.color.r, cfg.color.g, cfg.color.b, seg_alpha );
							draw_list.add_line( s0.x, s0.y, s1.x, s1.y, seg_color, thickness );
						}
					}
					prev = cur;
				}
			}
		}
	}

}
