#pragma once

// Helpers shared by the walk bot's source files.

#include <features/misc/walk_bot.hpp>
#include <render/presentation.hpp>
#include <game/input/bindings.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>
#include <algorithm>

namespace features::misc::walk_detail
{
using clock = std::chrono::steady_clock;
using move = simulation::nav_graph::move;

constexpr std::uint8_t k_movetype_ladder = 9;
constexpr float k_stand_clearance = 72.0f;
constexpr float k_eye_height = 64.0f;
constexpr float k_sample_spacing = 12.0f;  // path resolution
constexpr float k_steer_lookahead = 56.0f; // how far ahead along the path the feet aim
constexpr float k_look_ahead = 170.0f;     // how far ahead along the path the eyes aim
constexpr float k_fillet = 28.0f;          // how far from a corner the rounding starts

[[nodiscard]] inline float distance_2d(const foundation::vec3& a, const foundation::vec3& b) noexcept
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

[[nodiscard]] inline foundation::vec3 flat(foundation::vec3 value) noexcept
{
    value.z = 0.0f;
    return value;
}

[[nodiscard]] inline foundation::vec3 flat_direction(const foundation::vec3& value) noexcept
{
    auto result = flat(value);
    const auto length = result.length();
    return length > 1e-3f ? result / length : foundation::vec3{};
}

[[nodiscard]] inline float cross_2d(const foundation::vec3& a, const foundation::vec3& b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] inline float degrees(const float radians) noexcept
{
    return radians * 180.0f / std::numbers::pi_v<float>;
}

// Whether a point of the line is on the floor the player is on. In the air (a jump, a drop) the line may be well
// below the feet, never far above them.
[[nodiscard]] inline bool same_level_impl(const bool on_ground, const foundation::vec3& feet,
                                          const foundation::vec3& point)
{
    const auto dz = feet.z - point.z;
    return on_ground ? std::abs(dz) < 40.0f : dz > -32.0f && dz < 160.0f;
}

[[nodiscard]] inline bool focus_ready()
{
    return render::combat_input_ready() && !render::menu_open() && !game::input_bindings().text_entry_active();
}

// The game window itself is the one in front (its own menus may be open, the buy menu for instance), the Vesta menu is
// closed and no text field takes keys: the condition for any key the walk bot types into the game.
[[nodiscard]] inline bool game_window_in_front()
{
    const auto game = ::FindWindowW(nullptr, L"Counter-Strike 2");
    return game && ::GetAncestor(::GetForegroundWindow(), GA_ROOT) == game && !render::menu_open() &&
           !game::input_bindings().text_entry_active();
}

[[nodiscard]] inline bool recent(const std::int64_t ticks, const clock::time_point now,
                                 const std::chrono::milliseconds window)
{
    return ticks != 0 && now - clock::time_point{clock::duration{ticks}} < window;
}

// Pulls a string through a corridor of portals (each a left/right pair as seen walking) from `start` to `end`: the
// shortest line that stays inside every crossing. Returns the turning points between start and end.
[[nodiscard]] inline std::vector<foundation::vec3>
string_pull(const foundation::vec3& start, const std::vector<std::pair<foundation::vec3, foundation::vec3>>& portals,
            const foundation::vec3& end)
{
    std::vector<std::pair<foundation::vec3, foundation::vec3>> gates;
    gates.reserve(portals.size() + 2);
    gates.emplace_back(start, start);
    gates.insert(gates.end(), portals.begin(), portals.end());
    gates.emplace_back(end, end);

    std::vector<foundation::vec3> corners;
    auto apex = start, left = start, right = start;
    std::size_t apex_index = 0, left_index = 0, right_index = 0;
    for (std::size_t i = 1; i < gates.size(); ++i)
    {
        const auto& [gate_left, gate_right] = gates[i];
        // Funnel interior lies counter-clockwise of the right edge and clockwise of the left edge.
        if (cross_2d(right - apex, gate_right - apex) >= 0.0f)
        {
            if (distance_2d(apex, right) < 1e-3f || cross_2d(left - apex, gate_right - apex) < 0.0f)
            {
                right = gate_right;
                right_index = i;
            }
            else
            {
                corners.push_back(left);
                apex = left;
                apex_index = left_index;
                left = right = apex;
                left_index = right_index = apex_index;
                i = apex_index;
                continue;
            }
        }
        if (cross_2d(left - apex, gate_left - apex) <= 0.0f)
        {
            if (distance_2d(apex, left) < 1e-3f || cross_2d(right - apex, gate_left - apex) > 0.0f)
            {
                left = gate_left;
                left_index = i;
            }
            else
            {
                corners.push_back(right);
                apex = right;
                apex_index = right_index;
                left = right = apex;
                left_index = right_index = apex_index;
                i = apex_index;
                continue;
            }
        }
    }
    return corners;
}
// The point at `distance` along a resampled path (clamped to its ends).
template<class Path>
[[nodiscard]] foundation::vec3 point_at(const Path& route, const float distance)
{
    if (route.empty())
        return {};
    auto it = std::lower_bound(route.begin(), route.end(), distance,
                               [](const auto& point, const float value) { return point.distance < value; });
    if (it == route.end())
        return route.back().position;
    if (it == route.begin())
        return it->position;
    const auto& b = *it;
    const auto& a = *(it - 1);
    const auto span = std::max(b.distance - a.distance, 1e-3f);
    return a.position + (b.position - a.position) * ((distance - a.distance) / span);
}
} // namespace features::misc::walk_detail
