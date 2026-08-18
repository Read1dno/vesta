#include <stdafx.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/penetration_solver.hpp>

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

	}

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

	bool ballistics_t::penetration::run( const foundation::vec3& start, const foundation::vec3& end, const game::player_snapshot& target, const game::skeleton_reader::data& bones, result& out ) const
	{
		if ( !game::collision().valid( )
			|| this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto direction = ( end - start ).normalized( );
		const auto max_range = this->m_weapon_data.range;
		const auto ray_end = start + direction * max_range;

		float best_target_dist = max_range;
		const game::hitbox_catalog::entry* best_hb = nullptr;

		for ( const auto& hb : target.hitboxes )
		{
			if ( hb.index < 0 || hb.bone < 0 )
			{
				continue;
			}

			const auto& bone = bones.bones[ hb.bone ];

			const auto capsule_start = bone.position + foundation::rotate( bone.rotation, hb.mins );
			const auto capsule_end = bone.position + foundation::rotate( bone.rotation, hb.maxs );
			const auto center_world = ( capsule_start + capsule_end ) * 0.5f;

			if ( ballistics().ray_hits_capsule( start, direction, capsule_start, capsule_end, hb.radius ) )
			{
				const auto to_center = center_world - start;
				const auto hit_dist = to_center.dot( direction );
				if ( hit_dist > 0.0f && hit_dist < best_target_dist )
				{
					best_target_dist = hit_dist;
					best_hb = &hb;
				}
			}
		}

		if ( !best_hb )
		{
			return false;
		}

		auto all_hits = game::collision().trace_ray_all( start, ray_end );
		std::erase_if( all_hits, [ best_target_dist ]( const auto& hit )
			{ return hit.distance > best_target_dist; } );
		const auto collision = game::collision().build_segments(
			std::move( all_hits ), best_target_dist );

		const auto passage = detail::pass_through_world(
			collision, best_target_dist, m_weapon_data.penetration,
			m_weapon_data.damage, m_weapon_data.range_modifier, true );
		if ( !passage )
		{
			return false;
		}

		auto damage = passage->damage;

		if ( damage < 1.0f )
		{
			return false;
		}

		const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox( best_hb->index );
		detail::scale_damage( hitgroup, target.armor, target.has_helmet, target.team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, damage );

		if ( damage < 1.0f )
		{
			return false;
		}

		out.damage = damage;
		out.distance = best_target_dist;
		out.hitbox = best_hb->index;
		out.penetrated = passage->penetrations > 0;
		return true;
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
		const auto ray_capsule_distance = [ ](
			const foundation::vec3& ray_origin,
			const foundation::vec3& ray_direction,
			const foundation::vec3& start, const foundation::vec3& end,
			float radius, float& distance )
		{
			const auto axis = end - start;
			const auto length = axis.length( );
			if ( length < 0.001f )
			{
				distance = ( start - ray_origin ).dot( ray_direction );
				return distance >= 0.0f
					&& ( ray_origin + ray_direction * distance - start )
						.length_sqr( ) <= radius * radius;
			}
			const auto capsule_direction = axis / length;
			const auto offset = ray_origin - start;
			const auto a = ray_direction.dot( ray_direction );
			const auto b = ray_direction.dot( capsule_direction );
			const auto c = capsule_direction.dot( capsule_direction );
			const auto d = ray_direction.dot( offset );
			const auto e = capsule_direction.dot( offset );
			const auto denominator = a * c - b * b;
			auto capsule_parameter = std::abs( denominator ) < 0.0001f
				? ( b > c ? d / b : e / c )
				: ( a * e - b * d ) / denominator;
			capsule_parameter =
				std::clamp( capsule_parameter, 0.0f, length );
			const auto capsule_point =
				start + capsule_direction * capsule_parameter;
			distance = ( capsule_point - ray_origin ).dot( ray_direction );
			return distance >= 0.0f
				&& ( ray_origin + ray_direction * distance - capsule_point )
					.length_sqr( ) <= radius * radius;
		};

		auto target_distance = this->m_weapon_data.range;
		int target_hitbox = -1;
		constexpr auto hitbox_scale{ 0.9999f };
		for ( const auto& box : target.hitboxes )
		{
			if ( box.index < 0 || box.bone < 0 || box.bone >= 128
				|| !hitbox_enabled( box.index )
				|| ( required_hitbox >= 0 && box.index != required_hitbox ) )
			{
				continue;
			}
			const auto& bone = bones.bones[ box.bone ];
			const auto full_start =
				bone.position + bone.rotation.apply( box.mins );
			const auto full_end =
				bone.position + bone.rotation.apply( box.maxs );
			const auto center = ( full_start + full_end ) * 0.5f;
			const auto start =
				center + ( full_start - center ) * hitbox_scale;
			const auto end =
				center + ( full_end - center ) * hitbox_scale;
			float distance{};
			if ( ray_capsule_distance(
				origin, direction, start, end,
				std::max( 0.5f, box.radius ) * hitbox_scale, distance )
				&& distance < target_distance )
			{
				target_distance = distance;
				target_hitbox = box.index;
			}
		}
		if ( target_hitbox < 0 )
		{
			return false;
		}

		const auto ray_end = origin + direction * m_weapon_data.range;
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
		if ( group == 1 )
			damage *= this->m_weapon_data.headshot_multiplier;
		else if ( group == 3 )
			damage *= 1.25f;
		else if ( group == 6 || group == 7 )
			damage *= 0.75f;

		const auto armored = ( group >= 1 && group <= 5 ) || group == 8;
		if ( target.armor > 0 && armored
			&& ( group != 1 || target.has_helmet ) )
		{
			constexpr auto armor_bonus{ 0.5f };
			const auto ratio = this->m_weapon_data.armor_ratio * 0.5f;
			auto health_damage = damage * ratio;
			const auto armor_damage =
				( damage - health_damage ) * armor_bonus;
			if ( armor_damage > static_cast<float>( target.armor ) )
			{
				health_damage =
					damage - static_cast<float>( target.armor ) / armor_bonus;
			}
			damage = health_damage;
		}
		damage = std::floor( damage );
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

}
