#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <core/math/vector.hpp>

namespace foundation {

// Returns the first nonnegative ray parameter, including zero for an inside origin.
[[nodiscard]] inline bool ray_capsule_entry(const vec3& origin, const vec3& ray,
    const vec3& start, const vec3& end, float radius, float& distance) noexcept
{
    const auto finite = [](const vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite(origin) || !finite(ray) || !finite(start) || !finite(end)
        || !std::isfinite(radius) || radius < 0.0f) return false;
    const auto dot = [](const vec3& a, const vec3& b) {
        return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
    };
    const auto ray_sqr = dot(ray, ray);
    if (ray_sqr <= 0.0) return false;
    const auto radius_sqr = double(radius) * radius;
    auto best = std::numeric_limits<double>::infinity();
    const auto sphere = [&](const vec3& center) {
        const auto offset = origin - center;
        const auto b = dot(offset, ray);
        const auto c = dot(offset, offset) - radius_sqr;
        if (c <= 0.0) { best = 0.0; return; }
        const auto discriminant = b * b - ray_sqr * c;
        if (discriminant < 0.0) return;
        const auto entry = (-b - std::sqrt(discriminant)) / ray_sqr;
        if (entry >= 0.0) best = std::min(best, entry);
    };
    const auto axis = end - start;
    const auto axis_sqr = dot(axis, axis);
    if (axis_sqr > 0.0) {
        const auto offset = origin - start;
        const auto axis_ray = dot(axis, ray);
        const auto axis_offset = dot(axis, offset);
        const auto a = axis_sqr * ray_sqr - axis_ray * axis_ray;
        const auto b = axis_sqr * dot(ray, offset) - axis_offset * axis_ray;
        const auto c = axis_sqr * (dot(offset, offset) - radius_sqr)
            - axis_offset * axis_offset;
        if (c <= 0.0 && axis_offset >= 0.0 && axis_offset <= axis_sqr) best = 0.0;
        else if (a > axis_sqr * ray_sqr * 1e-12) {
            const auto discriminant = b * b - a * c;
            if (discriminant >= 0.0) {
                const auto entry = (-b - std::sqrt(discriminant)) / a;
                const auto along = axis_offset + entry * axis_ray;
                if (entry >= 0.0 && along >= 0.0 && along <= axis_sqr)
                    best = std::min(best, entry);
            }
        }
    }
    sphere(start);
    sphere(end);
    if (!std::isfinite(best) || best > std::numeric_limits<float>::max()) return false;
    distance = static_cast<float>(best);
    return true;
}

} // namespace foundation
