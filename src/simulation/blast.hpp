#pragma once

#include <cstdint>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <core/math/vector.hpp>

namespace game {

class blast_model
{
public:
	struct site_info
	{
		foundation::vec3 mins{};
		foundation::vec3 maxs{};
		float scale{};
	};

	struct damage_value
	{
		std::uint16_t wave_dist{};
		std::uint8_t yaw{};
		std::uint8_t pitch{};
	};

	struct grid_sample
	{
		foundation::vec3 wave_forward{};
		float z{};
		std::int32_t base_damage{};
		bool valid{};
	};

	struct grid_quad
	{
		std::int32_t cell_x{};
		std::int32_t cell_y{};
		std::array<grid_sample, 4> samples{};
		std::array<std::uint32_t, 4> point_indices{};
		std::array<float, 4> damage{};
	};

	struct grid_bounds
	{
		std::int32_t min_x{};
		std::int32_t min_y{};
		std::int32_t max_x{};
		std::int32_t max_y{};
		bool valid{};
	};

	static_assert( sizeof( damage_value ) == sizeof( std::uint32_t ) );

	void parse( );
	void clear( );
	[[nodiscard]] bool valid( ) const;
	[[nodiscard]] std::size_t point_count( ) const;
	[[nodiscard]] std::int32_t site_count( ) const;
	[[nodiscard]] site_info site( std::int32_t index ) const;
	[[nodiscard]] grid_bounds bounds( ) const;
	[[nodiscard]] std::int32_t site_for_position( const foundation::vec3& position ) const;
	[[nodiscard]] int predicted_damage( const foundation::vec3& position, std::int32_t site,
		const foundation::vec3& eye_angles, bool ducked ) const;
	[[nodiscard]] bool cell_damage( std::int32_t cell_x, std::int32_t cell_y, float reference_z,
		std::int32_t site, const foundation::vec3& eye_angles, bool ducked,
		float& out_damage, float& out_z ) const;
	[[nodiscard]] bool sample_grid( std::int32_t min_x, std::int32_t min_y, int width, int height,
		float reference_z, std::int32_t site, std::vector<grid_sample>& out_samples ) const;

	[[nodiscard]] bool sample_grid_surfaces( std::int32_t site,
		float minimum_iso, float maximum_iso,
		std::vector<grid_quad>& out_surfaces ) const;
	[[nodiscard]] static bool evaluate_grid( const std::vector<grid_sample>& samples,
		const foundation::vec3& eye_angles, bool ducked, std::vector<float>& out_damage );
	[[nodiscard]] static bool evaluate_grid_worst_case( const std::vector<grid_sample>& samples,
		std::vector<float>& out_damage );

private:
	struct point
	{
		std::int16_t x, y, z;
	};

	struct cell
	{

		std::uint32_t idx[ 16 ]{};
		std::uint8_t count{};
	};

	[[nodiscard]] static std::uint64_t cell_key( std::int32_t x, std::int32_t y )
	{
		return ( static_cast<std::uint64_t>( static_cast<std::uint32_t>( x ) ) << 32 )
			| static_cast<std::uint32_t>( y );
	}

	[[nodiscard]] const cell* find_cell( std::int32_t x, std::int32_t y ) const;
	[[nodiscard]] std::int32_t nearest_point_index( const foundation::vec3& position ) const;
	[[nodiscard]] static std::int32_t calculate_base_damage( const damage_value& value,
		const site_info& site );
	[[nodiscard]] static std::int32_t calculate_sample_damage( const grid_sample& sample,
		const foundation::vec3& player_forward, bool ducked );
	[[nodiscard]] static std::int32_t calculate_sample_damage_worst_case( const grid_sample& sample );
	[[nodiscard]] static int calculate_damage( const damage_value& value, const site_info& site,
		const foundation::vec3& eye_angles, bool ducked );

	std::vector<site_info> m_sites{};
	std::vector<point> m_points{};
	std::vector<damage_value> m_damage_values{};
	std::unordered_map<std::uint64_t, cell> m_cells{};
	grid_bounds m_bounds{};
	mutable std::shared_mutex m_mutex{};
};

}
