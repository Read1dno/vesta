#include <stdafx.hpp>
#include <render/chams/renderer.hpp>
#include <features/visuals/visuals.hpp>

namespace features::visuals {
namespace {

	using steady_clock = std::chrono::steady_clock;

	[[nodiscard]] float seconds_since( steady_clock::time_point then,
		steady_clock::time_point now = steady_clock::now( ) )
	{
		return std::chrono::duration<float>( now - then ).count( );
	}

	[[nodiscard]] bool entity_present(
		const std::vector<game::projectile_snapshot>& projectiles, std::uintptr_t entity )
	{
		return std::ranges::any_of( projectiles,
			[ entity ]( const auto& projectile ) { return projectile.entity == entity; } );
	}

	void draw_square_marker( zdraw::draw_list& output, const foundation::vec2& center,
		float size, const zdraw::rgba& color )
	{
		const auto half = size * 0.5f;
		output.add_rect_filled( center.x - half - 1.0f, center.y - half - 1.0f,
			size + 2.0f, size + 2.0f, { 0, 0, 0, color.a } );
		output.add_rect_filled( center.x - half, center.y - half, size, size, color );
	}

}

	void grenade_prediction_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& settings = config::general_settings.m_grenades;
		if ( !settings.enabled )
			return;

		const auto now = steady_clock::now( );
		const auto collision_ready = game::collision().valid( );
		if ( collision_ready && now - m_last_flight_update >= std::chrono::milliseconds( 16 ) )
		{
			m_last_flight_update = now;
			reconcile_live_projectiles( );
		}

		const auto live_projectiles = game::world().projectiles( );
		std::erase_if( m_in_flight, [ & ]( const in_flight_grenade& flight )
			{
				return flight.detonated && seconds_since( flight.detonate_time, now ) > 0.5f
					&& !entity_present( *live_projectiles, flight.entity );
			} );

		for ( auto& flight : m_in_flight )
		{
			if ( !flight.traj.valid )
				continue;

			if ( !flight.detonated && seconds_since( flight.throw_time, now ) >= flight.traj.duration )
			{
				flight.detonated = true;
				flight.detonate_time = now;
			}

			const auto opacity = flight.detonated
				? std::clamp( 1.0f - seconds_since( flight.detonate_time, now ) / 0.5f, 0.0f, 1.0f )
				: 1.0f;
			if ( opacity > 0.0f )
				draw_path( draw_list, flight.traj, opacity );
		}

		const auto& weapon = simulation::ballistics().ctx( );
		const auto pin_held = weapon.valid && weapon.weapon
			&& weapon.weapon_type == game::rules::equipment_class::throwable
			&& app::context().process.load<bool>(
				weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
		if ( m_was_holding && !pin_held )
			m_last_throw_time = now;
		m_was_holding = pin_held;

		if ( !collision_ready || !preview_allowed( ) )
		{
			m_preview.valid = false;
			return;
		}

		if ( !m_preview.valid || now - m_last_preview_update >= std::chrono::milliseconds( 16 ) )
		{
			m_last_preview_update = now;
			refresh_weapon_profile( );
			foundation::vec3 origin{}, velocity{};
			sample_throw( origin, velocity );
			m_preview = m_trajectory_engine.predict( origin, velocity, m_weapon_id );
		}

		if ( m_preview.valid )
			draw_path( draw_list, m_preview, 1.0f );
	}

	bool grenade_prediction_t::preview_allowed( ) const
	{
		const auto& weapon = simulation::ballistics().ctx( );
		if ( !weapon.valid || !weapon.weapon || !weapon.weapon_vdata
			|| weapon.weapon_type != game::rules::equipment_class::throwable )
		{
			return false;
		}

		const auto pin_held = app::context().process.load<bool>(
			weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
		if ( !pin_held && seconds_since( m_last_throw_time ) < throw_cooldown )
			return false;

		return app::context().process.load<float>(
			weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_id ) ) <= 0.0f;
	}

	void grenade_prediction_t::refresh_weapon_profile( )
	{
		const auto weapon_data = simulation::ballistics().ctx().weapon_vdata;
		if ( !weapon_data || weapon_data == m_weapon_vdata )
			return;

		m_weapon_vdata = weapon_data;
		m_throw_velocity = std::clamp( app::context().process.load<float>(
			weapon_data + SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_id ) ), 1.0f, 10000.0f );
		const auto name_address = app::context().process.load<std::uintptr_t>(
			weapon_data + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
		if ( !name_address )
		{
			m_weapon_id = 0;
			return;
		}

		char name[ 64 ]{};
		if ( !app::context().process.copy( name_address, name, sizeof( name ) - 1 ) )
		{
			m_weapon_id = 0;
			return;
		}
		m_weapon_id = identity::of( name );
	}

	void grenade_prediction_t::sample_throw( foundation::vec3& origin, foundation::vec3& velocity )
	{
		const auto& weapon = simulation::ballistics().ctx( );
		auto strength = 1.0f;
		if ( app::context().process.load<bool>(
			weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) ) )
		{
			strength = std::clamp( app::context().process.load<float>(
				weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_id ) ), 0.0f, 1.0f );
			if ( std::abs( strength - 0.5f ) <= 0.1f )
				strength = 0.5f;
		}

		foundation::vec3 direct_origin{}, direct_angles{};
		const auto direct_sample = game::camera().sample( direct_origin, direct_angles );
		auto angles = direct_sample ? direct_angles : game::camera().angles( );
		if ( angles.x > 90.0f )
			angles.x -= 360.0f;
		else if ( angles.x < -90.0f )
			angles.x += 360.0f;
		angles.x -= ( 90.0f - std::abs( angles.x ) ) / 9.0f;

		foundation::vec3 forward{}, right{}, up{};
		angles.to_directions( &forward, &right, &up );
		auto eye = direct_sample ? direct_origin : game::camera().origin( );
		eye.z += strength * 12.0f - 12.0f;
		const auto obstruction = game::collision().trace_ray( eye, eye + forward * 22.0f );
		origin = obstruction.hit ? obstruction.end_pos - forward * 6.0f : eye + forward * 16.0f;

		const auto pawn_velocity = app::context().process.load<foundation::vec3>(
			game::local_player().pawn() + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		const auto base_speed = std::clamp( m_throw_velocity * 0.9f, 15.0f, 750.0f );
		velocity = forward * ( ( strength * 0.7f + 0.3f ) * base_speed ) + pawn_velocity * 1.25f;
	}

	void grenade_prediction_t::reconcile_live_projectiles( )
	{
		const auto local_handle = game::local_player().pawn_handle( );
		if ( !local_handle )
			return;

		const auto& settings = config::general_settings.m_grenades;
		const auto projectiles = game::world().projectiles( );
		const auto now = steady_clock::now( );
		static thread_local std::vector<std::uintptr_t> observed{};
		observed.clear( );
		if ( observed.capacity( ) < projectiles->size( ) )
			observed.reserve( projectiles->size( ) );

		for ( const auto& projectile : *projectiles )
		{
			if ( settings.local_only && projectile.thrower_handle != local_handle )
				continue;
			observed.push_back( projectile.entity );

			const auto existing = std::ranges::find( m_in_flight, projectile.entity,
				&in_flight_grenade::entity );
			if ( existing != m_in_flight.end( ) )
			{
				existing->last_seen = now;
				if ( !existing->detonated && projectile.effect_tick_begin > 0 )
				{
					existing->detonated = true;
					existing->detonate_time = now;
				}
				continue;
			}

			if ( projectile.effect_tick_begin > 0 )
				continue;
			const auto initial_position = app::context().process.load<foundation::vec3>(
				projectile.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_id ) );
			const auto initial_velocity = app::context().process.load<foundation::vec3>(
				projectile.entity + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_id ) );
			if ( initial_velocity.length_sqr( ) < 1.0f )
				continue;

			const auto weapon_id = weapon_id_for( projectile.subtype );
			m_in_flight.push_back( in_flight_grenade{
				.entity = projectile.entity,
				.weapon_id = weapon_id,
				.traj = m_trajectory_engine.predict( initial_position, initial_velocity, weapon_id ),
				.throw_time = now,
				.last_seen = now
			} );
		}

		for ( auto& flight : m_in_flight )
		{
			if ( !flight.detonated
				&& std::ranges::find( observed, flight.entity ) == observed.end( )
				&& seconds_since( flight.last_seen, now ) >= missing_grace )
			{
				flight.detonated = true;
				flight.detonate_time = now;
			}
		}
	}

	std::uintptr_t grenade_prediction_t::weapon_id_for( game::projectile_kind type )
	{
		using kind = game::projectile_kind;
		switch ( type )
		{
		case kind::he_grenade:    return "weapon_hegrenade"_id;
		case kind::flashbang:     return "weapon_flashbang"_id;
		case kind::smoke_grenade: return "weapon_smokegrenade"_id;
		case kind::molotov:       return "weapon_molotov"_id;
		case kind::decoy:         return "weapon_decoy"_id;
		default:                   return 0;
		}
	}

	void grenade_prediction_t::draw_path( zdraw::draw_list& draw_list,
		const grenade_path& path, float opacity ) const
	{
		if ( !path.valid || path.points.size( ) < 2 )
			return;

		const auto& settings = config::general_settings.m_grenades;
		const auto& color = settings.color;
		for ( std::size_t index = 1; index < path.points.size( ); ++index )
		{
			const auto from = game::camera().project( path.points[ index - 1 ] );
			const auto to = game::camera().project( path.points[ index ] );
			if ( !game::camera().projection_valid( from ) || !game::camera().projection_valid( to ) )
				continue;

			const auto progress = static_cast<float>( index - 1 ) / ( path.points.size( ) - 1 );
			const auto fade = opacity * ( 1.0f - progress * 0.6f );
			if ( settings.bloom )
			{
				const auto bloom_alpha = static_cast<std::uint8_t>( std::clamp(
					fade * settings.bloom_color.a, 0.0f, 255.0f ) );
				chams::g_renderer.add_2d_bloom_segment( from.x, from.y, to.x, to.y,
					settings.thickness, settings.bloom_radius,
					{ settings.bloom_color.r, settings.bloom_color.g,
						settings.bloom_color.b, bloom_alpha } );
			}
			const auto alpha = static_cast<std::uint8_t>( std::clamp(
				fade * color.a, 0.0f, 255.0f ) );
			draw_list.add_line( from.x, from.y, to.x, to.y,
				{ color.r, color.g, color.b, alpha }, settings.thickness );
		}

		if ( settings.show_bounces )
		{
			const auto bounce_color = zdraw::rgba{ settings.bounce_color.r,
				settings.bounce_color.g, settings.bounce_color.b,
				static_cast<std::uint8_t>( opacity * settings.bounce_color.a ) };
			for ( const auto& bounce : path.bounces )
			{
				const auto screen = game::camera().project( bounce );
				if ( game::camera().projection_valid( screen ) )
					draw_square_marker( draw_list, screen,
						settings.bounce_size, bounce_color );
			}
		}
		if ( settings.show_endpoint )
		{
			const auto endpoint = game::camera().project( path.end_pos );
			if ( game::camera().projection_valid( endpoint ) )
			{
				const auto endpoint_color = zdraw::rgba{ settings.endpoint_color.r,
					settings.endpoint_color.g, settings.endpoint_color.b,
					static_cast<std::uint8_t>( opacity * settings.endpoint_color.a ) };
				draw_square_marker( draw_list, endpoint,
					settings.endpoint_size, endpoint_color );
			}
		}
	}

}
