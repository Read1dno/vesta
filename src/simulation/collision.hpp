#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <core/math/vector.hpp>

namespace game {

class collision_world
{
public:
	static constexpr std::uint32_t grenade_clip_contents{ 0x80000000u };

	struct surface_info
	{
		float penetration{};
		std::uint16_t surface_type{};
		std::uint8_t global_index{ 255 };
		std::uint32_t contents{};
		float density{};
	};

	struct global_surface_entry
	{
		float unk_00{};
		float unk_04{};
		float penetration_mod{};
		float unk_0C{};
		float unk_10{};
		std::uint16_t surface_type{};
		std::uint16_t pad{};
		std::uint8_t pad2[ 8 ]{};
	};

	struct triangle
	{
		foundation::vec3 v0{};
		foundation::vec3 v1{};
		foundation::vec3 v2{};
		surface_info surface{};
		std::uint64_t solid_id{};
	};

	struct trace_result
	{
		bool hit{};
		float fraction{};
		float distance{};
		foundation::vec3 end_pos{};
		foundation::vec3 normal{};
		surface_info surface{};
		std::int32_t triangle_index{ -1 };
	};

	struct hit_entry
	{
		float distance{};
		float fraction{};
		foundation::vec3 position{};
		foundation::vec3 normal{};
		surface_info surface{};
		std::int32_t triangle_index{ -1 };
		bool is_enter{ true };
		std::uint64_t solid_id{};
	};

	struct penetration_segment
	{
		float enter_fraction{};
		float exit_fraction{};
		float enter_distance{};
		float exit_distance{};
		foundation::vec3 enter_pos{};
		foundation::vec3 exit_pos{};
		surface_info enter_surface{};
		surface_info exit_surface{};
		float thickness{};
		float min_pen_mod{};
		float max_density{};
		std::size_t first_contact{};
		std::size_t last_contact{};
	};

	struct penetration_record
	{
		std::size_t first_contact{};
		std::size_t last_contact{};
		bool range_loss{};
		float end_distance{};
		float start_distance{};
	};

	struct segment_build_result
	{
		std::vector<hit_entry> contacts{};
		std::vector<penetration_record> records{};
		std::vector<penetration_segment> segments{};
		float unresolved_distance{ std::numeric_limits<float>::infinity( ) };
		bool had_contacts{};

		[[nodiscard]] bool unresolved_before( float distance ) const noexcept
		{
			return unresolved_distance < distance;
		}
	};

	struct render_snapshot
	{
		struct mesh
		{
			struct batch
			{
				foundation::vec3 mins{};
				foundation::vec3 maxs{};
				std::uint32_t first_index{};
				std::uint32_t index_count{};
			};

			std::vector<foundation::vec3> vertices{};
			std::vector<std::uint32_t> indices{};
			std::vector<batch> batches{};
		};

		std::shared_ptr<const mesh> world{};
		std::shared_ptr<const mesh> entities{};
		std::uint64_t world_revision{};
		std::uint64_t entity_revision{};
	};

	void parse( );
	[[nodiscard]] bool build_from_map_file( const std::string& map_name );
	void refresh_map_entities( );
	void clear( );

	[[nodiscard]] std::vector<global_surface_entry> read_surface_table( ) const;
	[[nodiscard]] trace_result trace_ray( const foundation::vec3& start,
		const foundation::vec3& end, std::int32_t exclude_tri = -1 ) const;
	[[nodiscard]] trace_result sweep_sphere( const foundation::vec3& start,
		const foundation::vec3& end, float radius,
		std::int32_t exclude_tri = -1 ) const;
	[[nodiscard]] trace_result sweep_hull( const foundation::vec3& start,
		const foundation::vec3& end, const foundation::vec3& half_extents,
		std::int32_t exclude_tri = -1 ) const;
	[[nodiscard]] std::vector<hit_entry> trace_ray_all( const foundation::vec3& start,
		const foundation::vec3& end ) const;
	[[nodiscard]] segment_build_result build_segments(
		std::vector<hit_entry> hits, float ray_length ) const;
	[[nodiscard]] std::vector<triangle> triangles( ) const;
	[[nodiscard]] std::size_t count( ) const;
	[[nodiscard]] bool valid( ) const;
	[[nodiscard]] std::vector<foundation::vec3> get_render_vertices( ) const;
	[[nodiscard]] render_snapshot render_geometry( ) const;
	[[nodiscard]] std::uint64_t geometry_revision( ) const;

private:
	struct aabb
	{
		float mins[ 3 ]{ 1e12f, 1e12f, 1e12f };
		float maxs[ 3 ]{ -1e12f, -1e12f, -1e12f };

		void expand( const foundation::vec3& point );
		void expand( const aabb& other );
		[[nodiscard]] int longest_axis( ) const;
		[[nodiscard]] bool intersects_ray( const float origin[ 3 ],
			const float inverse_direction[ 3 ], float max_distance,
			float padding = 0.0f ) const;
	};

	struct bvh_node
	{
		aabb bounds{};
		std::int32_t left{ -1 };
		std::int32_t right{ -1 };
		std::int32_t tri_start{};
		std::int32_t tri_count{};
	};

	struct ray_query
	{
		foundation::vec3 origin{};
		foundation::vec3 direction{};
		float length{};
		float origin_components[ 3 ]{};
		float inverse_direction[ 3 ]{};
	};

	struct ray_contact
	{
		float distance{};
		foundation::vec3 position{};
		foundation::vec3 normal{};
		surface_info surface{};
		std::int32_t triangle_index{ -1 };
		bool entering{};
		std::uint64_t solid_id{};
	};

	void rebuild_accel( bool rebuild_world_render = true );
	void rebuild_render_geometry( bool rebuild_world, bool rebuild_entities );
	std::int32_t build_recursive( std::int32_t start, std::int32_t end, std::int32_t depth );
	[[nodiscard]] static std::optional<ray_query> make_ray_query(
		const foundation::vec3& start, const foundation::vec3& end );
	[[nodiscard]] static std::optional<ray_contact> intersect_triangle(
		const ray_query& ray, const triangle& candidate, std::int32_t triangle_index,
		float distance_limit );
	void traverse_ray( const ray_query& ray, std::int32_t excluded_triangle,
		bool nearest_only, std::vector<ray_contact>& contacts ) const;
	void traverse_sphere( const ray_query& ray, float radius,
		std::int32_t excluded_triangle, std::vector<ray_contact>& contacts ) const;
	void traverse_hull( const ray_query& ray, const foundation::vec3& half_extents,
		std::int32_t excluded_triangle, std::vector<ray_contact>& contacts ) const;
	[[nodiscard]] std::uintptr_t surface_manager( ) const;

	static constexpr auto k_surface_manager_attempts{ 3 };
	static constexpr auto k_max_leaf_tris{ 8 };
	static constexpr auto k_max_depth{ 48 };

	mutable std::uintptr_t m_surface_manager{};
	mutable std::int32_t m_surface_manager_attempts{};
	std::vector<triangle> m_triangles{};
	std::size_t m_world_triangle_count{};
	std::string m_map_name{};
	std::size_t m_entity_triangle_count{};
	mutable std::shared_mutex m_mutex{};
	std::vector<bvh_node> m_nodes{};
	std::vector<std::int32_t> m_indices{};
	std::vector<aabb> m_tri_bounds{};
	std::vector<float> m_centroids{};
	std::atomic<std::uint64_t> m_geometry_revision{};
	std::shared_ptr<const render_snapshot::mesh> m_world_render{};
	std::shared_ptr<const render_snapshot::mesh> m_entity_render{};
	std::shared_ptr<const collision_world> m_entity_collision{};
	std::uint64_t m_entity_state_hash{};
	std::uint64_t m_world_render_revision{};
	std::uint64_t m_entity_render_revision{};
};

}
