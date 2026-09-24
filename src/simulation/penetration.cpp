#include <stdafx.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/penetration_solver.hpp>
#include <core/math/ray_capsule.hpp>

namespace simulation {

	namespace detail {
		static void scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, float& damage )
		{
			const auto ct_head = game::variables().get<float>( CONVAR( "mp_damage_scale_ct_head"_id ) );
			const auto t_head = game::variables().get<float>( CONVAR( "mp_damage_scale_t_head"_id ) );
			const auto ct_body = game::variables().get<float>( CONVAR( "mp_damage_scale_ct_body"_id ) );
			const auto t_body = game::variables().get<float>( CONVAR( "mp_damage_scale_t_body"_id ) );

			const auto is_ct = ( team == 3 );
			const auto head_scale = is_ct ? ct_head : t_head;
			const auto body_scale = is_ct ? ct_body : t_body;

			switch ( hitgroup )
			{
			case 1:
				damage *= headshot_multiplier * head_scale;
				break;
			case 2:
			case 4:
			case 5:
			case 8:
				damage *= body_scale;
				break;
			case 3:
				damage *= 1.25f * body_scale;
				break;
			case 6:
			case 7:
				damage *= 0.75f * body_scale;
				break;
			default:
				break;
			}

			const auto is_head = ( hitgroup == 1 );
			const auto is_armored = ( hitgroup >= 1 && hitgroup <= 5 ) || ( hitgroup == 8 );

			if ( armor <= 0 || !is_armored || ( is_head && !has_helmet ) )
			{
				damage = std::floor( damage );
				return;
			}

			constexpr auto armor_bonus{ 0.5f };
			const auto armor_ratio_scaled = armor_ratio * 0.5f;

			auto damage_to_health = damage * armor_ratio_scaled;
			auto damage_to_armor = ( damage - damage_to_health ) * armor_bonus;

			if ( damage_to_armor > static_cast< float >( armor ) )
			{
				damage_to_health = damage - ( static_cast< float >( armor ) / armor_bonus );
			}

			damage = std::floor( damage_to_health );
		}

	} // namespace detail

	void ballistics_t::penetration::prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon )
	{
		if ( !weapon_vdata || !weapon )
		{
			return;
		}

		if ( this->m_weapon_vdata == weapon_vdata )
		{
			return;
		}

		weapon_data next{};
		int damage{};
		auto complete = true;
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nDamage"_id ),
			&damage, sizeof( damage ) );
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flPenetration"_id ),
			&next.penetration, sizeof( next.penetration ) );
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRangeModifier"_id ),
			&next.range_modifier, sizeof( next.range_modifier ) );
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_id ),
			&next.range, sizeof( next.range ) );
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_id ),
			&next.armor_ratio, sizeof( next.armor_ratio ) );
		complete &= app::context().process.copy(
			weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flHeadshotMultiplier"_id ),
			&next.headshot_multiplier, sizeof( next.headshot_multiplier ) );
		next.damage = static_cast<float>( damage );

		if ( !complete ) return;
		this->m_weapon_data = next;
		this->m_weapon_vdata = weapon_vdata;
	}

    bool ballistics_t::penetration::run(const foundation::vec3& start, const foundation::vec3& end,
        const game::player_snapshot& target, const game::skeleton_reader::data& bones, result& out) const
    {
        return run_seed(start, end - start, target, bones, std::numeric_limits<int>::max(),
            true, 1.0f, -1, out);
    }

	bool ballistics_t::penetration::run_seed( const foundation::vec3& origin,
		const foundation::vec3& input_direction,
		const game::player_snapshot& target,
		const game::skeleton_reader::data& bones, int hitbox_parts,
		bool allow_penetration, float minimum_damage,
		int required_hitbox, result& out ) const
	{
		if ( !game::collision().valid( )
			|| this->m_weapon_data.damage <= 0.0f
			|| this->m_weapon_data.range <= 0.0f )
		{
			return false;
		}

		const auto direction = input_direction.normalized( );
		const auto hitbox_enabled = [ hitbox_parts ]( int index )
		{
			int part{};
			if ( index == 0 )
				part = config::combat_profile::aim_part::head;
			else if ( index >= 1 && index <= 6 )
				part = config::combat_profile::aim_part::body;
			else if ( index >= 7 && index <= 12 )
				part = config::combat_profile::aim_part::legs;
			else if ( index >= 13 && index <= 18 )
				part = config::combat_profile::aim_part::arms;
			return part && ( hitbox_parts & part ) != 0;
		};

		auto target_distance = this->m_weapon_data.range;
		int target_hitbox = -1;
		for ( const auto& box : target.hitboxes )
		{
			if ( box.index < 0 || box.bone < 0 || box.bone >= 128 )
			{
				continue;
			}
			const auto& bone = bones.bones[ box.bone ];
			const auto full_start =
				bone.position + bone.rotation.apply( box.mins );
			const auto full_end =
				bone.position + bone.rotation.apply( box.maxs );
			float distance{};
			if ( foundation::ray_capsule_entry(
				origin, direction, full_start, full_end, box.radius, distance )
				&& distance < target_distance )
			{
				target_distance = distance;
				target_hitbox = box.index;
			}
		}
		if ( target_hitbox < 0 || !hitbox_enabled( target_hitbox )
            || ( required_hitbox >= 0 && target_hitbox != required_hitbox ) )
		{
			return false;
		}

		const auto ray_end = origin + direction * target_distance;
		auto hits = game::collision().trace_ray_all( origin, ray_end );
		std::erase_if( hits, [ target_distance ]( const auto& hit )
			{ return hit.distance > target_distance; } );
		const auto collision = game::collision().build_segments(
			std::move( hits ), target_distance );
		const auto passage = detail::pass_through_world(
			collision, target_distance, m_weapon_data.penetration,
			m_weapon_data.damage, m_weapon_data.range_modifier,
			allow_penetration );
		if ( !passage )
		{
			return false;
		}
		auto damage = passage->damage;
		const auto penetrated = passage->penetrations > 0;
		const auto group =
			game::hitbox_data().hitgroup_from_hitbox( target_hitbox );
        detail::scale_damage(group, target.armor, target.has_helmet, target.team,
            m_weapon_data.armor_ratio, m_weapon_data.headshot_multiplier, damage);

		if ( damage < minimum_damage )
		{
			return false;
		}

		out.damage = damage;
		out.distance = target_distance;
		out.hitbox = target_hitbox;
		out.penetrated = penetrated;
		return true;
	}

	bool ballistics_t::penetration::can( const foundation::vec3& start, const foundation::vec3& direction, float& out_damage ) const
	{
		out_damage = 0.0f;

		if ( !game::collision().valid( )
			|| this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto max_range = this->m_weapon_data.range;
		const auto ray_end = start + direction * max_range;

		auto all_hits = game::collision().trace_ray_all( start, ray_end );
		if ( all_hits.empty( ) )
		{
			return false;
		}

		auto collision = game::collision().build_segments(
			all_hits, max_range );

		if ( collision.segments.empty( ) )
			return false;

		const auto target_distance = std::nextafter(
			collision.segments.front( ).exit_distance,
			std::numeric_limits<float>::infinity( ) );
		std::erase_if( all_hits, [ target_distance ]( const auto& hit )
			{ return hit.distance > target_distance; } );
		collision = game::collision().build_segments(
			std::move( all_hits ), target_distance );
		const auto passage = detail::pass_through_world(
			collision, target_distance, m_weapon_data.penetration,
			m_weapon_data.damage, m_weapon_data.range_modifier, true );
		if ( !passage )
			return false;

		out_damage = passage->damage;
		return true;
	}

	float ballistics_t::penetration::get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return 0.0f;
		}

		auto damage = this->m_weapon_data.damage;
		detail::scale_damage( hitgroup, target_armor, has_helmet, target_team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, damage );
		return damage;
	}

} // namespace simulation
