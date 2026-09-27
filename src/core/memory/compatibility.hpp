#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace game::compatibility {

struct radar_layout
{
    std::uint32_t origin{};
    std::uint32_t alternate_scale{};
};

struct panel_layout
{
    std::uint32_t width{}, height{}, x{}, y{}, visible{};
    [[nodiscard]] explicit operator bool() const { return width && height && x && y && visible; }
};

[[nodiscard]] std::uint32_t shot_punch_offset();
[[nodiscard]] std::uintptr_t spread_patterns();
[[nodiscard]] std::optional<radar_layout> radar();
[[nodiscard]] panel_layout panel(std::uintptr_t ui);
[[nodiscard]] std::uintptr_t hud_element(std::string_view name);
int report(const char* path);

} // namespace game::compatibility
