#include <stdafx.hpp>

#include <simulation/grenade.hpp>

namespace simulation {

	grenade_trajectory_engine::flight_profile grenade_trajectory_engine::profile_for( std::uintptr_t weapon_id )
	{
		flight_profile profile{};
		profile.gravity = game::variables().get<float>( CONVAR( "sv_gravity"_id ) ) * gravity_scale;

		if ( weapon_id == "weapon_molotov"_id || weapon_id == "weapon_incgrenade"_id )
		{
			const auto maximum_slope = game::variables().get<float>(
				CONVAR( "weapon_molotov_maxdetonateslope"_id ) );
			profile.fuse_seconds = 2.0f;
			profile.floor_normal_z = std::cos( maximum_slope * std::numbers::pi_v<float> / 180.0f );
			profile.floor_detonates = true;
			return profile;
		}

		if ( weapon_id == "weapon_smokegrenade"_id || weapon_id == "weapon_decoy"_id )
		{
			profile.fuse_seconds = 2.0f;
			profile.rest_speed = weapon_id == "weapon_decoy"_id ? 0.2f : 0.1f;
			profile.rest_detonates = true;
			return profile;
		}

		profile.delayed_fuse = weapon_id == "weapon_flashbang"_id || weapon_id == "weapon_hegrenade"_id;
		return profile;
	}

	grenade_trajectory_engine::step_result grenade_trajectory_engine::advance(
		const foundation::vec3& position, const foundation::vec3& velocity, float gravity )
	{
		step_result result{};
		result.velocity = velocity;
		const auto next_vertical_speed = velocity.z - gravity * game::rules::simulation_step;
		const auto displacement = foundation::vec3{
			velocity.x * game::rules::simulation_step,
			velocity.y * game::rules::simulation_step,
			( velocity.z + next_vertical_speed ) * 0.5f * game::rules::simulation_step };
		result.velocity.z = next_vertical_speed;

		result.position = position;
		auto movement = displacement;
		auto remaining_fraction = 1.0f;
		for ( int contact_index = 0;
			contact_index < maximum_contacts_per_tick && movement.length_sqr( ) > 1e-8f;
			++contact_index )
		{
			const auto contact = game::collision().sweep_hull(
				result.position, result.position + movement, grenade_collision_half_extents );
			if ( !contact.hit )
			{
				result.position = contact.end_pos;
				break;
			}

			result.position = contact.end_pos + contact.normal * collision_skin;
			result.collided = true;
			result.collision_normal = contact.normal;
			result.velocity = reflected_velocity( result.velocity, contact.normal );
			if ( contact.normal.z > 0.7f )
			{
				const auto speed_squared = result.velocity.length_sqr( );
				if ( speed_squared > 96000.0f )
				{
					const auto alignment = result.velocity.normalized().dot( contact.normal );
					if ( alignment > 0.5f ) result.velocity *= 1.5f - alignment;
				}
				if ( speed_squared < 400.0f )
				{
					result.velocity = {};
					break;
				}
			}

			remaining_fraction *= std::clamp( 1.0f - contact.fraction, 0.0f, 1.0f );
			if ( remaining_fraction <= 1e-4f ) break;
			movement = result.velocity * ( remaining_fraction * game::rules::simulation_step );
		}
		return result;
	}

	foundation::vec3 grenade_trajectory_engine::reflected_velocity(
		const foundation::vec3& incoming, const foundation::vec3& normal )
	{
		const auto reflected = incoming - normal * ( incoming.dot( normal ) * 2.0f );
		return reflected * std::clamp( bounce_elasticity, 0.0f, 0.9f );
	}

	bool grenade_trajectory_engine::fuse_complete( const flight_profile& profile,
		const foundation::vec3& velocity, int tick )
	{
		if ( profile.rest_detonates )
		{
			const auto horizontal_speed = std::hypot( velocity.x, velocity.y );
			const auto check_interval = static_cast<int>( 0.2f / game::rules::simulation_step );
			return horizontal_speed < profile.rest_speed && tick % check_interval == 0;
		}

		const auto elapsed = tick * game::rules::simulation_step;
		return profile.delayed_fuse
			? ( tick - 8 ) * game::rules::simulation_step > profile.fuse_seconds
			: elapsed > profile.fuse_seconds;
	}

	grenade_path grenade_trajectory_engine::predict( const foundation::vec3& origin,
		const foundation::vec3& initial_velocity, std::uintptr_t weapon_id,
		const float remaining_lifetime ) const
	{
		grenade_path path{};
		path.points.reserve( maximum_ticks / sample_stride );
		const auto profile = profile_for( weapon_id );
		auto position = origin;
		auto velocity = initial_velocity;
		int collisions{};

		for ( int tick = 0; tick < maximum_ticks; ++tick )
		{
			if ( tick % sample_stride == 0 )
				path.points.push_back( position );

			const auto step = advance( position, velocity, profile.gravity );
			position = step.position;
			velocity = step.velocity;
			if ( step.collided )
			{
				++collisions;
				path.bounces.push_back( position );
				if ( profile.floor_detonates && step.collision_normal.z >= profile.floor_normal_z )
				{
					path.end_tick = tick;
					break;
				}
			}

			const auto stopped = std::abs( velocity.x ) < 20.0f
				&& std::abs( velocity.y ) < 20.0f && velocity.length_sqr( ) < 400.0f;
			const auto remaining_fuse_complete = std::isfinite( remaining_lifetime )
				&& remaining_lifetime >= 0.0f
				&& static_cast<float>( tick ) * game::rules::simulation_step >= remaining_lifetime;
			if ( remaining_fuse_complete || fuse_complete( profile, velocity, tick )
				|| collisions > 20 || stopped )
			{
				path.end_tick = tick;
				break;
			}
		}

		if ( path.end_tick < 0 )
			return path;

		path.end_pos = position;
		path.duration = path.end_tick * game::rules::simulation_step;
		if ( path.points.empty( ) || path.points.back().distance_sqr( position ) > 1.0f )
			path.points.push_back( position );
		path.valid = true;
		return path;
	}

}
