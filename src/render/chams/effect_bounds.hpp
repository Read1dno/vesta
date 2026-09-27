#pragma once
#include <algorithm>

namespace chams::detail {
template<class Bounds>
[[nodiscard]] Bounds death_effect_bounds(Bounds bounds, float progress)
{
    const auto p = std::clamp(progress, 0.0f, 1.0f);
    const auto t = p * p;
    // GS_Death: radial [24,76], vertical multiplier [0.25,1.10], gravity 58.
    const auto lateral = 76.0f * t;
    bounds.mins.x -= lateral;
    bounds.mins.y -= lateral;
    bounds.maxs.x += lateral;
    bounds.maxs.y += lateral;
    bounds.mins.z -= 52.0f * t;
    bounds.maxs.z += 25.6f * t;
    return bounds;
}
}
