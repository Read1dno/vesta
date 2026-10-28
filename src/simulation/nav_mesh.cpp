#include <stdafx.hpp>
#include <simulation/nav_mesh.hpp>
#include <simulation/collision.hpp>
#include <assets/vpk.hpp>
#include <game/memory/bound_services.hpp>
#include <diagnostics/journal.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace simulation
{
namespace
{
// Player movement limits the route is planned against (CS2 units).
constexpr float k_step_height = 18.0f;        // walked up without a jump
constexpr float k_jump_height = 32.0f;        // above this the jump needs a crouch
constexpr float k_crouch_jump_height = 64.0f; // above this the ledge cannot be climbed: the route goes around
constexpr float k_gap_jump = 24.0f;           // a horizontal gap wider than this is jumped
constexpr float k_stand_clearance = 72.0f;    // standing hull height
constexpr float k_crouch_clearance = 55.0f;   // crouched hull height (with a unit of slack)
constexpr float k_player_radius = 16.0f;

constexpr std::uint32_t k_nav_magic = 0xFEEDFACEu;

class reader
{
public:
    explicit reader(const std::vector<std::uint8_t>& data)
        : m_data(data)
    {
    }

    template<class T>
    [[nodiscard]] bool read(T& out)
    {
        if (m_offset + sizeof(T) > m_data.size())
            return false;
        std::memcpy(&out, m_data.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }
    [[nodiscard]] bool skip(const std::size_t bytes)
    {
        if (m_offset + bytes > m_data.size())
            return false;
        m_offset += bytes;
        return true;
    }
    void align(const std::size_t to) { m_offset = (m_offset + to - 1) / to * to; }
    [[nodiscard]] std::size_t offset() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return m_offset < m_data.size() ? m_data.size() - m_offset : 0;
    }

    // Embedded KV3 blocks carry generation settings this reader does not need; their header gives their length.
    [[nodiscard]] bool kv3_here() const
    {
        std::uint32_t magic{};
        if (m_offset + 4 > m_data.size())
            return false;
        std::memcpy(&magic, m_data.data() + m_offset, 4);
        return (magic & 0xFFFFFF00u) == 0x4B563300u;
    }
    [[nodiscard]] bool skip_kv3()
    {
        constexpr std::size_t k_header = 120; // KV3 v5 header, magic included
        if (!kv3_here() || m_offset + k_header > m_data.size() || (m_data[m_offset] & 0xFF) != 5)
            return false;
        std::int32_t compressed_total{}, blob_blocks{};
        std::memcpy(&compressed_total, m_data.data() + m_offset + 52, 4);
        std::memcpy(&blob_blocks, m_data.data() + m_offset + 56, 4);
        if (compressed_total < 0 || blob_blocks != 0)
            return false;
        return skip(k_header + static_cast<std::size_t>(compressed_total));
    }

private:
    const std::vector<std::uint8_t>& m_data;
    std::size_t m_offset{};
};

[[nodiscard]] float cross_2d(const float ax, const float ay, const float bx, const float by) noexcept
{
    return ax * by - ay * bx;
}

[[nodiscard]] float distance_2d(const foundation::vec3& a, const foundation::vec3& b) noexcept
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Closest point of segment ab to p, in the horizontal plane; t in [0,1] along ab.
[[nodiscard]] float closest_t(const foundation::vec3& a, const foundation::vec3& b, const foundation::vec3& p) noexcept
{
    const auto dx = b.x - a.x, dy = b.y - a.y;
    const auto length_sqr = dx * dx + dy * dy;
    if (length_sqr <= 1e-6f)
        return 0.0f;
    return std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length_sqr, 0.0f, 1.0f);
}

[[nodiscard]] foundation::vec3 lerp(const foundation::vec3& a, const foundation::vec3& b, const float t) noexcept
{
    return a + (b - a) * t;
}

// Free height above a floor point: a ray straight up from a step above the floor, so a polygon lying a little below
// the real ground does not read the ground itself as a ceiling.
[[nodiscard]] float ceiling_clearance(const collision_world& world, const foundation::vec3& floor)
{
    constexpr float k_start = 18.0f;
    constexpr float k_probe = 62.0f;
    const auto start = floor + foundation::vec3{0.0f, 0.0f, k_start};
    const auto hit = world.trace_ray(start, start + foundation::vec3{0.0f, 0.0f, k_probe});
    return hit.hit ? k_start + hit.fraction * k_probe : k_start + k_probe;
}
} // namespace

bool nav_graph::parse(const std::vector<std::uint8_t>& data, std::string& error)
{
    m_areas.clear();
    m_ladders.clear();
    reader in{data};
    std::uint32_t magic{}, version{}, sub_version{}, header_flags{};
    if (!in.read(magic) || magic != k_nav_magic)
        return error = "not a navigation mesh", false;
    if (!in.read(version) || version < 35 || version > 36)
        return error = "unsupported navigation mesh version " + std::to_string(version), false;
    if (!in.read(sub_version) || !in.read(header_flags))
        return error = "truncated header", false;
    if (in.kv3_here() && !in.skip_kv3())
        return error = "unreadable header settings", false;

    std::uint32_t corner_count{};
    if (!in.read(corner_count) || corner_count > 4'000'000 || in.remaining() < corner_count * 12ull)
        return error = "bad corner table", false;
    std::vector<foundation::vec3> corners(corner_count);
    for (auto& corner : corners)
        if (!in.read(corner.x) || !in.read(corner.y) || !in.read(corner.z))
            return error = "truncated corner table", false;

    std::uint32_t polygon_count{};
    if (!in.read(polygon_count) || polygon_count > 4'000'000)
        return error = "bad polygon table", false;
    std::vector<std::vector<std::uint32_t>> polygons(polygon_count);
    for (auto& polygon : polygons)
    {
        std::uint8_t count{};
        if (!in.read(count) || count < 3)
            return error = "bad polygon", false;
        polygon.resize(count);
        for (auto& index : polygon)
            if (!in.read(index) || index >= corner_count)
                return error = "polygon corner out of range", false;
        if (!in.skip(4))
            return error = "truncated polygon table", false;
    }
    // Two reserved words, then the area settings block on an 8-byte boundary.
    if (!in.skip(8))
        return error = "truncated area settings", false;
    in.align(8);
    if (!in.skip_kv3())
        return error = "unreadable area settings", false;

    std::uint32_t area_count{};
    if (!in.read(area_count) || area_count > 1'000'000)
        return error = "bad area count", false;
    std::vector<std::vector<std::pair<std::uint32_t, std::uint8_t>>> edge_links; // per area: (target id, own edge)
    std::vector<std::uint8_t> hulls;
    m_areas.reserve(area_count);
    edge_links.reserve(area_count);
    for (std::uint32_t i = 0; i < area_count; ++i)
    {
        area value{};
        std::uint8_t hull{};
        std::uint32_t polygon{}, reserved{};
        if (!in.read(value.id) || !in.read(value.flags) || !in.read(hull) || !in.read(polygon) || !in.read(reserved) ||
            polygon >= polygon_count)
            return error = "bad area record", false;
        for (const auto index : polygons[polygon])
            value.corners.push_back(corners[index]);

        std::vector<std::pair<std::uint32_t, std::uint8_t>> links;
        for (std::size_t edge = 0; edge < value.corners.size(); ++edge)
        {
            std::uint32_t count{};
            if (!in.read(count) || count > 4096)
                return error = "bad area connections", false;
            for (std::uint32_t c = 0; c < count; ++c)
            {
                std::uint32_t target{}, target_edge{};
                if (!in.read(target) || !in.read(target_edge))
                    return error = "truncated area connections", false;
                links.emplace_back(target, static_cast<std::uint8_t>(edge));
            }
        }
        std::uint8_t hiding_spots{};
        if (!in.read(hiding_spots) || !in.skip(hiding_spots * 17ull))
            return error = "bad hiding spots", false;
        std::uint32_t encounter_paths{};
        if (!in.read(encounter_paths) || encounter_paths != 0)
            return error = "unsupported encounter paths", false;
        for (int direction = 0; direction < 2; ++direction) // ladders above, then below; the ladder records say it too
        {
            std::uint32_t count{};
            if (!in.read(count) || count > 64 || !in.skip(count * 4ull))
                return error = "bad area ladders", false;
        }

        value.mins = value.maxs = value.corners.front();
        foundation::vec3 sum{};
        foundation::vec3 newell{};
        for (std::size_t c = 0; c < value.corners.size(); ++c)
        {
            const auto& a = value.corners[c];
            const auto& b = value.corners[(c + 1) % value.corners.size()];
            sum += a;
            value.mins = {std::min(value.mins.x, a.x), std::min(value.mins.y, a.y), std::min(value.mins.z, a.z)};
            value.maxs = {std::max(value.maxs.x, a.x), std::max(value.maxs.y, a.y), std::max(value.maxs.z, a.z)};
            newell += foundation::vec3{(a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)};
        }
        value.center = sum / static_cast<float>(value.corners.size());
        if (newell.z < 0.0f)
            newell = -newell;
        value.normal = newell.length() > 1e-4f && newell.z > 1e-3f ? newell.normalized() : foundation::vec3{0, 0, 1};
        m_areas.push_back(std::move(value));
        edge_links.push_back(std::move(links));
        hulls.push_back(hull);
    }

    std::uint32_t ladder_count{};
    if (!in.read(ladder_count) || ladder_count > 4096)
        return error = "bad ladder table", false;
    std::vector<std::vector<std::uint32_t>> ladder_top(ladder_count), ladder_bottom(ladder_count);
    m_ladders.resize(ladder_count);
    for (std::uint32_t i = 0; i < ladder_count; ++i)
    {
        auto& value = m_ladders[i];
        float length{};
        std::uint32_t direction{};
        if (!in.read(value.id) || !in.read(value.width) || !in.read(value.top.x) || !in.read(value.top.y) ||
            !in.read(value.top.z) || !in.read(value.bottom.x) || !in.read(value.bottom.y) || !in.read(value.bottom.z) ||
            !in.read(length) || !in.read(direction))
            return error = "truncated ladder", false;
        for (int slot = 0; slot < 7; ++slot) // four areas at the top, three at the bottom
        {
            std::uint32_t id{};
            if (!in.read(id))
                return error = "truncated ladder", false;
            if (id)
                (slot < 4 ? ladder_top[i] : ladder_bottom[i]).push_back(id);
        }
        // Facing from the record's compass direction (north is -y); corrected below from the bottom area's side.
        static constexpr float k_facing[4][2]{{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};
        value.facing =
            direction < 4 ? foundation::vec3{k_facing[direction][0], k_facing[direction][1], 0.0f} : foundation::vec3{};
    }

    // Only the standing-player hull is used.
    std::vector<area> kept;
    std::vector<std::vector<std::pair<std::uint32_t, std::uint8_t>>> kept_links;
    for (std::size_t i = 0; i < m_areas.size(); ++i)
    {
        if (hulls[i] != 0)
            continue;
        kept.push_back(std::move(m_areas[i]));
        kept_links.push_back(std::move(edge_links[i]));
    }
    m_areas = std::move(kept);
    if (m_areas.empty())
        return error = "no walkable areas", false;
    build_links(kept_links, ladder_top, ladder_bottom);
    build_edges();
    return true;
}

void nav_graph::build_edges()
{
    // Corner positions rounded to a quarter unit identify a shared edge from either side.
    const auto key = [](const foundation::vec3& v)
    { return std::tuple{std::lround(v.x * 4.0f), std::lround(v.y * 4.0f), std::lround(v.z * 4.0f)}; };
    std::vector<std::tuple<long, long, long, long, long, long>> seen;
    for (const auto& a : m_areas)
        for (std::size_t c = 0; c < a.corners.size(); ++c)
        {
            auto p = key(a.corners[c]);
            auto q = key(a.corners[(c + 1) % a.corners.size()]);
            if (q < p)
                std::swap(p, q);
            seen.emplace_back(std::get<0>(p), std::get<1>(p), std::get<2>(p), std::get<0>(q), std::get<1>(q),
                              std::get<2>(q));
            m_edges.emplace_back(a.corners[c], a.corners[(c + 1) % a.corners.size()]);
        }
    std::vector<std::size_t> order(seen.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::ranges::sort(order, [&](const std::size_t l, const std::size_t r) { return seen[l] < seen[r]; });
    std::vector<std::pair<foundation::vec3, foundation::vec3>> unique;
    unique.reserve(m_edges.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        if (i == 0 || seen[order[i]] != seen[order[i - 1]])
            unique.push_back(m_edges[order[i]]);
    m_edges = std::move(unique);
}

float nav_graph::floor_height(const std::uint32_t area_index, const float x, const float y) const
{
    const auto& a = m_areas[area_index];
    const auto& n = a.normal;
    if (n.z < 0.05f)
        return a.center.z;
    return a.center.z - (n.x * (x - a.center.x) + n.y * (y - a.center.y)) / n.z;
}

bool nav_graph::contains_2d(const std::uint32_t area_index, const float x, const float y) const
{
    const auto& a = m_areas[area_index];
    if (x < a.mins.x - 0.5f || x > a.maxs.x + 0.5f || y < a.mins.y - 0.5f || y > a.maxs.y + 0.5f)
        return false;
    bool inside = false;
    const auto count = a.corners.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++)
    {
        const auto& p = a.corners[i];
        const auto& q = a.corners[j];
        if ((p.y > y) != (q.y > y) && x < (q.x - p.x) * (y - p.y) / (q.y - p.y) + p.x)
            inside = !inside;
    }
    return inside;
}

std::int32_t nav_graph::locate(const foundation::vec3& position, const float search_radius) const
{
    std::int32_t best = -1;
    float best_floor = -std::numeric_limits<float>::max();
    for (std::uint32_t i = 0; i < m_areas.size(); ++i)
    {
        if (!contains_2d(i, position.x, position.y))
            continue;
        const auto floor = floor_height(i, position.x, position.y);
        // Feet may sit a step above the floor polygon (stairs) and the polygon may float a little above the ground.
        if (floor <= position.z + k_step_height && position.z - floor < 96.0f && floor > best_floor)
        {
            best_floor = floor;
            best = static_cast<std::int32_t>(i);
        }
    }
    if (best >= 0 || search_radius <= 0.0f)
        return best;

    float best_distance = search_radius;
    for (std::uint32_t i = 0; i < m_areas.size(); ++i)
    {
        const auto& a = m_areas[i];
        float edge_distance = std::numeric_limits<float>::max();
        for (std::size_t c = 0; c < a.corners.size(); ++c)
        {
            const auto& p = a.corners[c];
            const auto& q = a.corners[(c + 1) % a.corners.size()];
            const auto point = lerp(p, q, closest_t(p, q, position));
            edge_distance = std::min(edge_distance, distance_2d(point, position));
        }
        const auto vertical = std::abs(floor_height(i, position.x, position.y) - position.z);
        const auto score = edge_distance + vertical * 0.5f;
        if (score < best_distance)
        {
            best_distance = score;
            best = static_cast<std::int32_t>(i);
        }
    }
    return best;
}

void nav_graph::build_links(const std::vector<std::vector<std::pair<std::uint32_t, std::uint8_t>>>& edge_links,
                            const std::vector<std::vector<std::uint32_t>>& ladder_areas_top,
                            const std::vector<std::vector<std::uint32_t>>& ladder_areas_bottom)
{
    std::unordered_map<std::uint32_t, std::uint32_t> index_of;
    index_of.reserve(m_areas.size());
    for (std::uint32_t i = 0; i < m_areas.size(); ++i)
        index_of.emplace(m_areas[i].id, i);

    for (std::uint32_t i = 0; i < m_areas.size(); ++i)
    {
        auto& from = m_areas[i];
        for (const auto& [target_id, edge] : edge_links[i])
        {
            const auto found = index_of.find(target_id);
            if (found == index_of.end() || found->second == i || edge >= from.corners.size())
                continue;
            const auto target = found->second;
            const auto& to = m_areas[target];
            const auto& a = from.corners[edge];
            const auto& b = from.corners[(edge + 1) % from.corners.size()];
            const auto edge_length = distance_2d(a, b);

            // The part of this edge the target lies along, narrowed by the player's radius where it is wide enough.
            float lo = 1.0f, hi = 0.0f;
            for (const auto& corner : to.corners)
            {
                const auto t = closest_t(a, b, corner);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            if (hi < lo)
                lo = hi = closest_t(a, b, to.center);
            const auto margin = edge_length > 1.0f ? k_player_radius / edge_length : 0.0f;
            if (hi - lo > 2.0f * margin)
            {
                lo += margin;
                hi -= margin;
            }
            else
                lo = hi = (lo + hi) * 0.5f;
            const auto portal = lerp(a, b, (lo + hi) * 0.5f);
            const auto portal_a = lerp(a, b, lo);
            const auto portal_b = lerp(a, b, hi);

            // Height step and horizontal gap measured where the route crosses.
            const auto target_floor = floor_height(target, portal.x, portal.y);
            const auto rise = target_floor - portal.z;
            float gap = 0.0f;
            if (!contains_2d(target, portal.x, portal.y))
            {
                gap = std::numeric_limits<float>::max();
                for (std::size_t c = 0; c < to.corners.size(); ++c)
                {
                    const auto& p = to.corners[c];
                    const auto& q = to.corners[(c + 1) % to.corners.size()];
                    gap = std::min(gap, distance_2d(lerp(p, q, closest_t(p, q, portal)), portal));
                }
            }

            link value{};
            value.target = target;
            value.portal = portal;
            value.portal_a = portal_a;
            value.portal_b = portal_b;
            value.rise = rise;
            // Where the feet come down: the target's nearest boundary point to the crossing, a little inside it.
            {
                foundation::vec3 nearest = to.center;
                float nearest_distance = std::numeric_limits<float>::max();
                for (std::size_t c = 0; c < to.corners.size(); ++c)
                {
                    const auto& p = to.corners[c];
                    const auto& q = to.corners[(c + 1) % to.corners.size()];
                    const auto point = lerp(p, q, closest_t(p, q, portal));
                    if (const auto d = distance_2d(point, portal); d < nearest_distance)
                    {
                        nearest_distance = d;
                        nearest = point;
                    }
                }
                auto inward = to.center - nearest;
                inward.z = 0.0f;
                const auto inward_length = inward.length();
                auto landing = inward_length > 1e-3f
                                   ? nearest + inward * (std::min(20.0f, inward_length) / inward_length)
                                   : to.center;
                landing.z = floor_height(target, landing.x, landing.y);
                value.landing = landing;
            }
            if (rise > k_crouch_jump_height)
                continue; // too high to climb: the planner has to go around
            if (rise > k_jump_height)
                value.kind = move::crouch_jump;
            else if (rise > k_step_height || gap > k_gap_jump)
                value.kind = move::jump;
            else if (rise < -k_step_height)
                value.kind = move::drop;
            from.links.push_back(value);
        }
    }

    for (std::size_t l = 0; l < m_ladders.size(); ++l)
    {
        auto& ladder = m_ladders[l];
        std::vector<std::uint32_t> top, bottom;
        for (const auto id : ladder_areas_top[l])
            if (const auto found = index_of.find(id); found != index_of.end())
                top.push_back(found->second);
        for (const auto id : ladder_areas_bottom[l])
            if (const auto found = index_of.find(id);
                found != index_of.end() && std::ranges::find(bottom, found->second) == bottom.end())
                bottom.push_back(found->second);
        // The ladder is climbed from the side its bottom area is on.
        if (!bottom.empty())
        {
            auto side = m_areas[bottom.front()].center - ladder.bottom;
            side.z = 0.0f;
            if (side.length() > 1.0f)
                ladder.facing = side.normalized();
        }
        // Off the top: into the upper area itself, at its nearest part to the ladder top (it can lie behind the rungs,
        // to a side, or in front of a ladder that ends in a shaft), 20 units in. Assuming "behind the rungs" sent the
        // player into walls and ceilings where the exit is elsewhere.
        const auto top_exit_into = [&](const std::uint32_t area)
        {
            const auto& target = m_areas[area];
            auto toward = target.center - ladder.top;
            toward.z = 0.0f;
            const auto length = toward.length();
            foundation::vec3 exit = target.center;
            if (length > 1.0f)
            {
                const auto direction = toward * (1.0f / length);
                for (float along = 0.0f; along <= length; along += 4.0f)
                {
                    const auto probe = ladder.top + direction * along;
                    if (contains_2d(area, probe.x, probe.y))
                    {
                        exit = ladder.top + direction * std::min(along + 20.0f, length);
                        break;
                    }
                }
            }
            exit.z = floor_height(area, exit.x, exit.y);
            return exit;
        };
        for (const auto b : bottom)
            for (const auto t : top)
            {
                // Off the bottom: a step back from the ladder.
                const auto top_exit = top_exit_into(t);
                auto bottom_exit = ladder.bottom + ladder.facing * 24.0f;
                bottom_exit.z = floor_height(b, bottom_exit.x, bottom_exit.y);
                m_areas[b].links.push_back({.target = t,
                                            .portal = ladder.bottom,
                                            .portal_a = ladder.bottom,
                                            .portal_b = ladder.bottom,
                                            .landing = top_exit,
                                            .rise = ladder.top.z - ladder.bottom.z,
                                            .ladder = static_cast<std::int32_t>(l),
                                            .kind = move::ladder_up});
                m_areas[t].links.push_back({.target = b,
                                            .portal = ladder.top,
                                            .portal_a = ladder.top,
                                            .portal_b = ladder.top,
                                            .landing = bottom_exit,
                                            .rise = ladder.bottom.z - ladder.top.z,
                                            .ladder = static_cast<std::int32_t>(l),
                                            .kind = move::ladder_down});
            }
    }
}

void nav_graph::measure_clearance(const std::stop_token stop)
{
    const auto& world = collision();
    if (!world.valid())
        return;
    for (auto& from : m_areas)
    {
        if (stop.stop_requested())
            return;
        float lowest = ceiling_clearance(world, from.center);
        for (const auto& corner : from.corners)
            lowest = std::min(lowest, ceiling_clearance(world, lerp(from.center, corner, 0.75f)));
        from.low_ceiling = lowest >= k_crouch_clearance && lowest < k_stand_clearance;

        std::erase_if(from.links,
                      [&](link& value)
                      {
                          if (value.kind == move::ladder_up || value.kind == move::ladder_down)
                          {
                              // Inside the ladder shaft: the free height at the bottom and at the top exit.
                              const auto& ladder = m_ladders[static_cast<std::size_t>(value.ladder)];
                              const auto bottom = ceiling_clearance(world, ladder.bottom + ladder.facing * 8.0f);
                              const auto top = ceiling_clearance(world, ladder.top - ladder.facing * 8.0f);
                              const auto lowest_ladder = std::min(bottom, top);
                              value.low_ceiling = lowest_ladder < k_stand_clearance;
                              return lowest_ladder < k_crouch_clearance;
                          }
                          const auto& to = m_areas[value.target];
                          auto direction = to.center - from.center;
                          direction.z = 0.0f;
                          if (direction.length() > 1.0f)
                              direction.normalize();
                          // A ledge is measured on each floor away from its face: a ray from the low floor at the
                          // face would hit the ledge itself and read as a low ceiling.
                          if (std::abs(value.rise) > k_step_height || value.kind == move::jump)
                          {
                              auto before = value.portal - direction * 16.0f;
                              before.z =
                                  floor_height(static_cast<std::uint32_t>(&from - m_areas.data()), before.x, before.y);
                              const auto clearance =
                                  std::min(ceiling_clearance(world, before), ceiling_clearance(world, value.landing));
                              value.low_ceiling = clearance < k_stand_clearance;
                              return clearance < k_crouch_clearance;
                          }
                          // At the crossing and a little to either side of it along the way.
                          float clearance = ceiling_clearance(world, value.portal);
                          for (const float offset : {-12.0f, 12.0f})
                          {
                              auto point = value.portal + direction * offset;
                              const auto owner =
                                  offset < 0.0f ? static_cast<std::uint32_t>(&from - m_areas.data()) : value.target;
                              point.z = floor_height(owner, point.x, point.y);
                              clearance = std::min(clearance, ceiling_clearance(world, point));
                          }
                          value.low_ceiling = clearance < k_stand_clearance;
                          return clearance < k_crouch_clearance;
                      });
    }
}

std::vector<nav_graph::step> nav_graph::route(const std::uint32_t start, const std::uint32_t goal,
                                              const foundation::vec3& goal_point, const bool allow_jumps,
                                              const bool allow_ladders, const link_filter& allowed,
                                              const area_weight& weight) const
{
    if (start >= m_areas.size() || goal >= m_areas.size())
        return {};
    const auto count = m_areas.size();
    std::vector<float> cost(count, std::numeric_limits<float>::max());
    std::vector<std::int32_t> came_from(count, -1);
    std::vector<std::int32_t> via(count, -1); // link index in came_from's area
    using entry = std::pair<float, std::uint32_t>;
    std::priority_queue<entry, std::vector<entry>, std::greater<>> open;
    cost[start] = 0.0f;
    open.emplace(m_areas[start].center.distance(goal_point), start);
    while (!open.empty())
    {
        const auto [estimate, current] = open.top();
        open.pop();
        if (current == goal)
            break;
        const auto& area = m_areas[current];
        if (estimate - area.center.distance(goal_point) > cost[current] + 0.01f)
            continue; // stale entry
        for (std::size_t l = 0; l < area.links.size(); ++l)
        {
            const auto& value = area.links[l];
            const bool ladder = value.kind == move::ladder_up || value.kind == move::ladder_down;
            const bool jump = value.kind == move::jump || value.kind == move::crouch_jump;
            if ((ladder && !allow_ladders) || (jump && !allow_jumps) || (allowed && !allowed(current, value.target)))
                continue;
            const auto& to = m_areas[value.target];
            float step_cost = area.center.distance(value.portal) + value.portal.distance(to.center);
            if (value.kind == move::jump)
                step_cost += 32.0f;
            else if (value.kind == move::crouch_jump)
                step_cost += 64.0f;
            else if (ladder)
                step_cost += std::abs(value.rise) + 96.0f;
            if (value.low_ceiling || to.low_ceiling)
                step_cost *= 1.6f; // crouched movement is slower
            if (weight)
                step_cost *= std::max(weight(value.target), 1.0f);
            const auto next = cost[current] + step_cost;
            if (next >= cost[value.target])
                continue;
            cost[value.target] = next;
            came_from[value.target] = static_cast<std::int32_t>(current);
            via[value.target] = static_cast<std::int32_t>(l);
            open.emplace(next + to.center.distance(goal_point), value.target);
        }
    }
    if (start != goal && came_from[goal] < 0)
        return {};

    std::vector<step> steps;
    for (auto current = goal; current != start;)
    {
        const auto previous = static_cast<std::uint32_t>(came_from[current]);
        const auto& value = m_areas[previous].links[static_cast<std::size_t>(via[current])];
        steps.push_back({.from = previous,
                         .to = current,
                         .point = value.portal,
                         .portal_a = value.portal_a,
                         .portal_b = value.portal_b,
                         .landing = value.landing,
                         .rise = value.rise,
                         .ladder = value.ladder,
                         .kind = value.kind,
                         .low_ceiling = value.low_ceiling});
        current = previous;
    }
    std::ranges::reverse(steps);
    steps.push_back({.from = goal,
                     .to = goal,
                     .point = goal_point,
                     .portal_a = goal_point,
                     .portal_b = goal_point,
                     .landing = goal_point,
                     .kind = move::walk});
    return steps;
}

// The game reports the map as a path ("maps/de_mirage.vpk" style); the archive and entry use the bare name, the same
// normalisation the geometry loader applies.
[[nodiscard]] std::string bare_map_name(std::string s)
{
    for (auto& c : s)
    {
        if (c == '\\')
            c = '/';
    }
    if (const auto slash = s.find_last_of('/'); slash != std::string::npos)
        s = s.substr(slash + 1);
    if (const auto dot = s.find_last_of('.'); dot != std::string::npos)
        s.resize(dot);
    return s;
}

std::shared_ptr<const nav_graph> load_nav_mesh(const std::string& raw_map, const std::stop_token stop)
{
    const auto map = bare_map_name(raw_map);
    if (map.empty())
        return nullptr;
    const auto vpk_path = assets::vpk_archive::locate_map_vpk(map);
    assets::vpk_archive vpk{};
    if (vpk_path.empty() || !vpk.open(vpk_path))
    {
        diagnostics::emit(diagnostics::event::navmesh_unavailable, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 1);
        return nullptr;
    }
    const auto* entry = vpk.find("maps/" + map + ".nav");
    if (!entry)
    {
        diagnostics::emit(diagnostics::event::navmesh_unavailable, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 2);
        return nullptr;
    }
    const auto bytes = vpk.read(*entry);
    auto graph = std::make_shared<nav_graph>();
    std::string error;
    if (!graph->parse(bytes, error))
    {
        diagnostics::emit(diagnostics::event::navmesh_unavailable, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 3);
        return nullptr;
    }
    graph->measure_clearance(stop);
    if (stop.stop_requested())
        return nullptr;
    diagnostics::emit(diagnostics::event::navmesh_ready, diagnostics::severity::information,
                      diagnostics::code_domain::mode, static_cast<std::uint32_t>(graph->areas().size()));
    return graph;
}
} // namespace simulation
