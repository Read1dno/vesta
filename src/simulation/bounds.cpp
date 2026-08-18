#include <stdafx.hpp>

namespace game {

	namespace {
		constexpr foundation::vec2 invalid_point{ 0xdead, 0xdead };
		constexpr std::array<std::uint8_t, 21> body_bones{
			1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
			17, 18, 19, 20, 21, 22, 23 };
		constexpr std::array<foundation::vec3, 9> silhouette_offsets{
			foundation::vec3{}, { 7.5f, 0.0f, 0.0f }, { -7.5f, 0.0f, 0.0f },
			{ 0.0f, 7.5f, 0.0f }, { 0.0f, -7.5f, 0.0f },
			{ 0.0f, 0.0f, 9.0f }, { 0.0f, 0.0f, -9.0f },
			{ 7.5f, 7.5f, 0.0f }, { -7.5f, -7.5f, 0.0f } };

		struct screen_extent
		{
			foundation::vec2 low{ std::numeric_limits<float>::max( ),
				std::numeric_limits<float>::max( ) };
			foundation::vec2 high{ std::numeric_limits<float>::lowest( ),
				std::numeric_limits<float>::lowest( ) };
			bool accepted{};
			bool clipped{};

			void include( const foundation::vec2& point ) noexcept
			{
				accepted = true;
				low.x = std::min( low.x, point.x );
				low.y = std::min( low.y, point.y );
				high.x = std::max( high.x, point.x );
				high.y = std::max( high.y, point.y );
			}
		};
	}

	bool bounds_projector::data::is_valid( ) const
	{
		return min != invalid_point && max.x >= min.x && max.y >= min.y;
	}

	bounds_projector::data bounds_projector::get(
		const skeleton_reader::data& skeleton ) const
	{
		screen_extent extent{};
		for ( const auto index : body_bones )
		{
			const auto& position = skeleton.bones[ index ].position;
			if ( position.length_sqr( ) == 0.0f ) continue;
			for ( const auto& offset : silhouette_offsets )
			{
				const auto point = camera( ).project( position + offset );
				if ( !camera( ).projection_valid( point ) )
				{
					extent.clipped = true;
					continue;
				}
				extent.include( point );
			}
		}

		if ( extent.clipped || !extent.accepted ) return { invalid_point, {} };
		return { extent.low, extent.high };
	}

}
