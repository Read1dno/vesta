#pragma once

#include <simulation/collision.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace simulation::detail {

	struct passage_result
	{
		float damage{};
		int penetrations{};
	};

	[[nodiscard]] inline std::optional<passage_result> pass_through_world(
		const game::collision_world::segment_build_result& collision,
		float target_distance, float weapon_penetration, float initial_damage,
		float range_modifier, bool allow_penetration )
	{
		passage_result state{ initial_damage, 0 };
		if ( collision.unresolved_before( target_distance )
			|| weapon_penetration <= 0.0f ) return std::nullopt;

		const auto weapon_scale = 3.0f / weapon_penetration;
		const auto normal_weapon_loss = std::max( weapon_scale * 1.25f, 0.0f );
		const auto fallback_weapon_loss = std::max( weapon_scale * 1.18f, 0.0f );
		for ( const auto& record : collision.records )
		{
			if ( record.first_contact >= collision.contacts.size( )
				|| record.last_contact >= collision.contacts.size( ) )
				return std::nullopt;

			if ( record.range_loss )
			{
				const auto distance = record.end_distance - record.start_distance;
				state.damage -= std::pow( range_modifier, distance * 0.002f );
				if ( state.damage <= 1.0f ) return std::nullopt;
				continue;
			}

			const auto& entrance = collision.contacts[ record.first_contact ];
			const auto& exit = collision.contacts[ record.last_contact ];
			if ( !allow_penetration ) return std::nullopt;

			auto material_factor = std::numeric_limits<float>::max( );
			auto maximum_density = 0.0f;
			if ( record.first_contact <= record.last_contact )
			{
				for ( auto index = record.first_contact;
					index <= record.last_contact; ++index )
				{
					const auto& surface = collision.contacts[ index ].surface;
					material_factor = std::min( material_factor, surface.penetration );
					maximum_density = std::max( maximum_density, surface.density );
				}
			}

			const auto thickness = record.end_distance - record.start_distance;
			auto base_loss = 2.8f;
			auto proportional_loss = 0.15f;
			auto penetration_loss = fallback_weapon_loss;
			if ( material_factor >= 0.1f && maximum_density <= 3000.0f )
			{
				base_loss = 3.0f;
				proportional_loss = 0.16f;
				penetration_loss = normal_weapon_loss;
				const auto entrance_material = entrance.surface.surface_type;
				if ( entrance_material == exit.surface.surface_type )
				{
					if ( entrance_material == 76 )
						material_factor = 2.0f;
					else if ( ( ( entrance_material - 85u ) & 0xfffffffdu ) == 0 )
						material_factor = 3.0f;

					if ( thickness < 6.0f
						&& ( entrance_material == 71 || entrance_material == 89 ) )
					{
						proportional_loss = 0.05f;
						material_factor = 3.0f;
					}
				}
			}

			const auto resistance = std::max( 1.0f / material_factor, 0.0f );
			state.damage -= thickness * thickness * resistance / 24.0f
				+ proportional_loss * state.damage
				+ base_loss * resistance * penetration_loss;
			if ( state.damage <= 1.0f ) return std::nullopt;
			++state.penetrations;
		}
		return state;
	}

}
