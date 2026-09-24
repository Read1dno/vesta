#include <stdafx.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/accuracy_snapshot.hpp>

namespace simulation {
	bool ballistics_t::seed_weapon( std::uintptr_t pawn, std::uintptr_t controller,
		const foundation::vec3& velocity, context& output )
	{
		output = {};
		context ctx{};

		if ( !pawn || !controller )
		{
			return false;
		}
		bool reads_valid{ true };
		const auto read_required = [ & ]( const std::uintptr_t address, auto& value )
		{
			if ( !app::context().process.copy( address, &value, sizeof( value ) ) )
			{
				value = {};
				reads_valid = false;
			}
		};

		std::uintptr_t weapon_services{};
		read_required( pawn
			+ SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_id ), weapon_services );
		if ( !weapon_services )
		{
			return false;
		}
		std::uint32_t weapon_handle{};
		read_required( weapon_services
			+ SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_id ), weapon_handle );
		ctx.weapon = game::entity_index().lookup( weapon_handle );
		if ( !ctx.weapon )
		{
			return false;
		}
		read_required( ctx.weapon
			+ SCHEMA( "C_BaseEntity", "m_nSubclassID"_id ) + 0x8, ctx.weapon_vdata );
		if ( !ctx.weapon_vdata )
		{
			return false;
		}

		read_required(
			ctx.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
			+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
			+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ), ctx.item_def_idx );
		constexpr std::uint16_t revolver_id{ 64 };

		if ( ctx.item_def_idx == revolver_id ) ctx.fire_mode = 0;
		else read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_weaponMode"_id ), ctx.fire_mode );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_id ), ctx.num_bullets );
        if (ctx.num_bullets > 1) {
            const auto offset = SCHEMA("CCSWeaponBaseVData", "m_nSpreadSeed"_id);
            if (offset <= 0) reads_valid = false;
            else read_required(ctx.weapon_vdata + offset, ctx.pattern_seed);
        }
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_id ), ctx.weapon_type );

		const auto mode_value = [ & ]( const std::pair<float, float>& value )
		{
			return ctx.fire_mode == 1 ? value.second : value.first;
		};
		std::pair<float, float> spread_values{};
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flSpread"_id ), spread_values );
		ctx.spread = mode_value( spread_values );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_id ), ctx.recoil_index );

		std::pair<float, float> inaccuracy_move{}, max_speed{};
		float jump_initial{}, jump_apex{}, turning{}, accuracy_penalty{};
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_id ), inaccuracy_move );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_id ), max_speed );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_id ), jump_initial );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_id ), jump_apex );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracy"_id ), turning );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_id ), accuracy_penalty );

		ctx.velocity = velocity;
		ctx.velocity_length = ctx.velocity.length_2d( );
        read_required(pawn + SCHEMA("C_CSPlayerPawn", "m_bIsWalking"_id), ctx.is_walking);
        read_required(pawn + SCHEMA("C_BaseEntity", "m_hGroundEntity"_id), ctx.ground_entity);
        ctx.on_ground = ctx.ground_entity != 0xffffffffu && ctx.ground_entity != 0xfffffffeu
            && game::entity_index().lookup(ctx.ground_entity) != 0;

        inaccuracy_debug_data accuracy{};
        accuracy.fire_mode = ctx.fire_mode;
        accuracy.velocity = ctx.velocity;
        accuracy.is_walking = ctx.is_walking;
        accuracy.on_ground = ctx.on_ground;
        accuracy.inaccuracy_move = inaccuracy_move;
        accuracy.max_speed = max_speed;
        accuracy.inaccuracy_jump_initial = jump_initial;
        accuracy.inaccuracy_jump_apex = jump_apex;
        accuracy.turning_inaccuracy = turning;
        accuracy.accuracy_penalty = accuracy_penalty;
        read_required(pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id), accuracy.eye_angles);
        ctx.inaccuracy = detail::accuracy_from_snapshot(accuracy);

		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_bInReload"_id ), ctx.is_reloading );
		int clip{};
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_id ), clip );
		ctx.clip = clip;
		int current_tick{};
		read_required( controller
			+ SCHEMA( "CBasePlayerController", "m_nTickBase"_id ), current_tick );
		ctx.player_tick = current_tick;
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_nPostponeFireReadyTicks"_id ), ctx.postpone_fire_ready_tick );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flPostponeFireReadyFrac"_id ), ctx.postpone_fire_ready_fraction );
		constexpr auto secondary = false;
		int next_tick{};
		read_required( ctx.weapon + ( secondary
				? SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_id )
				: SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_id ) ), next_tick );
		float next_ratio{};
		read_required( ctx.weapon + ( secondary
				? SCHEMA( "C_BasePlayerWeapon", "m_flNextSecondaryAttackTickRatio"_id )
				: SCHEMA( "C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_id ) ), next_ratio );
		ctx.weapon_ready = !ctx.is_reloading && clip != 0
			&& ( next_tick < current_tick
				|| ( next_tick == current_tick && next_ratio <= 0.001f ) );

		if ( !reads_valid || ctx.num_bullets <= 0 || ctx.num_bullets > 32 )
			return false;

		this->m_pen.prepare( ctx.weapon_vdata, ctx.weapon );
		const auto& weapon_data = this->m_pen.get_weapon_data( );
		ctx.valid = std::isfinite( ctx.spread ) && ctx.spread >= 0.0f
			&& std::isfinite( ctx.inaccuracy ) && ctx.inaccuracy >= 0.0f
			&& ctx.item_def_idx > 0 && weapon_data.damage > 0.0f
			&& weapon_data.penetration > 0.0f && weapon_data.range > 0.0f
			&& weapon_data.range_modifier > 0.0f
			&& weapon_data.range_modifier <= 1.0f;
		if ( !ctx.valid )
		{
			return false;
		}

		output = ctx;
		return true;
	}

bool ballistics_t::seed_weapon(std::uintptr_t pawn, std::uintptr_t controller, context& output)
{
    foundation::vec3 velocity{};
    if (!pawn || !app::context().process.copy(pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id),
        &velocity, sizeof(velocity))) { output = {}; return false; }
    return seed_weapon(pawn, controller, velocity, output);
}
}
