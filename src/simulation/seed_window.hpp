#pragma once

#include <array>
#include <simulation/recoil_model.hpp>

namespace simulation::seed_window {
using shot_model::vec3;

// Native shot ticks paired with an external click read at `tick`: network clicks landed
// on sim+1 (the selected tick) or sim, never sim+2; host clicks are unmeasured, so both
// neighbouring ticks stay plausible. A shot is safe only if every plausible tick hits.
struct tick_set {
    std::array<int, 2> ticks{};
    int count{};
};

[[nodiscard]] constexpr tick_set candidate_ticks(bool host_session, int tick) noexcept
{
    if (tick <= 0) return {};
    if (host_session) return {{tick, tick + 1}, 2};
    if (tick == 1) return {{tick, 0}, 1};
    return {{tick - 1, tick}, 2};
}

struct recoil_pair {
    shot_model::recoil_state predictable{};
    shot_model::recoil_state unpredictable{};
};

class punch_sampler {
public:
    explicit punch_sampler(const recoil_pair& state) noexcept : m_state(state) {}

    bool at(shot_model::shot_time time, vec3& out) noexcept
    {
        vec3 a{}, b{};
        if (!m_predictable.sample(m_state.predictable, time, a)
            || !m_unpredictable.sample(m_state.unpredictable, time, b)) return false;
        out = a + b;
        return shot_model::finite(out);
    }

private:
    recoil_pair m_state;
    shot_model::recoil_cache m_predictable{};
    shot_model::recoil_cache m_unpredictable{};
};

// Sub-tick fractions where the ray is traced; punch is smooth within one tick.
inline constexpr std::array<float, 3> k_ray_fractions{0.0f, 0.5f, 0.99999f};
inline constexpr int k_seed_probes = 9;

struct window {
    int tick{};
    std::array<vec3, k_ray_fractions.size()> angles{};
};

enum class status { ok, read_failed, seed_unstable };

[[nodiscard]] inline vec3 shot_angles(const vec3& base, const vec3& punch) noexcept
{
    auto angles = shot_model::angles(base, punch);
    angles.x = std::clamp(angles.x, -89.0f, 89.0f);
    return angles;
}

// The click's sub-tick fraction is unknown before it is sent, so the seed must be the
// same for every fraction in [0, 1); otherwise the tick is refused, not guessed.
[[nodiscard]] inline status resolve(punch_sampler& sampler, const vec3& base, int tick,
    window& out) noexcept
{
    out.tick = tick;
    float pitch{}, yaw{};
    for (int probe = 0; probe < k_seed_probes; ++probe) {
        const float fraction = probe + 1 == k_seed_probes
            ? k_ray_fractions.back()
            : static_cast<float>(probe) / static_cast<float>(k_seed_probes - 1);
        vec3 punch{};
        if (!sampler.at({tick, fraction}, punch)) return status::read_failed;
        const auto angles = shot_angles(base, punch);
        if (!shot_model::finite(angles)) return status::read_failed;
        const auto q_pitch = shot_model::quantize(angles.x);
        const auto q_yaw = shot_model::quantize(angles.y);
        if (probe == 0) {
            pitch = q_pitch;
            yaw = q_yaw;
        } else if (q_pitch != pitch || q_yaw != yaw) {
            return status::seed_unstable;
        }
        constexpr int step = (k_seed_probes - 1) / (static_cast<int>(k_ray_fractions.size()) - 1);
        if (probe % step == 0) out.angles[static_cast<std::size_t>(probe / step)] = angles;
    }
    return status::ok;
}
} // namespace simulation::seed_window
