#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace game::detail {
struct snapshot_epoch {
    std::uintptr_t scene{}, bones{};
    std::int32_t client_tick{-1};
    float simulation_time{};
};
struct snapshot_epoch_offsets {
    std::int32_t scene{}, bones{}, client_tick{}, simulation_time{};
};
inline bool valid_epoch(const snapshot_epoch& value) noexcept
{
    // Remote pawns can expose -1 here; it is not a ReadProcessMemory failure.
    return value.scene && value.bones && value.client_tick >= -1
        && std::isfinite(value.simulation_time) && value.simulation_time >= 0;
}
inline bool same_epoch(const snapshot_epoch& before, const snapshot_epoch& after) noexcept
{
    return valid_epoch(before) && valid_epoch(after)
        && before.scene == after.scene && before.bones == after.bones
        && before.client_tick == after.client_tick
        && before.simulation_time == after.simulation_time;
}
template<class Reader>
std::optional<snapshot_epoch> read_epoch(std::uintptr_t pawn,
    const snapshot_epoch_offsets& offsets, Reader&& read)
{
    if (!pawn || offsets.scene <= 0 || offsets.bones <= 0
        || offsets.client_tick <= 0 || offsets.simulation_time <= 0) return {};
    snapshot_epoch value;
    if (!read(pawn + offsets.scene, &value.scene, sizeof(value.scene)) || !value.scene
        || !read(pawn + offsets.client_tick, &value.client_tick, sizeof(value.client_tick))
        || !read(pawn + offsets.simulation_time, &value.simulation_time, sizeof(value.simulation_time))
        || !read(value.scene + offsets.bones, &value.bones, sizeof(value.bones))
        || !valid_epoch(value)) return {};
    return value;
}
}
