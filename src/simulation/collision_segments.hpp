#pragma once

#include <simulation/collision.hpp>

#include <algorithm>

namespace game::collision_detail {

	[[nodiscard]] inline collision_world::segment_build_result build_segments(
		std::vector<collision_world::hit_entry> hits, float ray_length )
	{
		using record = collision_world::penetration_record;
		using segment = collision_world::penetration_segment;
		collision_world::segment_build_result result{};
		if ( hits.empty( ) || ray_length <= 0.0f ) return result;

		result.had_contacts = true;
		std::ranges::sort( hits, {}, &collision_world::hit_entry::distance );
		result.contacts = std::move( hits );
		auto& contacts = result.contacts;
		auto& records = result.records;
		records.reserve( contacts.size( ) * 2 );

		const auto emit_pair = [ & ]( std::size_t first, std::size_t last )
		{
			const auto range_start = first == 0 ? 0.0f : contacts[ first ].distance;
			records.push_back( record{ first, last, true,
				contacts[ last ].distance, range_start } );
			records.push_back( record{ first, last, false,
				contacts[ last ].distance, contacts[ first ].distance } );
		};

		std::size_t anchor{};
		std::size_t depth{};
		for ( std::size_t current{}; current < contacts.size( ); ++current )
		{
			if ( contacts[ current ].is_enter )
			{
				if ( depth == 0 ) anchor = current;
				++depth;
				continue;
			}

			if ( depth == 0 )
			{

				result.unresolved_distance = 0.0f;
				continue;
			}

			if ( --depth == 0 )
				emit_pair( anchor, current );
		}
		if ( depth > 0 )
			result.unresolved_distance = std::min(
				result.unresolved_distance, contacts[ anchor ].distance );

		for ( const auto& folded : records )
		{
			if ( folded.range_loss || folded.first_contact >= contacts.size( )
				|| folded.last_contact >= contacts.size( ) ) continue;
			const auto& first = contacts[ folded.first_contact ];
			const auto& last = contacts[ folded.last_contact ];
			auto minimum = std::numeric_limits<float>::max( );
			auto maximum_density = 0.0f;
			if ( folded.first_contact <= folded.last_contact )
			{
				for ( auto index = folded.first_contact;
					index <= folded.last_contact; ++index )
				{
					minimum = std::min( minimum,
						contacts[ index ].surface.penetration );
					maximum_density = std::max( maximum_density,
						contacts[ index ].surface.density );
				}
			}
			result.segments.push_back( segment{
				.enter_fraction = first.fraction,
				.exit_fraction = last.fraction,
				.enter_distance = folded.start_distance,
				.exit_distance = folded.end_distance,
				.enter_pos = first.position,
				.exit_pos = last.position,
				.enter_surface = first.surface,
				.exit_surface = last.surface,
				.thickness = folded.end_distance - folded.start_distance,
				.min_pen_mod = minimum,
				.max_density = maximum_density,
				.first_contact = folded.first_contact,
				.last_contact = folded.last_contact
			} );
		}

		return result;
	}

}
