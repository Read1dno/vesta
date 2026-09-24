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

struct crosshair_primitive
{
    std::array<float, 4> geometry{};
    float angle{}, rotation{};
    std::uint32_t type{}, reserved{};
    std::array<float, 4> color{}, outline{};
};
struct crosshair_frame
{
    std::array<crosshair_primitive, 16> pieces{};
    std::array<float, 2> reserved{};
    std::uint32_t count{}, padding{};
};
static_assert(sizeof(crosshair_primitive) == 64);
static_assert(sizeof(crosshair_frame) == 0x410);

[[nodiscard]] std::uint32_t shot_punch_offset();
[[nodiscard]] std::uintptr_t spread_patterns();
[[nodiscard]] std::optional<radar_layout> radar();
[[nodiscard]] panel_layout panel(std::uintptr_t ui);
[[nodiscard]] bool read_crosshair(crosshair_frame& frame);
[[nodiscard]] bool crosshair_available();
[[nodiscard]] bool crosshair_dimensions(float& width, float& height);
[[nodiscard]] std::uintptr_t hud_element(std::string_view name);
int report(const char* path);

} // namespace game::compatibility
