#pragma once
#include <cmath>
#include <limits>
#include <optional>

namespace simulation {
struct shot_clock_evidence {
    double estimated_ticks{}, lower_ticks{}, upper_ticks{};
    int minimum_tick{}, maximum_tick{};

    [[nodiscard]] bool excludes(int candidate) const noexcept {
        return candidate < minimum_tick || candidate > maximum_tick;
    }
};

// Native wat = selected - (current + 1). Last-shot float time is only a proxy for current.
[[nodiscard]] inline std::optional<shot_clock_evidence> infer_shot_clock(
    float seconds, float wat) noexcept
{
    if (!std::isfinite(seconds) || !std::isfinite(wat) || seconds <= 0) return {};
    constexpr float infinity = std::numeric_limits<float>::infinity();
    const auto lower = double(std::nextafter(seconds, -infinity)) * 64.0
        + 1.0 + double(std::nextafter(wat, -infinity));
    const auto upper = double(std::nextafter(seconds, infinity)) * 64.0
        + 1.0 + double(std::nextafter(wat, infinity));
    if (!(lower > 0 && upper < double(std::numeric_limits<int>::max()) && lower <= upper)) return {};
    return shot_clock_evidence{double(seconds) * 64.0 + 1.0 + wat, lower, upper,
        static_cast<int>(std::floor(lower)), static_cast<int>(std::floor(upper))};
}
}
