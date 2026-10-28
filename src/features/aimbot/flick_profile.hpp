#pragma once

// A flick as a person's hand makes it. The duration grows with the logarithm of the angle (Fitts' law: a 180 degree
// turn takes about twice as long as a 10 degree one) and the speed follows the minimum-jerk profile: a smooth start, the
// peak in the middle, a smooth stop. `speed` scales it (1 is the reference hand).

#include <algorithm>
#include <cmath>

namespace features::aimbot::flick
{
[[nodiscard]] inline float duration(const float degrees, const float speed) noexcept
{
    return std::clamp((0.05f + 0.032f * std::log2(1.0f + std::max(degrees, 0.0f) * 0.5f)) / std::max(speed, 0.05f),
                      0.05f, 0.6f);
}

// Position along the movement, 0..1, at time fraction t (0..1).
[[nodiscard]] constexpr float minimum_jerk(const float t) noexcept
{
    return t * t * t * (10.0f + t * (-15.0f + 6.0f * t));
}

// The share of what is still left to cover in the step from progress `from` to `to`: applied to the remaining angle
// each step, it keeps the timing while following a target that moves.
[[nodiscard]] constexpr float share(const float from, const float to) noexcept
{
    const auto left = 1.0f - minimum_jerk(from);
    return left > 1e-4f ? (minimum_jerk(to) - minimum_jerk(from)) / left : 1.0f;
}
} // namespace features::aimbot::flick
