#pragma once

#include <core/math/vector.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace simulation::native_velocity {
struct offsets {
    std::ptrdiff_t eflags{};
    std::ptrdiff_t absolute{};
    std::ptrdiff_t network{};
    std::ptrdiff_t scene_node{};
    std::ptrdiff_t parent{};
};

template<class Reader>
[[nodiscard]] bool read(const Reader& reader, std::uintptr_t pawn,
    const offsets& fields, foundation::vec3& value) noexcept
{
    if (!pawn || fields.eflags <= 0 || fields.absolute <= 0 || fields.network <= 0
        || fields.scene_node <= 0 || fields.parent <= 0) return false;
    constexpr std::uint32_t dirty_absolute_velocity = 0x1000;
    std::uint32_t before{}, after{};
    if (!reader.copy(pawn + fields.eflags, &before, sizeof(before))) return false;
    const bool dirty = (before & dirty_absolute_velocity) != 0;
    if (dirty) {
        std::uintptr_t node{}, parent{};
        if (!reader.copy(pawn + fields.scene_node, &node, sizeof(node)) || !node
            || !reader.copy(node + fields.parent, &parent, sizeof(parent)) || parent)
            return false;
    }
    const auto source = dirty ? fields.network : fields.absolute;
    if (!reader.copy(pawn + source, &value, sizeof(value))
        || !reader.copy(pawn + fields.eflags, &after, sizeof(after))
        || ((before ^ after) & dirty_absolute_velocity)
        || !std::isfinite(value.x) || !std::isfinite(value.y)
        || !std::isfinite(value.z)) return false;
    return true;
}
} // namespace simulation::native_velocity
