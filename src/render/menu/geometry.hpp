#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace render::menu
{

struct point
{
    float x;
    float y;
};

[[nodiscard]] inline std::array<point, 3> chevron(float opened) noexcept
{
    const float angle = std::clamp(opened, 0.0f, 1.0f) * std::numbers::pi_v<float>;
    const float c = std::cos(angle), s = std::sin(angle);
    std::array<point, 3> points{{{-4, -2}, {0, 2}, {4, -2}}};
    for (auto &p : points)
        p = {p.x * c - p.y * s, p.x * s + p.y * c};
    return points;
}

[[nodiscard]] inline float fit_popup(float requested, float lower, float upper) noexcept
{
    return std::clamp(requested, lower, std::max(lower, upper));
}

} // namespace render::menu
