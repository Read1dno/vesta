#pragma once

#include <chrono>
#include <cstdint>

namespace game::detail {

inline constexpr auto pose_fallback_lifetime = std::chrono::milliseconds(100);

struct pose_identity
{
    std::uint64_t presentation{};
    std::uintptr_t controller{}, pawn{}, view_pawn{}, level{};
    std::uint32_t pawn_handle{};
    std::int32_t view_team{};
    bool team_mode{};
    bool operator==(const pose_identity&) const = default;
};

[[nodiscard]] inline bool valid_pose_identity(const pose_identity& value) noexcept
{
    return (value.presentation & 1u) && value.controller && (value.pawn || value.view_pawn);
}

[[nodiscard]] inline bool pose_frame_usable(const pose_identity& sampled,
    const pose_identity& current, std::chrono::steady_clock::time_point timestamp,
    std::chrono::steady_clock::time_point now) noexcept
{
    // Whole-frame reuse must not outlive the existing failed-bone-read allowance.
    return valid_pose_identity(current) && sampled == current && now >= timestamp
        && now - timestamp <= pose_fallback_lifetime;
}

} // namespace game::detail
