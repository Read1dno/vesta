#include <stdafx.hpp>
#include <core/input/bindings.hpp>
#include <features/misc/misc.hpp>

namespace features::misc {
	namespace {
		[[nodiscard]] bool game_has_input_focus( )
		{
			const auto foreground = ::GetForegroundWindow( );
			const auto root = foreground ? ::GetAncestor( foreground, GA_ROOT ) : nullptr;
			DWORD process_id{};
			if ( root ) ::GetWindowThreadProcessId( root, &process_id );
			return process_id != 0
				&& process_id == app::context().process.process_id( );
		}
	}

	void bhop_t::tick( )
	{
		auto& bunny_cfg = config::general_settings.m_bunny_hop;
		auto& edge_cfg = config::general_settings.m_edge_jump;
		const auto now = std::chrono::steady_clock::now( );
		const auto release = [ this, now ]( )
		{
			if ( this->m_jump_down )
			{
				app::context().input.key( this->m_owned_jump_key, false );
				this->m_jump_down = false;
				this->m_owned_jump_key = 0;
				this->m_last_transition = now;
			}
		};
		if ( !bunny_cfg.enabled && !edge_cfg.enabled )
		{

			if ( this->m_gate_requested )
			{
				const auto jump_key = this->m_jump_key;
				const auto game_active = game_has_input_focus( );
				const auto player_ready = game::local_player().pawn( )
					&& game::local_player().alive( );
				const auto restore = game_active && player_ready
					&& !app::context().menu.is_open( )
					&& app::context().input.key_gate_held( );
				app::context().input.set_key_gate( jump_key, false );
				if ( restore && jump_key ) app::context().input.key( jump_key, true );
				this->m_gate_requested = false;
			}
			release( );
			this->m_edge_was_on_ground = false;
			this->m_edge_armed = false;
			return;
		}

		const auto activation_key = static_cast<std::uint16_t>( std::clamp(
			bunny_cfg.activation_key, 0, 255 ) );
		if ( activation_key != this->m_activation_key
			|| now >= this->m_next_binding_refresh )
		{
			this->m_activation_key = activation_key;
			this->m_next_binding_refresh = now + std::chrono::seconds( 1 );
			const auto binding = game::input_bindings().resolve(
				game::input_action::jump, activation_key );
			const auto resolved_key = binding.device == game::input_device::keyboard
				? binding.virtual_key : std::uint16_t{};
			if ( this->m_jump_key != resolved_key )
			{
				app::context().input.set_key_gate( 0, false );
				release( );
				this->m_jump_key = resolved_key;
			}
		}

		const auto game_active = game_has_input_focus( );
		const auto pawn = game::local_player().pawn( );
		const auto player_ready = pawn && game::local_player().alive( );
		const auto menu_open = app::context().menu.is_open( );
		const auto jump_key = this->m_jump_key;
		const auto activation_is_jump = activation_key != 0
			&& activation_key == jump_key;
		const auto activation_held = activation_key != 0
			&& ( ::GetAsyncKeyState( activation_key ) & 0x8000 ) != 0;
		const auto gate_requested = game_active && !menu_open && player_ready
			&& bunny_cfg.enabled && jump_key != 0
			&& ( activation_is_jump || activation_held );
		const auto restore_physical_jump = this->m_gate_requested
			&& !gate_requested && game_active && !menu_open && player_ready
			&& app::context().input.key_gate_held( );
		app::context().input.set_key_gate( jump_key, gate_requested );
		if ( restore_physical_jump && jump_key )
			app::context().input.key( jump_key, true );
		this->m_gate_requested = gate_requested;

		const auto bunny_active = gate_requested
			&& app::context().input.key_gate_ready( )
			&& ( activation_is_jump
				? app::context().input.key_gate_held( ) : activation_held );
		const auto edge_active = game_active && !menu_open && player_ready
			&& edge_cfg.enabled && edge_cfg.activation_key > 0 && jump_key != 0
			&& ( ::GetAsyncKeyState( edge_cfg.activation_key ) & 0x8000 );

		if ( !player_ready || ( !bunny_active && !edge_active ) )
		{
			release( );
			this->m_edge_was_on_ground = false;
			this->m_edge_armed = false;
			return;
		}

		std::uint32_t flags{}, ground_entity{};
		if ( !app::context().process.copy( pawn
				+ SCHEMA( "C_BaseEntity", "m_fFlags"_id ),
				&flags, sizeof( flags ) )
			|| !app::context().process.copy( pawn
				+ SCHEMA( "C_BaseEntity", "m_hGroundEntity"_id ),
				&ground_entity, sizeof( ground_entity ) ) )
		{
			release( );
			this->m_edge_was_on_ground = false;
			this->m_edge_armed = false;
			return;
		}
		const auto flags_on_ground = ( flags & 1u ) != 0u;
		const auto on_ground = flags_on_ground || ground_entity != 0xffffffffu;
		const auto since_transition = now - this->m_last_transition;

		constexpr auto k_airborne_min_hold = std::chrono::milliseconds( 16 );
		constexpr auto k_bunny_tap_interval = std::chrono::milliseconds( 16 );

		if ( bunny_active )
		{
			release( );
			this->m_edge_was_on_ground = on_ground;
			this->m_edge_armed = false;

			if ( !flags_on_ground )
				return;

			float simulation_time{};
			if ( !app::context().process.copy( pawn
					+ SCHEMA( "C_BaseEntity", "m_flSimulationTime"_id ),
					&simulation_time, sizeof( simulation_time ) )
				|| !std::isfinite( simulation_time )
				|| simulation_time == this->m_last_bunny_simulation_time
				|| now - this->m_last_bunny_tap <= k_bunny_tap_interval )
			{
				return;
			}

			const std::array tap{
				platform::windows::input_gateway::key_transition{ jump_key, true },
				platform::windows::input_gateway::key_transition{ jump_key, false },
			};
			app::context().input.keys( tap );
			this->m_last_bunny_simulation_time = simulation_time;
			this->m_last_bunny_tap = now;
			return;
		}

		if ( this->m_jump_down && since_transition >= k_airborne_min_hold )
		{
			app::context().input.key( this->m_owned_jump_key, false );
			this->m_jump_down = false;
			this->m_owned_jump_key = 0;
			this->m_last_transition = now;
		}

		if ( on_ground && !this->m_edge_was_on_ground )
			this->m_edge_armed = true;

		auto trigger_edge_jump = false;
		if ( this->m_edge_armed && !this->m_jump_down )
		{
			const auto sample_motion = [ & ]( foundation::vec3& origin,
				foundation::vec3& velocity )
			{
				std::uintptr_t node{};
				return app::context().process.copy( pawn
						+ SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ),
						&node, sizeof( node ) ) && node
					&& app::context().process.copy( node
						+ SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ),
						&origin, sizeof( origin ) )
					&& app::context().process.copy( pawn
						+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
						&velocity, sizeof( velocity ) );
			};
			if ( on_ground )
			{
				foundation::vec3 origin{}, velocity{};
				trigger_edge_jump = sample_motion( origin, velocity )
					&& this->will_leave_ground( pawn, origin, velocity, true );
			}
			else if ( this->m_edge_was_on_ground )
			{
				foundation::vec3 origin{}, velocity{};
				trigger_edge_jump = sample_motion( origin, velocity )
					&& this->will_leave_ground( pawn, origin, velocity, false );
			}
		}

		if ( trigger_edge_jump )
		{
			if ( app::context().input.key( jump_key, true ) )
			{
				this->m_owned_jump_key = jump_key;
				this->m_jump_down = true;
				this->m_last_transition = now;
				this->m_edge_armed = false;
			}
		}
		this->m_edge_was_on_ground = on_ground;
	}

	bool bhop_t::will_leave_ground( std::uintptr_t pawn,
		const foundation::vec3& origin, const foundation::vec3& velocity,
		bool require_current_support ) const
	{
		if ( !game::collision().valid( ) || velocity.length_2d( ) < 12.0f
			|| velocity.z > 40.0f || !std::isfinite( origin.x )
			|| !std::isfinite( origin.y ) || !std::isfinite( origin.z ) )
		{
			return false;
		}

		const auto collision = pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_id );
		foundation::vec3 mins{}, maxs{};
		if ( !app::context().process.copy( collision
				+ SCHEMA( "CCollisionProperty", "m_vecMins"_id ),
				&mins, sizeof( mins ) )
			|| !app::context().process.copy( collision
				+ SCHEMA( "CCollisionProperty", "m_vecMaxs"_id ),
				&maxs, sizeof( maxs ) ) ) return false;
		const auto extent_x = std::clamp( std::min( std::abs( mins.x ), std::abs( maxs.x ) ) - 1.0f, 6.0f, 20.0f );
		const auto extent_y = std::clamp( std::min( std::abs( mins.y ), std::abs( maxs.y ) ) - 1.0f, 6.0f, 20.0f );

		const auto has_walkable_support = [ & ]( const foundation::vec3& base )
		{
			constexpr auto k_step_up = 20.0f;
			constexpr auto k_step_down = 24.0f;
			for ( int y = -2; y <= 2; ++y )
			{
				for ( int x = -2; x <= 2; ++x )
				{
					const foundation::vec3 offset{
						extent_x * static_cast<float>( x ) * 0.5f,
						extent_y * static_cast<float>( y ) * 0.5f,
						0.0f };
					const auto start = base + offset + foundation::vec3{ 0.0f, 0.0f, k_step_up };
					const auto end = base + offset - foundation::vec3{ 0.0f, 0.0f, k_step_down };
					const auto trace = game::collision().trace_ray( start, end );
					if ( trace.hit && trace.normal.z >= 0.55f )
						return true;
				}
			}
			return false;
		};

		if ( require_current_support && !has_walkable_support( origin ) )
			return false;

		auto horizontal_velocity = velocity;
		horizontal_velocity.z = 0.0f;
		const auto predicted_origin = origin + horizontal_velocity * game::rules::simulation_step;
		if ( has_walkable_support( predicted_origin ) )
			return false;

		const auto speed = horizontal_velocity.length_2d( );
		const auto direction = horizontal_velocity / speed;
		constexpr std::array<float, 4> k_bridge_probes{ 4.0f, 8.0f, 12.0f, 16.0f };
		for ( const auto distance : k_bridge_probes )
		{
			if ( has_walkable_support( predicted_origin + direction * distance ) )
				return false;
		}
		return true;
	}

}
