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

	[[nodiscard]] constexpr std::uintptr_t weapon_id_for_item_definition(
		const std::uint16_t item_definition )
	{
		switch ( item_definition )
		{
		case 43: return "weapon_flashbang"_id;
		case 44: return "weapon_hegrenade"_id;
		case 45: return "weapon_smokegrenade"_id;
		case 46: return "weapon_molotov"_id;
		case 47: return "weapon_decoy"_id;
		case 48: return "weapon_incgrenade"_id;
		default: return 0;
		}
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

		const auto weapon = sample_held_grenade( );
		const auto pin_held = weapon.valid
			&& app::context().process.load<bool>(
				weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
		if ( m_was_holding && !pin_held )
			m_last_throw_time = now;
		m_was_holding = pin_held;

		if ( !collision_ready || !preview_allowed( weapon ) )
		{
			m_preview.valid = false;
			m_display_preview.valid = false;
			m_last_preview_blend = {};
			return;
		}

		if ( !m_preview.valid || now - m_last_preview_update >= std::chrono::milliseconds( 8 ) )
		{
			m_last_preview_update = now;
			refresh_weapon_profile( weapon );
			foundation::vec3 origin{}, velocity{};
			sample_throw( weapon, origin, velocity );
			m_preview = m_trajectory_engine.predict( origin, velocity, m_weapon_id );
		}

		if ( m_preview.valid )
		{
			const auto topology_matches = m_display_preview.valid
				&& m_display_preview.points.size( ) == m_preview.points.size( )
				&& m_display_preview.bounces.size( ) == m_preview.bounces.size( );
			if ( !topology_matches )
			{
				m_display_preview = m_preview;
			}
			else
			{
				const auto dt = m_last_preview_blend == steady_clock::time_point{}
					? 1.0f / 120.0f
					: std::clamp( seconds_since( m_last_preview_blend, now ), 0.0f, 0.05f );
				const auto blend = 1.0f - std::exp( -dt / 0.022f );
				for ( std::size_t index = 0; index < m_preview.points.size( ); ++index )
					m_display_preview.points[ index ] +=
						( m_preview.points[ index ] - m_display_preview.points[ index ] ) * blend;
				for ( std::size_t index = 0; index < m_preview.bounces.size( ); ++index )
					m_display_preview.bounces[ index ] +=
						( m_preview.bounces[ index ] - m_display_preview.bounces[ index ] ) * blend;
				m_display_preview.end_pos +=
					( m_preview.end_pos - m_display_preview.end_pos ) * blend;
				m_display_preview.duration = m_preview.duration;
				m_display_preview.end_tick = m_preview.end_tick;
				m_display_preview.valid = true;
			}
			m_last_preview_blend = now;
			draw_path( draw_list, m_display_preview, 1.0f );
		}
	}

	grenade_prediction_t::held_grenade_snapshot grenade_prediction_t::sample_held_grenade( )
	{
		held_grenade_snapshot result{};
		result.weapon = game::local_player().weapon( );
		result.weapon_vdata = game::local_player().weapon_vdata( );
		if ( !result.weapon || !result.weapon_vdata )
			return result;

		result.item_definition = app::context().process.load<std::uint16_t>(
			result.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
			+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
			+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ) );
		result.valid = weapon_id_for_item_definition( result.item_definition ) != 0
			&& game::local_player().weapon( ) == result.weapon
			&& game::local_player().weapon_vdata( ) == result.weapon_vdata;
		return result;
	}

	bool grenade_prediction_t::preview_allowed( const held_grenade_snapshot& weapon ) const
	{
		if ( !weapon.valid )
			return false;

		const auto pin_held = app::context().process.load<bool>(
			weapon.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
		if ( !pin_held && seconds_since( m_last_throw_time ) < throw_cooldown )
			return false;
		return true;
	}

	void grenade_prediction_t::refresh_weapon_profile( const held_grenade_snapshot& weapon )
	{
		const auto weapon_data = weapon.weapon_vdata;
		const auto definition_id = weapon_id_for_item_definition( weapon.item_definition );
		if ( !weapon_data || ( weapon_data == m_weapon_vdata && m_weapon_id
			&& ( !definition_id || definition_id == m_weapon_id )
			&& std::isfinite( m_throw_velocity ) && m_throw_velocity > 1.0f ) )
			return;

		auto throw_velocity = app::context().process.load<float>(
			weapon_data + SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_id ) );
		if ( !std::isfinite( throw_velocity ) || throw_velocity <= 1.0f )
			throw_velocity = 750.0f;

		auto resolved_id = definition_id;
		if ( !resolved_id )
		{
			const auto name_address = app::context().process.load<std::uintptr_t>(
				weapon_data + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
			char name[ 64 ]{};
			if ( !name_address || !app::context().process.copy(
				name_address, name, sizeof( name ) - 1 ) )
				return;
			resolved_id = identity::of( name );
		}
		m_weapon_vdata = weapon_data;
		m_throw_velocity = std::clamp( throw_velocity, 1.0f, 10000.0f );
		m_weapon_id = resolved_id;
	}

	void grenade_prediction_t::sample_throw( const held_grenade_snapshot& weapon,
		foundation::vec3& origin, foundation::vec3& velocity )
	{
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
		const auto obstruction = game::collision().sweep_hull(
			eye, eye + forward * 22.0f, simulation::grenade_collision_half_extents );
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
			const auto effect_started = projectile.detonated
				|| ( projectile.subtype == game::projectile_kind::smoke_grenade
					? projectile.smoke_active && !projectile.in_flight
					: projectile.effect_tick_begin > 0 && !projectile.in_flight );

			const auto existing = std::ranges::find( m_in_flight, projectile.entity,
				&in_flight_grenade::entity );
			if ( existing != m_in_flight.end( ) )
			{
				existing->last_seen = now;
				if ( !existing->detonated && effect_started )
				{
					existing->detonated = true;
					existing->detonate_time = now;
				}
				continue;
			}

			if ( effect_started )
				continue;
			if ( !projectile.launch_valid )
				continue;
			const auto initial_position = projectile.initial_position;
			const auto initial_velocity = projectile.initial_velocity;

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
