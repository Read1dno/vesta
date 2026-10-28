#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <simulation/collision.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/journal.hpp>
#include <game/state/services.hpp>
#include <unordered_map>

#include <algorithm>
#include <ranges>

namespace features::misc
{
using namespace walk_detail;

std::shared_ptr<const walk_bot_t::path> walk_bot_t::build_path(const simulation::nav_graph& graph,
                                                               const std::vector<step>& steps,
                                                               const foundation::vec3& start) const
{
    struct raw_point
    {
        foundation::vec3 position{};
        bool pinned{};
        action act{action::none};
        foundation::vec3 goal{};
        std::int32_t ladder{-1};
        std::uint32_t from{};
        std::uint32_t to{};
    };
    std::vector<raw_point> raw;
    raw.push_back({.position = start, .pinned = true});

    // Areas the route passes through, for floor heights and for keeping smoothed corners on the mesh.
    std::vector<std::uint32_t> corridor;
    for (const auto& leg : steps)
    {
        corridor.push_back(leg.from);
        corridor.push_back(leg.to);
    }
    std::ranges::sort(corridor);
    corridor.erase(std::unique(corridor.begin(), corridor.end()), corridor.end());
    const auto on_mesh = [&](const foundation::vec3& point)
    {
        return std::ranges::any_of(corridor,
                                   [&](const std::uint32_t area) { return graph.contains_2d(area, point.x, point.y); });
    };

    std::vector<std::pair<foundation::vec3, foundation::vec3>> portals;
    auto anchor = start;
    const auto close_run = [&](const foundation::vec3& end)
    {
        for (const auto& corner : string_pull(anchor, portals, end))
            raw.push_back({.position = corner});
        portals.clear();
    };

    for (std::size_t k = 0; k < steps.size(); ++k)
    {
        const auto& leg = steps[k];
        if (k + 1 == steps.size())
        {
            close_run(leg.point);
            raw.push_back({.position = leg.point, .pinned = true});
            break;
        }
        switch (leg.kind)
        {
            case move::walk:
            case move::drop:
            {
                // Orient the crossing as seen walking through it.
                const auto heading = flat(leg.landing - leg.point);
                const auto mid = (leg.portal_a + leg.portal_b) * 0.5f;
                const bool a_left = cross_2d(heading, leg.portal_a - mid) > 0.0f;
                auto left = a_left ? leg.portal_a : leg.portal_b;
                auto right = a_left ? leg.portal_b : leg.portal_a;
                // A wide crossing is narrowed towards this plan's lane, leaving at least 48 units to pass through.
                const auto width = distance_2d(left, right);
                if (width > 96.0f)
                {
                    const auto spare = width - 48.0f;
                    const auto toward = flat_direction(right - left);
                    const auto lane = m_route_lane;
                    const auto from_left = spare * 0.5f * std::max(-lane, 0.0f);
                    const auto from_right = spare * 0.5f * std::max(lane, 0.0f);
                    left = left + toward * (from_left + spare * 0.1f);
                    right = right - toward * (from_right + spare * 0.1f);
                }
                portals.emplace_back(left, right);
                break;
            }
            case move::jump:
            case move::crouch_jump:
            {
                // Take off from the part of the edge nearest the landing; the jump itself is a straight line.
                const auto edge = leg.portal_b - leg.portal_a;
                const auto length_sqr = flat(edge).length_sqr();
                const auto t =
                    length_sqr > 1e-3f
                        ? std::clamp(flat(leg.landing - leg.portal_a).dot(flat(edge)) / length_sqr, 0.0f, 1.0f)
                        : 0.5f;
                const auto takeoff = leg.portal_a + edge * t;
                close_run(takeoff);
                raw.push_back({.position = takeoff,
                               .pinned = true,
                               .act = leg.kind == move::jump ? action::jump : action::crouch_jump,
                               .goal = leg.landing,
                               .from = leg.from,
                               .to = leg.to});
                raw.push_back({.position = leg.landing, .pinned = true});
                anchor = leg.landing;
                break;
            }
            case move::ladder_up:
            case move::ladder_down:
            {
                if (leg.ladder < 0 || static_cast<std::size_t>(leg.ladder) >= graph.ladders().size())
                    break;
                const auto& ladder = graph.ladders()[static_cast<std::size_t>(leg.ladder)];
                const bool up = leg.kind == move::ladder_up;
                // Up: stand in front of the rungs. Down: stand on the upper floor right behind the top of the ladder.
                auto entry = up ? ladder.bottom + ladder.facing * 12.0f : ladder.top - ladder.facing * 20.0f;
                entry.z = graph.floor_height(leg.from, entry.x, entry.y);
                close_run(entry);
                raw.push_back({.position = entry,
                               .pinned = true,
                               .act = up ? action::ladder_up : action::ladder_down,
                               .goal = leg.landing,
                               .ladder = leg.ladder,
                               .from = leg.from,
                               .to = leg.to});
                raw.push_back({.position = leg.landing, .pinned = true});
                anchor = leg.landing;
                break;
            }
        }
    }

    // Keep turning points off the wall corners they wrap around, then round them with short fillets. The string-pull
    // corners sit exactly at the player radius from the walls; a human swings a little wider.
    std::vector<raw_point> rounded;
    rounded.reserve(raw.size() * 3);
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        auto point = raw[i];
        if (point.pinned || i == 0 || i + 1 >= raw.size() || raw[i - 1].act != action::none)
        {
            rounded.push_back(point);
            continue;
        }
        const auto in = flat_direction(raw[i - 1].position - point.position);
        const auto out = flat_direction(raw[i + 1].position - point.position);
        const auto bisector = flat_direction(in + out); // into the turn
        if (bisector.length() > 0.5f)
        {
            const auto pushed = point.position - bisector * m_route_corner_margin;
            if (on_mesh(pushed))
                point.position = pushed;
        }
        const auto before = std::min(k_fillet, distance_2d(raw[i - 1].position, point.position) * 0.45f);
        const auto after = std::min(k_fillet, distance_2d(raw[i + 1].position, point.position) * 0.45f);
        const auto p0 = point.position + flat_direction(raw[i - 1].position - point.position) * before;
        const auto p2 = point.position + flat_direction(raw[i + 1].position - point.position) * after;
        // Quadratic curve through the corner's neighbourhood: p0 -> (corner) -> p2.
        for (int s = 0; s <= 4; ++s)
        {
            const auto t = static_cast<float>(s) / 4.0f;
            const auto curve = p0 * ((1 - t) * (1 - t)) + point.position * (2 * (1 - t) * t) + p2 * (t * t);
            rounded.push_back({.position = curve});
        }
    }

    // Resample at a fixed spacing, with floor heights from the route's areas.
    auto result = std::make_shared<path>();
    const auto floor_at = [&](foundation::vec3 point, const float expected)
    {
        float best = expected;
        float best_error = 40.0f;
        for (const auto area : corridor)
        {
            if (!graph.contains_2d(area, point.x, point.y))
                continue;
            const auto floor = graph.floor_height(area, point.x, point.y);
            if (std::abs(floor - expected) < best_error)
            {
                best_error = std::abs(floor - expected);
                best = floor;
            }
        }
        point.z = best;
        return point;
    };
    float travelled = 0.0f;
    for (std::size_t i = 0; i + 1 < rounded.size(); ++i)
    {
        const auto& a = rounded[i];
        const auto& b = rounded[i + 1];
        const bool action_leg = a.act != action::none; // the jump arc or the ladder itself
        const auto length = action_leg ? a.position.distance(b.position) : distance_2d(a.position, b.position);
        const auto pieces = std::max(1, static_cast<int>(std::ceil(length / k_sample_spacing)));
        for (int s = 0; s < pieces; ++s)
        {
            const auto t = static_cast<float>(s) / static_cast<float>(pieces);
            auto position = a.position + (b.position - a.position) * t;
            if (!action_leg && s > 0)
                position = floor_at(position, position.z);
            path_point point{.position = position, .distance = travelled + length * t};
            point.in_action = action_leg && s > 0;
            if (s == 0 && action_leg)
            {
                point.act = a.act;
                point.goal = a.goal;
                point.goal_distance = travelled + length;
                point.ladder = a.ladder;
                point.from = a.from;
                point.to = a.to;
            }
            result->push_back(point);
        }
        travelled += length;
    }
    result->push_back({.position = rounded.back().position, .distance = travelled});

    // Room at the sides. The mesh knows the walls but not every prop (the crates beside the line at Dust II's suicide),
    // and a line that passes one at less than the player's half width scrapes along it. Each point is measured to both
    // sides at knee and waist height and moved over, within the route's areas, until it keeps k_side_clearance or sits
    // in the middle of a passage narrower than that. The moves are widened and smoothed along the line so it bends
    // gently around the obstacle instead of zigzagging.
    if (simulation::collision().valid() && result->size() > 4)
    {
        const auto& world = simulation::collision();
        constexpr float k_side_clearance = 24.0f; // half the player's width (16) and some
        constexpr float k_side_probe = 48.0f;
        auto& points = *result;
        std::vector<float> shift(points.size(), 0.0f);
        std::vector<foundation::vec3> sides(points.size());
        for (std::size_t i = 1; i + 1 < points.size(); ++i)
        {
            const auto direction = flat_direction(points[i + 1].position - points[i - 1].position);
            sides[i] = {-direction.y, direction.x, 0.0f}; // to the left
            if (points[i].act != action::none || points[i].in_action || points[i - 1].in_action ||
                points[i + 1].act != action::none || direction.length() < 0.5f)
                continue;
            const auto room = [&](const foundation::vec3& toward)
            {
                float nearest = k_side_probe;
                for (const float height : {20.0f, 44.0f})
                {
                    const auto from = points[i].position + foundation::vec3{0.0f, 0.0f, height};
                    const auto hit = world.trace_ray(from, from + toward * k_side_probe);
                    if (hit.hit)
                        nearest = std::min(nearest, hit.distance);
                }
                return nearest;
            };
            const auto left = room(sides[i]);
            const auto right = room(sides[i] * -1.0f);
            if (left < k_side_clearance && right > left)
                shift[i] = -std::min(k_side_clearance - left, (right - left) * 0.5f);
            else if (right < k_side_clearance && left > right)
                shift[i] = std::min(k_side_clearance - right, (left - right) * 0.5f);
        }
        // Widen (the strongest move within three samples) then smooth (average over five).
        std::vector<float> widened(shift.size(), 0.0f);
        for (std::size_t i = 0; i < shift.size(); ++i)
            for (std::size_t j = i >= 3 ? i - 3 : 0; j <= std::min(i + 3, shift.size() - 1); ++j)
                if (std::abs(shift[j]) > std::abs(widened[i]))
                    widened[i] = shift[j];
        for (std::size_t i = 1; i + 1 < points.size(); ++i)
        {
            if (points[i].act != action::none || points[i].in_action)
                continue;
            float sum = 0.0f;
            int count = 0;
            for (std::size_t j = i >= 2 ? i - 2 : 0; j <= std::min(i + 2, points.size() - 1); ++j, ++count)
                sum += widened[j];
            const auto moved = points[i].position + sides[i] * (sum / static_cast<float>(count));
            if (std::abs(sum) > 0.01f && on_mesh(moved))
                points[i].position = floor_at(moved, points[i].position.z);
        }
    }

    // Crouched stretches: measured straight up from the floor of each point. Points near a ledge or a ladder are
    // left out: a ray rising beside a ledge face could clip the ledge and read as a ceiling.
    if (simulation::collision().valid())
    {
        std::vector<std::pair<float, float>> excluded; // action spans, widened a little
        for (const auto& point : *result)
            if (point.act != action::none)
                excluded.emplace_back(point.distance - 28.0f, point.goal_distance + 28.0f);
        for (auto& point : *result)
        {
            const bool near_action =
                point.in_action ||
                std::ranges::any_of(excluded, [&](const auto& span)
                                    { return point.distance >= span.first && point.distance <= span.second; });
            if (near_action)
                continue;
            // From a step above the floor, so a polygon lying a little below the ground does not hit the ground.
            const auto from = point.position + foundation::vec3{0.0f, 0.0f, 18.0f};
            const auto hit = simulation::collision().trace_ray(from, from + foundation::vec3{0.0f, 0.0f, 62.0f});
            point.duck = hit.hit && 18.0f + hit.fraction * 62.0f < k_stand_clearance;
        }
    }
    return result;
}
float walk_bot_t::route_length(const std::vector<step>& steps, const foundation::vec3& start) const
{
    float length = 0.0f;
    auto previous = start;
    for (const auto& leg : steps)
    {
        length += previous.distance(leg.point);
        previous = leg.point;
    }
    return length;
}

void walk_bot_t::install_path(std::shared_ptr<const path> route, const foundation::vec3& destination)
{
    std::scoped_lock lock(m_route_mutex);
    m_destination = destination;
    m_has_destination = true;
    m_path = std::move(route);
    m_progress.store(0.0f, std::memory_order_release);
    m_action_index = static_cast<std::size_t>(-1);
    m_jump_attempts = 0;
    m_off_path_since = {};
    m_stuck_since = {};
    m_stuck_stage = 0;
    m_headway.reset();
}

void walk_bot_t::vary_route()
{
    m_route_seed = std::uniform_int_distribution<std::uint32_t>{}(m_random);
    m_route_lane = std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_random);
    m_route_corner_margin = std::uniform_real_distribution<float>(6.0f, 22.0f)(m_random);
}

void walk_bot_t::reset_route()
{
    std::scoped_lock lock(m_route_mutex);
    m_path.reset();
    m_has_destination = false;
    m_progress.store(0.0f, std::memory_order_release);
    m_action_index = static_cast<std::size_t>(-1);
    m_jump_attempts = 0;
    m_off_path_since = {};
    m_stuck_since = {};
    m_stuck_stage = 0;
    m_headway.reset();
}

bool walk_bot_t::plan_to(const simulation::nav_graph& graph, const player_state& player, const std::uint32_t goal_area,
                         const foundation::vec3& goal_point, const bool require_direct)
{
    const auto start = graph.locate(player.origin);
    if (start < 0)
        return false;
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto allowed = [this](const std::uint32_t from, const std::uint32_t to)
    { return !m_banned.contains({from, to}); };
    // A new character for every plan: areas cost up to 45% more at random (a different but still sensible way through),
    // a lane in wide passages and a corner width.
    vary_route();
    const auto seed = m_route_seed;
    const auto weight = [seed](const std::uint32_t area)
    {
        auto h = (area + 1u) * 0x9E3779B1u ^ seed;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        return 1.0f + 0.45f * static_cast<float>(h & 0xffffu) / 65535.0f;
    };
    const auto steps =
        graph.route(static_cast<std::uint32_t>(start), goal_area, goal_point, true, true, allowed, weight);
    if (steps.empty())
        return false;
    if (require_direct &&
        !walk_logic::route_is_direct(route_length(steps, player.origin), distance_2d(player.origin, goal_point)))
        return false;
    auto built = build_path(graph, steps, player.origin);
    if (built->size() < 2)
        return false;
    const auto end = built->back().position;
    install_path(std::move(built), end);
    diagnostics::emit(diagnostics::event::walkbot_route, diagnostics::severity::information,
                      diagnostics::code_domain::mode, static_cast<std::uint32_t>(steps.size()));
    return true;
}

// A random reachable point ahead of the player: not back where it came from, not right next to a point it already
// reached, and not so far that the walk becomes a lap of the map. A strict pass first, then a lax one.
bool walk_bot_t::plan_random(const simulation::nav_graph& graph, const player_state& player)
{
    const std::vector<foundation::vec3> recent(m_recent.begin(), m_recent.end());
    std::uniform_real_distribution<float> noise(0.0f, 1.0f);
    std::uniform_int_distribution<std::size_t> pick(0, graph.areas().size() - 1);
    for (int pass = 0; pass < 2; ++pass)
    {
        walk_logic::destination_rules rules{};
        if (pass == 1)
        {
            rules.min_distance = 400.0f;
            rules.max_distance = 4000.0f;
            rules.recent_radius = 300.0f;
            rules.behind_penalty = 0.5f;
        }
        struct candidate
        {
            std::uint32_t area{};
            float score{};
        };
        std::vector<candidate> candidates;
        for (int sample = 0; sample < 96; ++sample)
        {
            const auto index = static_cast<std::uint32_t>(pick(m_random));
            const auto& area = graph.areas()[index];
            // An end point the player can stand on comfortably and that a player would walk to: inside the map's
            // walkable space (three or more ways out of it, not a strip along a fence at the map's edge), with open
            // room around it, and not one the bot already failed to reach.
            if (area.low_ceiling || (area.maxs.x - area.mins.x) < 40.0f || (area.maxs.y - area.mins.y) < 40.0f ||
                m_unreachable.contains(index))
                continue;
            const auto ways_out = std::ranges::count_if(area.links, [](const auto& link)
                                                        { return link.kind == simulation::nav_graph::move::walk; });
            if (ways_out < 3)
                continue;
            if (const auto& world = simulation::collision(); world.valid())
            {
                int open = 0;
                const auto waist = area.center + foundation::vec3{0.0f, 0.0f, 40.0f};
                for (int direction = 0; direction < 8; ++direction)
                {
                    const auto angle = static_cast<float>(direction) * std::numbers::pi_v<float> / 4.0f;
                    const foundation::vec3 toward{std::cos(angle) * 64.0f, std::sin(angle) * 64.0f, 0.0f};
                    open += world.trace_ray(waist, waist + toward).hit ? 0 : 1;
                }
                if (open < 5)
                    continue;
            }
            if (const auto score = walk_logic::destination_score(player.origin, m_heading, area.center, recent,
                                                                 noise(m_random), rules))
                candidates.push_back({index, *score});
        }
        std::ranges::sort(candidates, [](const candidate& l, const candidate& r) { return l.score > r.score; });
        int tried = 0;
        for (const auto& option : candidates)
        {
            if (++tried > 10)
                break;
            if (plan_to(graph, player, option.area, graph.areas()[option.area].center, pass == 0))
                return true;
        }
    }
    return false;
}

// The nearest enemy, kept until another one is clearly closer; planned again when the target has moved away from the
// point the route was planned to.
bool walk_bot_t::plan_hunt(const simulation::nav_graph& graph, const player_state& player, const clock::time_point now)
{
    const auto players = game::world().players();
    std::vector<walk_logic::hunt_candidate> candidates;
    std::unordered_map<std::uintptr_t, foundation::vec3> positions;
    if (players)
        for (const auto& other : *players)
        {
            if (other.health <= 0 || !other.pawn || other.pawn == player.pawn ||
                !game::local_player().is_enemy(other.team))
                continue;
            candidates.push_back({other.pawn, other.origin.distance(player.origin)});
            positions[other.pawn] = other.origin;
        }
    const auto target = walk_logic::choose_hunt_target(candidates, m_hunt_target);
    if (!target)
    {
        m_hunt_target = 0;
        return false;
    }
    const auto where = positions[target];
    std::shared_ptr<const path> current;
    {
        std::scoped_lock lock(m_route_mutex);
        current = m_path;
    }
    const bool fresh = target != m_hunt_target || where.distance(m_hunt_planned_at) > 256.0f || !current;
    if (!fresh || now < m_next_hunt_plan)
        return current != nullptr;
    m_next_hunt_plan = now + std::chrono::milliseconds(600);
    const auto area = graph.locate(where, 160.0f);
    if (area < 0)
        return current != nullptr;
    m_hunt_target = target;
    m_hunt_planned_at = where;
    return plan_to(graph, player, static_cast<std::uint32_t>(area), where, false);
}

// A recorded route for this map and team, chosen at random: walk over the mesh to it, then follow it point by point
// with the recorded jumps and crouches.
bool walk_bot_t::plan_recording(const simulation::nav_graph& graph, const player_state& player)
{
    if (m_recordings.empty())
        return false;
    std::uniform_int_distribution<std::size_t> pick(0, m_recordings.size() - 1);
    const auto& chosen = m_recordings[pick(m_random)];
    // Join the recording at its point nearest to the player, so a route recorded from spawn also works mid-round.
    std::size_t join = 0;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < chosen.points.size(); ++i)
    {
        const auto d = chosen.points[i].position.distance(player.origin);
        if (d < best)
        {
            best = d;
            join = i;
        }
    }
    if (join + 2 >= chosen.points.size())
        join = 0;
    const auto& entry = chosen.points[join].position;

    auto combined = std::make_shared<path>();
    float travelled = 0.0f;
    if (player.origin.distance(entry) > 48.0f)
    {
        const auto area = graph.locate(entry, 96.0f);
        const auto start = graph.locate(player.origin);
        if (area < 0 || start < 0)
            return false;
        const auto snapshot = config::get_runtime_snapshot();
        const auto& cfg = snapshot->general.m_walk_bot;
        const auto steps =
            graph.route(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(area), entry, true, true);
        if (steps.empty())
            return false;
        const auto approach = build_path(graph, steps, player.origin);
        combined->assign(approach->begin(), approach->end());
        travelled = combined->empty() ? 0.0f : combined->back().distance;
    }
    if (combined->empty())
        combined->push_back({.position = player.origin});
    auto previous = combined->back().position;
    for (std::size_t i = join; i < chosen.points.size(); ++i)
    {
        const auto& recorded = chosen.points[i];
        travelled += previous.distance(recorded.position);
        previous = recorded.position;
        path_point point{.position = recorded.position, .distance = travelled};
        point.duck = (recorded.flags & walk_logic::recorded_point::duck) != 0;
        if ((recorded.flags & walk_logic::recorded_point::jump) && i + 1 < chosen.points.size())
        {
            point.act = point.duck ? action::crouch_jump : action::jump;
            point.goal = chosen.points[i + 1].position;
            point.goal_distance = travelled + recorded.position.distance(point.goal);
            point.from = point.to = 0xffffffffu; // recorded jumps are never banned
        }
        combined->push_back(point);
    }
    if (combined->size() < 2)
        return false;
    const auto end = combined->back().position;
    install_path(std::move(combined), end);
    return true;
}
} // namespace features::misc
