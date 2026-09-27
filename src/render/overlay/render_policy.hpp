#pragma once
#include <cstdint>

namespace render {
inline constexpr int overlay_gpu_priority = -2;
struct feature_plan { bool sample_players{}; bool gpu_effects{}; };
[[nodiscard]] constexpr feature_plan plan_features(bool attached, bool player_esp,
    bool chams, bool hardware_device, bool use_gpu) noexcept
{
    const bool gpu = hardware_device && use_gpu;
    return {attached && (player_esp || (gpu && chams)), gpu};
}
[[nodiscard]] constexpr std::uint32_t pose_sample_limit(bool limited,
    std::uint32_t fps) noexcept
{
    // A capped render loop already bounds RPM; uncapped rendering must not poll at 1000 Hz.
    return limited && fps > 0 && fps <= 240 ? 0u : 240u;
}
} // namespace render
