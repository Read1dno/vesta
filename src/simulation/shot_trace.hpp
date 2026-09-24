#pragma once
#include <cstdint>
#include <chrono>
#include <core/math/vector.hpp>

namespace simulation::shot_trace {
enum class decision_reason : unsigned {
    snapshot, policy, reaction, cooldown, pending, no_targets, no_hit,
    recheck, auto_stop, changed_state, stale_delivery, input_failed, submitted, collision, inactive, weapon, plan_unavailable, restricted, count
};
struct decision_scope {
    decision_reason reason{decision_reason::snapshot};
    bool enabled{true};
    ~decision_scope();
};
void initialize();
bool enabled() noexcept;
void expired(int observed_tick);
struct ray_sample {
    foundation::vec3 origin{}, direction{};
    std::uint32_t seed{};
    int pellet{-1};
};
void input(std::uintptr_t pawn, std::uintptr_t weapon, int tick, int next_tick,
    foundation::vec3 angles, foundation::vec3 next_angles,
    float inaccuracy, float spread, float recoil, std::uintptr_t target,
    float fraction, foundation::vec3 view, foundation::vec3 punch,
    foundation::vec3 velocity, int player_tick, int clip, int item, int next_attack, ray_sample ray = {});
void consumed(std::uintptr_t pawn, int observed_tick, int shots);
}
