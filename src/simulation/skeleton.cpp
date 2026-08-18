#include <stdafx.hpp>

namespace game {

	namespace {
		struct alignas( 16 ) transform_record
		{
			foundation::vec3 translation{};
			float scale{};
			foundation::rotation orientation{};
		};
		static_assert( sizeof( transform_record ) == 32 );
	}

	bool skeleton_reader::data::is_valid( ) const
	{
		const auto& origin = bones.front( ).position;
		return std::isfinite( origin.x ) && std::isfinite( origin.y ) &&
			std::isfinite( origin.z ) && origin.length_sqr( ) > 0.0f;
	}

	foundation::vec3 skeleton_reader::data::get_position(
		std::uint32_t id ) const
	{
		return id < bones.size( ) ? bones[ id ].position : foundation::vec3{};
	}

	foundation::rotation skeleton_reader::data::get_rotation(
		std::uint32_t id ) const
	{
		return id < bones.size( ) ? bones[ id ].rotation : foundation::rotation{};
	}

	skeleton_reader::data skeleton_reader::get( std::uintptr_t bone_cache ) const
	{
		std::array<transform_record, 128> source{};
		static_assert( sizeof( source ) == 4096 );
		if ( !app::context().process.copy( bone_cache, source.data( ), sizeof( source ) ) )
			return {};

		data snapshot{};
		std::transform( source.begin( ), source.end( ), snapshot.bones.begin( ),
			[ ]( const transform_record& value )
			{
				return data::bone{ value.translation, value.orientation };
			} );
		return snapshot;
	}

}
