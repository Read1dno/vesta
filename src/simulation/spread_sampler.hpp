#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <core/math/random.hpp>
#include <core/math/vector.hpp>

namespace simulation::detail {

struct spread_parameters
{
    int seed{};
    float inaccuracy{};
    float spread{};
    float recoil_index{};
    int item{};
    int mode{};
    int bullets{1};
    bool patterns{};
    bool force_radius{};
    bool force_angle{};
};

[[nodiscard]] inline float spread_radius(float radius, const spread_parameters& p) noexcept
{
    if (p.item == 64 && p.mode == 1) return 1.0f - radius * radius;
    if (p.item == 28 && p.recoil_index < 3.0f) {
        auto count = 3;
        do { --count; radius *= radius; } while (float(count) > p.recoil_index && count > 0);
        return 1.0f - radius;
    }
    return radius;
}

enum class pattern_result { value, random, unavailable };

struct shotgun_pattern
{
    std::array<foundation::vec2, 64> angle_radius{};
    bool random{};
    [[nodiscard]] pattern_result sample(float recoil, int bullets, int pellet,
        foundation::vec2& polar) const noexcept
    {
        if (!std::isfinite(recoil) || recoil < 0.0f || bullets <= 0 || pellet < 0)
            return pattern_result::unavailable;
        const auto index = static_cast<std::int64_t>(std::min(recoil, 1000000.0f)) * bullets + pellet;
        if (random || index >= static_cast<std::int64_t>(angle_radius.size())) return pattern_result::random;
        const auto value = angle_radius[static_cast<std::size_t>(index)];
        polar = {value.y, value.x};
        return std::isfinite(polar.x) && std::isfinite(polar.y) && polar.x >= 0 && polar.x <= 1
            ? pattern_result::value : pattern_result::unavailable;
    }
};

[[nodiscard]] inline shotgun_pattern make_shotgun_pattern(int seed, int bullets)
{
    shotgun_pattern result{};
    result.random=bullets<2;
    if(result.random) return result;
    const auto count=std::min(bullets,64);
    const auto step=1.0f/static_cast<float>(count);
    foundation::source_random rng; rng.seed(seed);
    for(std::size_t i=0;i<result.angle_radius.size();++i) {
        const auto pellet=static_cast<int>(i)%count;
        const auto angle=rng.uniform(0.0f,2.0f*std::numbers::pi_v<float>);
        const auto radius=rng.uniform(float(pellet)*step,float(pellet+1)*step);
        result.angle_radius[i]={angle,std::clamp(radius,0.0f,1.0f)};
    }
    return result;
}

// Native pattern fallback consumes angle before radius.
template<class PatternReader>
[[nodiscard]] bool sample_spread(const spread_parameters& p, int pellet,
    PatternReader&& pattern, foundation::vec2& output)
{
    if (pellet < 0 || pellet >= p.bullets || p.bullets <= 0 || p.bullets > 32
        || !std::isfinite(p.inaccuracy) || p.inaccuracy < 0.0f
        || !std::isfinite(p.spread) || p.spread < 0.0f
        || !std::isfinite(p.recoil_index) || p.recoil_index < 0.0f) return false;
    constexpr auto two_pi = 2.0f * std::numbers::pi_v<float>;
    foundation::source_random rng;
    rng.seed(p.seed);
    float ir{}, ia{};
    for (int current = 0; current <= pellet; ++current) {
        if (current == 0 || p.patterns) {
            ir = spread_radius(rng.uniform(), p);
            ia = rng.uniform(0.0f, two_pi);
            if (p.force_radius) ir = 1.0f;
            ir *= p.inaccuracy;
            if (p.force_angle) ia = std::numbers::pi_v<float> * 0.5f;
        }
        foundation::vec2 polar{};
        if (p.patterns) {
            const auto lookup=p.bullets<2 ? pattern_result::random
                : pattern(p.item, p.mode, p.recoil_index, p.bullets, current, polar);
            switch (lookup) {
            case pattern_result::unavailable: return false;
            case pattern_result::random:
                polar.y = rng.uniform(0.0f, two_pi);
                polar.x = rng.uniform();
                break;
            case pattern_result::value: break;
            }
        } else {
            polar.x = rng.uniform();
            polar.y = rng.uniform(0.0f, two_pi);
        }
        if (!std::isfinite(polar.x) || !std::isfinite(polar.y)) return false;
        auto radius = spread_radius(polar.x, p);
        if (p.force_radius) radius = 1.0f;
        if (p.force_angle) polar.y = std::numbers::pi_v<float> * 0.5f;
        if (current == pellet) {
            radius *= p.spread;
            output = {std::cos(polar.y) * radius + std::cos(ia) * ir,
                std::sin(polar.y) * radius + std::sin(ia) * ir};
        }
    }
    return true;
}

} // namespace simulation::detail
