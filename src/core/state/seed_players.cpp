#include <stdafx.hpp>

namespace game {
void world_sampler::seed_players_into(
	std::vector<player_snapshot>& fresh,
	std::uintptr_t local_pawn, std::uintptr_t local_controller,
	int local_team, bool free_for_all, std::uintptr_t only_pawn ) const
{
	// This is the standalone-equivalent reader: it owns every target input used by
	// seed evaluation and never consumes/publishes the normal ESP snapshot.
	static const auto controller_pawn =
		SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_id );
	static const auto controller_helmet =
		SCHEMA( "CCSPlayerController", "m_bPawnHasHelmet"_id );
	static const auto pawn_health =
		SCHEMA( "C_BaseEntity", "m_iHealth"_id );
	static const auto pawn_team =
		SCHEMA( "C_BaseEntity", "m_iTeamNum"_id );
	static const auto pawn_scene =
		SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id );
	static const auto pawn_immunity =
		SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_id );
	static const auto pawn_armor =
		SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_id );
	static const auto scene_origin =
		SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id );
	static const auto scene_model_state =
		SCHEMA( "CSkeletonInstance", "m_modelState"_id );

	fresh.clear( );
	if ( fresh.capacity( ) < 64 ) fresh.reserve( 64 );

	for ( std::uint32_t index = 1; index <= 64; ++index )
	{
		// Controller enumeration supplies a plain slot index, not a serial CHandle.
		const auto controller = game::entity_index().lookup_index( index );
		if ( !controller || controller == local_controller )
		{
			continue;
		}

		const auto pawn_handle =
			app::context().process.load<std::uint32_t>( controller + controller_pawn );
		const auto pawn = game::entity_index().lookup( pawn_handle );
		if ( !pawn || pawn == local_pawn )
		{
			continue;
		}
		if ( only_pawn && pawn != only_pawn )
		{
			continue;
		}
		player_snapshot value{};
		value.controller = controller;
		value.pawn = pawn;
		value.health = app::context().process.load<std::int32_t>( pawn + pawn_health );
		value.team = app::context().process.load<std::int32_t>( pawn + pawn_team );
		value.invulnerable = app::context().process.load<bool>( pawn + pawn_immunity );
		if ( value.health <= 0 || value.health > 100
			|| ( value.team != 2 && value.team != 3 )
			|| ( !free_for_all && value.team == local_team )
			|| value.invulnerable )
		{
			continue;
		}

		value.armor = app::context().process.load<std::int32_t>( pawn + pawn_armor );
		value.game_scene_node =
			app::context().process.load<std::uintptr_t>( pawn + pawn_scene );
		if ( !value.game_scene_node )
		{
			continue;
		}

		value.origin = app::context().process.load<foundation::vec3>(
			value.game_scene_node + scene_origin );
		value.velocity = app::context().process.load<foundation::vec3>( pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		value.simulation_tick = app::context().process.load<std::int32_t>( pawn
			+ SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id ) );
		value.simulation_time = app::context().process.load<float>( pawn
			+ SCHEMA( "C_BaseEntity", "m_flSimulationTime"_id ) );
		if ( !std::isfinite( value.origin.x ) || !std::isfinite( value.origin.y )
			|| !std::isfinite( value.origin.z ) )
		{
			continue;
		}

		value.bone_cache = app::context().process.load<std::uintptr_t>(
			value.game_scene_node + scene_model_state + 0x80 );
		if ( !value.bone_cache )
		{
			continue;
		}
		value.bones = game::skeletons().get( value.bone_cache );
		if ( !value.bones.is_valid( ) )
		{
			continue;
		}

		value.has_helmet =
			app::context().process.load<bool>( controller + controller_helmet );
		value.hitboxes = game::hitbox_data().query(
			value.game_scene_node, false );
		if ( value.hitboxes.count < 3 )
		{
			continue;
		}
		fresh.push_back( std::move( value ) );
		if ( only_pawn ) break;
	}
}

}
