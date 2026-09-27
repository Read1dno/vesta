#pragma once
#include <algorithm>
#include <cmath>
namespace render::menu {
[[nodiscard]] inline int wrapped_text_rows(float height, float spacing, float row_height) noexcept
{
    if (!std::isfinite(height) || !std::isfinite(spacing) || row_height <= 0) return 0;
    return static_cast<int>(std::ceil((std::max(0.0f, height) + std::max(0.0f, spacing) + 8.0f) / row_height));
}
} // namespace render::menu
