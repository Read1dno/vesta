#pragma once
#include <array>
#include <cstdint>
#include <chrono>
#include <core/math/vector.hpp>

namespace simulation::shot_trace {
enum class decision_reason : unsigned {
    snapshot, policy, reaction, cooldown, pending, no_targets, no_hit,
    recheck, auto_stop, changed_state, stale_delivery, input_failed, submitted, collision, inactive, weapon, plan_unavailable, restricted, count
};
struct snapshot_record {
    std::uintptr_t pawn{}, controller{}, weapon{}, target{}, weapon_vdata{};
    int tick{-1}, simulation_tick{-1}, tick_base{-1}, ping{-1}, clip{}, item{}, next_attack{-1};
    bool host_mode{}, weapon_ready{}, reloading{};
    float inaccuracy{}, spread{}, recoil{};
    int fire_mode{}, pattern_seed{}, num_bullets{};
    std::uint32_t flags{}, ground_entity{};
    bool flags_valid{};
    bool walking{}, on_ground{};
    float current_time{}, wat{}, last_shot_time{};
    foundation::vec3 eye{}, view{}, velocity{};
    // penalty, turning, move, air, strafe, move_factor, max_speed[2], move_base[2],
    // jump_initial, jump_apex, walking, grounded, next_ratio, postpone_fraction,
    // accuracy_view_pitch, accuracy_view_yaw, speed, velocity_length.
    std::array<float, 20> accuracy_terms{};
};
struct plan_record {
    int tick{-1}, next_tick{-1}, phase{-1};
    float fraction{}, next_fraction{};
    foundation::vec3 source{}, hash{}, next_hash{}, direction{}, next_direction{}, punch{}, next_punch{};
};
struct cycle_record {
    std::uint64_t id{};
    std::int64_t start_qpc{}, snapshot_end_qpc{}, plan_qpc{}, target_qpc{},
        final_start_qpc{}, final_snapshot_qpc{}, final_plan_qpc{}, final_ray_qpc{},
        terminal_start_qpc{}, terminal_end_qpc{}, delivery_qpc{}, input_start_qpc{}, input_end_qpc{};
    snapshot_record initial{}, final{};
    plan_record initial_plan{}, final_plan{};
    int terminal_tick{-1}, delivery_tick{-1}, phase_tick{-1}, minimum_us{}, maximum_us{};
    std::int64_t phase_us{}, final_age_us{}, terminal_age_us{};
    foundation::vec3 terminal_angles{}, terminal_punch{};
    float terminal_recoil{};
    int terminal_clip{};
    int input_backend{};
    bool terminal_reloading{}, same_bucket{}, same_punch{}, input_ok{}, input_proxy{}, pre_cock{};
    // min_damage, hitbox_parts, seed_mode, reaction_ms, predictive, wall_policy, lethal_only, activation_key.
    std::array<float, 8> policy{};
};
struct capsule_record {
    int hitbox{-1}, bone{-1}, hitgroup{-1};
    foundation::vec3 start{}, end{};
    float radius{};
};
struct target_record {
    std::uint64_t cycle_id{};
    std::uintptr_t pawn{}, controller{};
    int stage{}, health{}, armor{}, simulation_tick{-1}, count{};
    float simulation_time{};
    bool helmet{};
    foundation::vec3 origin{}, velocity{};
    std::array<capsule_record, 32> capsules{};
};
#if defined(VESTA_SHOT_TRACE_ENABLED) && VESTA_SHOT_TRACE_ENABLED
std::int64_t qpc_now() noexcept;
void submit_target(const target_record& value) noexcept;
#else
constexpr std::int64_t qpc_now() noexcept { return 0; }
inline void submit_target(const target_record&) noexcept {}
#endif
struct decision_scope {
    decision_reason reason{decision_reason::snapshot};
    bool enabled{true};
    cycle_record record{};
#if defined(VESTA_SHOT_TRACE_ENABLED) && VESTA_SHOT_TRACE_ENABLED
    explicit decision_scope(decision_reason value = decision_reason::snapshot, bool active = true) noexcept;
    ~decision_scope();
#else
    explicit decision_scope(decision_reason = decision_reason::snapshot, bool = true) noexcept {}
    ~decision_scope() = default;
#endif
    decision_scope(const decision_scope&) = delete;
    decision_scope& operator=(const decision_scope&) = delete;
private:
    decision_scope* previous{};
};
#if defined(VESTA_SHOT_TRACE_ENABLED) && VESTA_SHOT_TRACE_ENABLED
void initialize();
bool enabled() noexcept;
void shutdown() noexcept;
void expired(int observed_tick);
void weapon_snapshot(int selected_tick, int simulation_tick, int tick_base, int ping,
    float inaccuracy, float spread, float recoil, foundation::vec3 view,
    foundation::vec3 velocity, std::uintptr_t weapon);
#else
inline void initialize() {}
constexpr bool enabled() noexcept { return false; }
inline void shutdown() noexcept {}
inline void expired(int) {}
inline void weapon_snapshot(int, int, int, int, float, float, float,
    foundation::vec3, foundation::vec3, std::uintptr_t) {}
#endif
struct ray_sample {
    foundation::vec3 origin{}, direction{};
    std::uint32_t seed{};
    int pellet{-1};
};
#if defined(VESTA_SHOT_TRACE_ENABLED) && VESTA_SHOT_TRACE_ENABLED
void input(std::uintptr_t pawn, std::uintptr_t weapon, int tick, int next_tick,
    foundation::vec3 angles, foundation::vec3 next_angles,
    float inaccuracy, float spread, float recoil, std::uintptr_t target,
    float fraction, foundation::vec3 view, foundation::vec3 punch,
    foundation::vec3 velocity, int player_tick, int clip, int item, int next_attack, ray_sample ray = {});
void consumed(std::uintptr_t pawn, int observed_tick, int shots);
#else
inline void input(std::uintptr_t, std::uintptr_t, int, int,
    foundation::vec3, foundation::vec3, float, float, float, std::uintptr_t,
    float, foundation::vec3, foundation::vec3, foundation::vec3,
    int, int, int, int, ray_sample = {}) {}
inline void consumed(std::uintptr_t, int, int) {}
#endif
}
