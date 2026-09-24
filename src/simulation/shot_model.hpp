#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <core/math/random.hpp>
#include <core/math/vector.hpp>

namespace simulation::shot_model {
using foundation::vec3;
struct shot_time { int tick{}; float fraction{}; };

inline float length(const vec3& v) noexcept { return std::sqrt(v.length_sqr()); }
inline bool finite(const vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline float quantize(float angle) noexcept {
    if (!std::isfinite(angle)) return std::numeric_limits<float>::quiet_NaN();
    // The native fast path preserves +180, unlike unconditional wrapping.
    if (angle < -180.0f || angle > 180.0f)
        angle = std::clamp(angle - std::floor(angle * 0.0027777778f + 0.5f) * 360.0f, -180.0f, 180.0f);
    return std::floor(angle * 2.0f) * 0.5f;
}
inline std::uint32_t seed(const vec3& shot_angles, int tick) noexcept {
    struct payload { float pitch, yaw; int tick; };
    const payload data{quantize(shot_angles.x), quantize(shot_angles.y), tick};
    static_assert(sizeof(data) == 12);
    return foundation::sha1_first_word(std::as_bytes(std::span{&data,1}));
}
inline vec3 angles(const vec3& view, const vec3& punch) noexcept {
    // BacktrackLocalPlayer adds punch before both the SHA and AngleVectors.
    return view + punch;
}
inline bool same_direction(const vec3& a, const vec3& b, float tolerance = 0.002f) noexcept {
    return finite(a) && finite(b) && std::abs(a.x-b.x)<=tolerance
        && std::abs(std::remainder(a.y-b.y,360.0f))<=tolerance
        && std::abs(a.z-b.z)<=tolerance;
}
} // namespace simulation::shot_model
