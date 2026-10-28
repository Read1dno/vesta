#pragma once

#include <foundation/math/vector.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace simulation
{
// The map's navigation mesh (maps/<map>.nav, format 35/36), reduced to what route planning needs: walkable polygons,
// their links and the ladders. Built once per map on the map-loading thread, then read-only.
class nav_graph
{
public:
    enum class move : std::uint8_t
    {
        walk,        // level or a step the player walks up
        jump,        // a ledge or a gap that needs a jump
        crouch_jump, // a ledge too high for a standing jump
        drop,        // walking off a ledge
        ladder_up,
        ladder_down,
    };

    struct link
    {
        std::uint32_t target{};      // area index
        foundation::vec3 portal{};   // where the route crosses into the target (on this area's floor)
        foundation::vec3 portal_a{}; // the crossable part of this area's edge, narrowed by the player's radius
        foundation::vec3 portal_b{};
        foundation::vec3 landing{}; // first point on the target's floor past the crossing (or the ladder's far end)
        float rise{};               // target floor height at the portal minus this area's
        std::int32_t ladder{-1};    // ladder index for ladder moves
        move kind{move::walk};
        bool low_ceiling{}; // the crossing has no room to stand
    };

    struct area
    {
        std::uint32_t id{};
        std::uint64_t flags{};
        std::vector<foundation::vec3> corners{};
        foundation::vec3 center{};
        foundation::vec3 normal{0.0f, 0.0f, 1.0f};
        foundation::vec3 mins{};
        foundation::vec3 maxs{};
        std::vector<link> links{};
        bool low_ceiling{}; // somewhere in the area the player has to crouch
    };

    struct ladder
    {
        std::uint32_t id{};
        float width{};
        foundation::vec3 top{};
        foundation::vec3 bottom{};
        foundation::vec3 facing{}; // horizontal, from the ladder towards the side it is climbed from
    };

    // One leg of a route: reach `point` in `area`'s successor, moving as `kind`.
    struct step
    {
        std::uint32_t from{};
        std::uint32_t to{};
        foundation::vec3 point{};
        foundation::vec3 portal_a{};
        foundation::vec3 portal_b{};
        foundation::vec3 landing{};
        float rise{};
        std::int32_t ladder{-1};
        move kind{move::walk};
        bool low_ceiling{};
    };

    // Reads the file; false (with `error` set) when it is not a navigation mesh this reader understands.
    [[nodiscard]] bool parse(const std::vector<std::uint8_t>& data, std::string& error);
    // Marks crossings and areas without standing room, from the map's collision geometry when it is loaded.
    void measure_clearance(std::stop_token stop);

    // The area the player stands in: the highest floor at or below `position` (feet), or the nearest one within
    // `search_radius` when the point is off the mesh. -1 when none.
    [[nodiscard]] std::int32_t locate(const foundation::vec3& position, float search_radius = 128.0f) const;
    [[nodiscard]] float floor_height(std::uint32_t area_index, float x, float y) const;
    [[nodiscard]] bool contains_2d(std::uint32_t area_index, float x, float y) const;

    // A* over areas. Empty when the goal cannot be reached. `allowed` may veto single crossings (from, to);
    // `weight` scales the cost of entering an area (at least 1, so the straight-line estimate stays admissible), which
    // lets a caller vary which of several similar routes is taken.
    using link_filter = std::function<bool(std::uint32_t from, std::uint32_t to)>;
    using area_weight = std::function<float(std::uint32_t area)>;
    [[nodiscard]] std::vector<step> route(std::uint32_t start, std::uint32_t goal, const foundation::vec3& goal_point,
                                          bool allow_jumps, bool allow_ladders, const link_filter& allowed = {},
                                          const area_weight& weight = {}) const;

    [[nodiscard]] const std::vector<area>& areas() const noexcept { return m_areas; }
    [[nodiscard]] const std::vector<ladder>& ladders() const noexcept { return m_ladders; }
    // Every polygon edge once (shared edges are not repeated), for drawing the mesh.
    [[nodiscard]] const std::vector<std::pair<foundation::vec3, foundation::vec3>>& edges() const noexcept
    {
        return m_edges;
    }
    [[nodiscard]] bool empty() const noexcept { return m_areas.empty(); }

private:
    void build_links(const std::vector<std::vector<std::pair<std::uint32_t, std::uint8_t>>>& edge_links,
                     const std::vector<std::vector<std::uint32_t>>& ladder_areas_top,
                     const std::vector<std::vector<std::uint32_t>>& ladder_areas_bottom);

    void build_edges();

    std::vector<area> m_areas{};
    std::vector<ladder> m_ladders{};
    std::vector<std::pair<foundation::vec3, foundation::vec3>> m_edges{};
};

// The current map's navigation mesh; null while none is loaded. Any thread may read.
class nav_mesh_holder
{
public:
    [[nodiscard]] std::shared_ptr<const nav_graph> current() const noexcept
    {
        return m_graph.load(std::memory_order_acquire);
    }
    void replace(std::shared_ptr<const nav_graph> graph) noexcept
    {
        m_graph.store(std::move(graph), std::memory_order_release);
    }
    void clear() noexcept { replace(nullptr); }

private:
    std::atomic<std::shared_ptr<const nav_graph>> m_graph{};
};

[[nodiscard]] inline nav_mesh_holder& nav_mesh() noexcept
{
    static nav_mesh_holder value{};
    return value;
}

// Loads maps/<map>.nav from the map's archive. Null (and a journal line) when the map has none or it is unreadable.
[[nodiscard]] std::shared_ptr<const nav_graph> load_nav_mesh(const std::string& map, std::stop_token stop);
} // namespace simulation
