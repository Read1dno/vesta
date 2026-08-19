#include <stdafx.hpp>

namespace game {
	namespace {
		struct pawn_candidate
		{
			std::uintptr_t address{};
			std::uint32_t handle{};
			std::int32_t health{};
			std::int32_t team{};
		};

		[[nodiscard]] std::uint32_t observer_target_handle(
			std::uintptr_t controller )
		{
			const auto observer_handle = app::context().process.load<std::uint32_t>(
				controller + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_id ) );
			const auto observer_pawn = observer_handle
				? entity_index( ).lookup( observer_handle ) : 0;
			const auto observer_services = observer_pawn
				? app::context().process.load<std::uintptr_t>( observer_pawn
					+ SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_id ) )
				: 0;
			return observer_services
				? app::context().process.load<std::uint32_t>( observer_services
					+ SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_id ) )
				: 0u;
		}
	}

	local_pawn_binding resolve_local_pawn( std::uintptr_t controller )
	{
		if ( !controller )
			return {};

		const auto& process = app::context().process;
		const auto controlling_bot = process.load<bool>( controller
			+ SCHEMA( "CCSPlayerController", "m_bControllingBot"_id ) );
		const auto controller_reports_alive = process.load<bool>( controller
			+ SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_id ) );

		std::array<pawn_candidate, 6> candidates{};
		auto candidate_count = std::size_t{};
		const auto add_candidate = [&]( std::uint32_t handle )
		{
			if ( !handle || handle == 0xffffffffu )
				return;

			const auto address = entity_index( ).lookup( handle );
			if ( !address )
				return;
			for ( auto index = std::size_t{}; index < candidate_count; ++index )
			{
				if ( candidates[index].address == address )
					return;
			}

			const auto health = process.load<std::int32_t>( address
				+ SCHEMA( "C_BaseEntity", "m_iHealth"_id ) );
			const auto team = process.load<std::int32_t>( address
				+ SCHEMA( "C_BaseEntity", "m_iTeamNum"_id ) );
			if ( health < 0 || ( team != 2 && team != 3 )
				|| candidate_count >= candidates.size( ) )
			{
				return;
			}
			candidates[candidate_count++] = { address, handle, health, team };
		};

		const auto player_handle = process.load<std::uint32_t>( controller
			+ SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_id ) );
		const auto base_handle = process.load<std::uint32_t>( controller
			+ SCHEMA( "CBasePlayerController", "m_hPawn"_id ) );

		if ( controlling_bot )
		{
			add_candidate( base_handle );
			add_candidate( player_handle );
		}
		else
		{
			add_candidate( player_handle );
			add_candidate( base_handle );
		}

		const auto original_controller_handle = process.load<std::uint32_t>( controller
			+ SCHEMA( "CCSPlayerController", "m_hOriginalControllerOfCurrentPawn"_id ) );
		const auto original_controller = original_controller_handle
			&& original_controller_handle != 0xffffffffu
			? entity_index( ).lookup( original_controller_handle ) : 0;
		if ( original_controller && original_controller != controller )
		{
			add_candidate( process.load<std::uint32_t>( original_controller
				+ SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_id ) ) );
			add_candidate( process.load<std::uint32_t>( original_controller
				+ SCHEMA( "CBasePlayerController", "m_hPawn"_id ) ) );
		}

		if ( controlling_bot )
			add_candidate( observer_target_handle( controller ) );

		const pawn_candidate* selected{};
		if ( controlling_bot || controller_reports_alive )
		{
			for ( auto index = std::size_t{}; index < candidate_count; ++index )
			{
				if ( candidates[index].health > 0 )
				{
					selected = &candidates[index];
					break;
				}
			}
		}
		if ( !selected && candidate_count )
			selected = &candidates.front( );
		if ( !selected )
			return {};

		return { selected->address, selected->handle, selected->health,
			selected->team, controlling_bot };
	}

	void local_state::update( )
	{
		const auto controller_address = app::context().process.load<std::uintptr_t>(
			app::context().addresses.local_player_controller );
		if ( !controller_address ) return reset( );

		const auto binding = resolve_local_pawn( controller_address );
		if ( !binding ) return reset( );

		const auto pawn_address = binding.pawn;
		const auto team = binding.team;
		const auto health = binding.health;
		const auto alive_now = health > 0;
		auto view_team = team;
		auto observer = std::uintptr_t{};
		auto crosshair = std::int32_t{};
		auto active_weapon = std::uintptr_t{};
		auto weapon_data = std::uintptr_t{};
		auto active_weapon_type = std::uint32_t{};
		auto flash_alpha = 0.0f;

		if ( alive_now )
		{
			crosshair = app::context().process.load<std::int32_t>( pawn_address +
				SCHEMA( "C_CSPlayerPawn", "m_iIDEntIndex"_id ) );
			const auto combat = config::combat_settings.get(
				game::local_player().weapon_type( ) );
			if ( config::visual_settings.m_no_flash.enabled
				|| combat.aimbot.checks.flashed
				|| combat.triggerbot.checks.flashed )
			{
				flash_alpha = app::context().process.load<float>( pawn_address +
					SCHEMA( "C_CSPlayerPawnBase", "m_flFlashOverlayAlpha"_id ) );
			}
			const auto weapon_services = app::context().process.load<std::uintptr_t>(
				pawn_address + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_id ) );
			const auto weapon_handle = weapon_services ?
				app::context().process.load<std::uint32_t>( weapon_services +
					SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_id ) ) : 0;
			if ( weapon_handle && weapon_handle != 0xffffffffu )
				active_weapon = entity_index( ).lookup( weapon_handle );
			if ( active_weapon )
			{
				weapon_data = app::context().process.load<std::uintptr_t>( active_weapon +
					SCHEMA( "C_BaseEntity", "m_nSubclassID"_id ) + 0x8 );
				if ( weapon_data )
					active_weapon_type = app::context().process.load<std::uint32_t>( weapon_data +
						SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_id ) );
			}
		}
		else
		{
			const auto observer_handle = app::context().process.load<std::uint32_t>(
				controller_address + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_id ) );
			const auto observer_pawn = observer_handle ? entity_index( ).lookup( observer_handle ) : 0;
			const auto observer_services = observer_pawn ?
				app::context().process.load<std::uintptr_t>( observer_pawn +
					SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_id ) ) : 0;
			const auto target_handle = observer_services ?
				app::context().process.load<std::uint32_t>( observer_services +
					SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_id ) ) : 0;
			if ( target_handle && target_handle != 0xffffffffu )
				observer = entity_index( ).lookup( target_handle );
			if ( observer )
				view_team = app::context().process.load<std::int32_t>( observer +
					SCHEMA( "C_BaseEntity", "m_iTeamNum"_id ) );
		}

		const auto game_type = variables( ).get<std::int32_t>( CONVAR( "game_type"_id ) );
		const auto game_mode = variables( ).get<std::int32_t>( CONVAR( "game_mode"_id ) );
		const auto free_for_all = ( game_type == 1 && game_mode == 2 ) ||
			( game_type == 2 && game_mode == 0 );
		const auto tick = app::context().process.load<std::uint32_t>( controller_address +
			SCHEMA( "CBasePlayerController", "m_nTickBase"_id ) );
		const auto needs_game_time = config::visual_settings.m_player.active( )
			|| config::visual_settings.m_projectile.enabled
			|| config::visual_settings.m_bomb.enabled;
		const auto global_vars = needs_game_time
			? app::context().process.load<std::uintptr_t>(
				app::context().addresses.global_vars ) : 0;
		const auto game_time = global_vars
			? app::context().process.load<float>( global_vars + 0x30 ) : 0.0f;

		publish( controller_address, pawn_address, binding.handle, observer, team, view_team,
			crosshair, alive_now, !free_for_all, active_weapon, weapon_data,
			active_weapon_type, tick, health, game_time, flash_alpha );
	}

	void local_state::publish( std::uintptr_t controller_address,
		std::uintptr_t pawn_address, std::uint32_t pawn_handle,
		std::uintptr_t observer,
		std::int32_t team, std::int32_t view_team, std::int32_t crosshair,
		bool alive_now, bool team_mode, std::uintptr_t active_weapon,
		std::uintptr_t weapon_data, std::uint32_t active_weapon_type,
		std::uint32_t tick, std::int32_t health, float game_time,
		float flash_alpha )
	{
		m_controller.store( controller_address, std::memory_order_relaxed );
		m_pawn.store( pawn_address, std::memory_order_relaxed );
		m_pawn_handle.store( pawn_handle, std::memory_order_relaxed );
		m_observer_pawn.store( observer, std::memory_order_relaxed );
		m_team.store( team, std::memory_order_relaxed );
		m_view_team.store( view_team, std::memory_order_relaxed );
		m_crosshair_id.store( crosshair, std::memory_order_relaxed );
		m_alive.store( alive_now, std::memory_order_relaxed );
		m_team_mode.store( team_mode, std::memory_order_relaxed );
		m_weapon.store( active_weapon, std::memory_order_relaxed );
		m_weapon_vdata.store( weapon_data, std::memory_order_relaxed );
		m_weapon_type.store( active_weapon_type, std::memory_order_relaxed );
		m_tick_base.store( tick, std::memory_order_release );
		m_health.store( health, std::memory_order_relaxed );
		m_game_time.store( game_time, std::memory_order_relaxed );
		m_flash_alpha.store( flash_alpha, std::memory_order_relaxed );
	}

	void local_state::reset( )
	{
		publish( 0, 0, 0, 0, 0, 0, 0, false, true, 0, 0, 0, 0,
			0, 0.0f, 0.0f );
	}

}
