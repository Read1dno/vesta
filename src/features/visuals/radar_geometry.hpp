#pragma once
#include <cmath>

namespace features::visuals::detail {
[[nodiscard]] inline bool square_radar_layout(float width, float height) noexcept
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return false;
    const auto aspect = width / height;
    return aspect > 0.80f && aspect < 1.20f;
}
}
