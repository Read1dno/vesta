#pragma once
#include <utility>

namespace simulation::seed_evaluation {
template<class Sample>
[[nodiscard]] bool identical_ray(const Sample& a, const Sample& b) noexcept
{
    const auto same = [](const auto& x, const auto& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    return a.tick == b.tick && same(a.hash_angles, b.hash_angles)
        && same(a.direction_angles, b.direction_angles);
}

template<class Evaluate>
[[nodiscard]] std::pair<bool, bool> required_pair(bool use_current, bool use_next,
    bool identical, Evaluate&& evaluate)
{
    const bool current = use_current && evaluate(false);
    if (use_current && !current) return {false, false};
    const bool next = use_next && ((use_current && identical) || evaluate(true));
    return {current, next};
}
}
