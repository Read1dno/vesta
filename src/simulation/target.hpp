#pragma once

#include <array>
#include <cstdint>
#include <shared_mutex>

#include <core/math/vector.hpp>

namespace game {

class skeleton_reader
{
public:
	struct data
	{
		struct bone
		{
			foundation::vec3 position{};
			foundation::rotation rotation{};
		};

		std::array<bone, 128> bones{};

		[[nodiscard]] bool is_valid( ) const;
		[[nodiscard]] foundation::vec3 get_position( std::uint32_t id ) const;
		[[nodiscard]] foundation::rotation get_rotation( std::uint32_t id ) const;
	};

	[[nodiscard]] data get( std::uintptr_t bone_array ) const;
};

class bounds_projector
{
public:
	struct data
	{
		foundation::vec2 min{};
		foundation::vec2 max{};

		[[nodiscard]] bool is_valid( ) const;
		[[nodiscard]] float width( ) const { return max.x - min.x; }
		[[nodiscard]] float height( ) const { return max.y - min.y; }
	};

	[[nodiscard]] data get( const skeleton_reader::data& bone_data ) const;
};

class hitbox_catalog
{
public:
	struct entry
	{
		int index{ -1 };
		int bone{ -1 };
		std::array<char, 16> name{};
		foundation::vec3 mins{};
		foundation::vec3 maxs{};
		float radius{};
	};

	struct set
	{
		std::array<entry, 20> entries{};
		int count{};

		[[nodiscard]] const entry* begin( ) const { return entries.data( ); }
		[[nodiscard]] const entry* end( ) const { return entries.data( ) + count; }
	};

	[[nodiscard]] set query( std::uintptr_t game_scene_node,
		bool use_geometry_cache = true ) const;
	[[nodiscard]] int hitgroup_from_hitbox( int hitbox ) const;
	void remember( const set& snapshot );
	[[nodiscard]] set snapshot( ) const;

private:
	set m_snapshot{};
	mutable std::shared_mutex m_snapshot_mutex{};
	static constexpr int k_bone_map[]{ 7, 6, 1, 2, 3, 4, 5, 17, 20, 18,
		21, 19, 22, 11, 15, 9, 10, 13, 14 };
};

}
