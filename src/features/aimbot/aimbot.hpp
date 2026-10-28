#pragma once

#include <simulation/ballistics.hpp>
#include <simulation/seed_reaction.hpp>
#include <simulation/seed_timing.hpp>
#include <config/settings.hpp>
#include <features/aimbot/command_ring.hpp>
#include <diagnostics/rate_gate.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <config/combat.hpp>
#include <foundation/math/vector.hpp>
#include <foundation/math/random.hpp>
#include <game/state/entities.hpp>
#include <game/body/body.hpp>
#include <foundation/math/interpolation.hpp>
#include <render/draw.hpp>
#include <vector>

namespace simulation::shot_trace
{
struct ray_sample;
}

namespace features::aimbot
{
// The walk bot's autofire: while set, the trigger bot runs (with its own settings) as if enabled and held.
inline std::atomic<bool> g_walk_autofire{false};

using simulation::ballistics_t;

enum class combat_block_reason : std::uint8_t
{
    none,
    inactive,
    airborne,
    flashed,
    smoke,
    wall,
    invulnerable,
    weapon_not_ready,
    reloading,
    accuracy,
    hitchance,
    damage,
};

struct combat_frame
{
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point timestamp{};
    foundation::matrix4 camera{};
    foundation::vec3 eye{};
    foundation::vec3 view_angles{};
    std::uintptr_t local_pawn{};
    float flash_alpha{};
    ballistics_t::context weapon{};
    std::shared_ptr<const std::vector<game::player_snapshot>> targets{};
};

class aimbot_t
{
public:
    void on_render(render::draw::draw_list& draw_list);
    void tick();
    void reset();
    void seed_tick(ballistics_t& seed_shared);
    void reset_seed();
    [[nodiscard]] bool seed_hot_path_requested() const noexcept;
    [[nodiscard]] int seed_observed_tick() const noexcept { return m_seed_last_tick; }
    [[nodiscard]] std::chrono::steady_clock::time_point seed_tick_observed_at() const noexcept
    {
        return m_seed_tick_observed_at;
    }
    [[nodiscard]] bool seed_input_pending() const noexcept { return m_trigger_held; }
    // When the aimbot last held a target or the trigger (steady_clock ticks, 0 = never). Features that also turn the
    // view keep their hands off around it.
    [[nodiscard]] std::int64_t view_engaged_ticks() const noexcept
    {
        return m_view_engaged_ticks.load(std::memory_order_acquire);
    }
    // The pawn the aimbot or the trigger last held (0 = none).
    [[nodiscard]] std::uintptr_t view_engaged_target() const noexcept
    {
        return m_view_engaged_target.load(std::memory_order_acquire);
    }
    void sync_seed_phase(int tick, std::chrono::steady_clock::time_point observed_at) noexcept
    {
        if (simulation::seed_timing::observe_phase(tick, observed_at, m_seed_phase_tick, m_seed_phase_tick_at,
                                                   m_seed_phase_sampled_at, m_seed_phase_observation))
        {
            m_seed_phase_receipt.feedback_valid = false;
            m_seed_network_window.early_streak = m_seed_network_window.late_streak = 0;
        }
    }

private:
    struct target
    {
        const game::player_snapshot* player{};
        game::skeleton_reader::data bones{};
        foundation::vec3 aim_point{};

        foundation::vec3 aim_offset{};
        int hitbox{-1};
        int bone{-1};
        int hitgroup{-1};
        foundation::vec3 point{};
        float damage{};
        float fov{};
        bool penetrated{};
    };

    [[nodiscard]] target choose_target(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                       const std::vector<game::player_snapshot>& players,
                                       const config::combat_profile::resolved_config& cfg,
                                       bool enforce_fov = true) const;
    [[nodiscard]] foundation::vec3 get_aim_point(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                                 const game::player_snapshot& player,
                                                 const game::skeleton_reader::data& bones,
                                                 const config::combat_profile::resolved_config& cfg, bool enforce_fov,
                                                 float& out_damage, int& out_hitbox, int& out_bone,
                                                 bool& out_penetrated, foundation::vec3& out_offset) const;

    [[nodiscard]] float get_fov(const foundation::vec3& view_angles, const foundation::vec3& eye_pos,
                                const foundation::vec3& target_pos) const;
    [[nodiscard]] float screen_radius_for_fov(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                              float fov_degrees) const;

    void draw_penetration_crosshair(render::draw::draw_list& draw_list, const foundation::vec3& eye_pos,
                                    const foundation::vec3& view_angles,
                                    const config::combat_profile::resolved_config& cfg, float current_time);
    void draw_fov_ring(render::draw::draw_list& draw_list, const foundation::vec3& eye_pos,
                       const foundation::vec3& view_angles, const config::combat_profile::aimbot& cfg);
    void aimbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const target& tgt,
                const config::combat_profile::aimbot& cfg);
    void recoil_control(const config::combat_profile::aimbot& cfg, bool defer_input);
    void flush_recoil_input();

    // Steps of tick(), in the order it runs them.
    void update_revolver_pre_cock(const ballistics_t::context& ctx, const config::combat_profile::triggerbot& cfg);
    void release_finished_press(const ballistics_t::context& ctx, std::chrono::steady_clock::time_point now);
    void observe_shots_fired(std::uintptr_t pawn);
    [[nodiscard]] bool select_and_aim(const combat_frame& frame, const config::combat_profile::resolved_config& cfg,
                                      bool visual_scan_requested, bool visual_scan_due);
    void publish_indicator(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                           const target& picked);
    void cancel_scheduled_trigger();
    void reset_aim_tracking();

    struct trigger_result
    {
        const game::player_snapshot* player{};
        game::skeleton_reader::data bones{};
        int hitbox{-1};
        int hitgroup{-1};
        foundation::vec3 point{};
        float damage{};
        bool penetrated{};
    };

    [[nodiscard]] trigger_result trace_direction(const foundation::vec3& eye_pos, const foundation::vec3& direction,
                                                 const std::vector<game::player_snapshot>& players,
                                                 const config::combat_profile::triggerbot& cfg) const;
    void triggerbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                    const std::vector<game::player_snapshot>& players, const config::combat_profile::triggerbot& cfg);

    enum class seed_prediction_phase : std::uint8_t
    {
        current,
        ambiguous,
        next,
    };

    struct seed_shot_sample
    {
        int tick{};
        foundation::vec3 hash_angles{};
        foundation::vec3 direction_angles{};
        float fraction{};
        foundation::vec3 punch{};
    };

    struct seed_shot_plan
    {
        seed_prediction_phase phase{seed_prediction_phase::current};
        seed_shot_sample current{};
        seed_shot_sample next{};

        std::uint64_t source_key{};
        int source_tick{-1};
        foundation::vec3 source_angles{};
        foundation::vec3 prepared_punch{};
    };

    struct seed_angle_history_entry
    {
        int tick{-1};
        foundation::vec3 hash_angles{};
    };

    struct seed_target_state
    {
        std::chrono::steady_clock::time_point acquired_at{};
        std::chrono::steady_clock::time_point last_seen_at{};
    };

    [[nodiscard]] std::optional<seed_shot_plan>
    build_seed_plan(std::uintptr_t pawn, const foundation::vec3& view_angles, bool host_session, int seed_tick,
                    std::chrono::steady_clock::time_point now, bool memoize,
                    const simulation::seed_window::recoil_pair* snapshot_recoil = nullptr);
    [[nodiscard]] static bool possible_seed_match(const seed_shot_plan& plan, const std::pair<bool, bool>& matches);
    [[nodiscard]] static bool safe_seed_match(const seed_shot_plan& plan, const std::pair<bool, bool>& matches);

    // Steps of the seed trigger pipeline, in the order seed_tick runs them (aimbot_seed.cpp).

    // The local player a candidate shot is fired from: weapon state, eye and velocity at one seed tick.
    struct seed_shooter
    {
        const ballistics_t::context& weapon;
        foundation::vec3 eye{};
        foundation::vec3 velocity{};
        int tick{};
    };
    // Releases whichever input holds the seed shot (primary, secondary or the F24 proxy key) and forgets the press.
    void end_seed_press();
    // A shot the game consumed since the previous poll ends the held press and confirms the phase receipt.
    void observe_consumed_shot(std::uintptr_t pawn, int seed_tick, int shots_fired,
                               std::chrono::steady_clock::time_point now, bool tracing);
    // Matches the server's shot time with the submitted press to learn the network delivery window.
    void update_network_receipt(std::uintptr_t controller, std::chrono::steady_clock::time_point now);
    // Whether one predicted shot (every pellet of it) hits the target under the trigger's damage policy.
    [[nodiscard]] static bool seed_sample_hits(ballistics_t& ballistics, const config::combat_profile::triggerbot& cfg,
                                               const std::vector<game::player_snapshot>& candidates,
                                               const seed_shooter& shooter, std::uintptr_t target_pawn,
                                               int required_hitbox, const seed_shot_sample& sample,
                                               simulation::shot_trace::ray_sample* traced_ray);
    // seed_sample_hits for the current and next tick candidates the plan needs.
    [[nodiscard]] static std::pair<bool, bool> seed_plan_hits(ballistics_t& ballistics,
                                                              const config::combat_profile::triggerbot& cfg,
                                                              const std::vector<game::player_snapshot>& candidates,
                                                              const seed_shooter& shooter, std::uintptr_t target_pawn,
                                                              int required_hitbox, const seed_shot_plan& plan,
                                                              simulation::shot_trace::ray_sample* traced_ray);
    struct restricted_target
    {
        std::uintptr_t pawn{};
        int hitbox{-1};
    };
    // Restricted mode only fires at the hitbox the unspread ray already crosses first.
    [[nodiscard]] static std::optional<restricted_target>
    find_restricted_target(ballistics_t& ballistics, const config::combat_profile::triggerbot& cfg,
                           const std::vector<game::player_snapshot>& players, const foundation::vec3& eye,
                           const seed_shot_plan& plan, int local_team, bool free_for_all);

    foundation::damped_value m_fov_alpha{};
    foundation::scalar_transition m_pen_color_tween{};
    bool m_pen_color_can_pen{false};
    foundation::source_random m_rng{};
    bool m_rng_seeded{false};
    mutable std::mutex m_indicator_mutex{};
    foundation::vec3 m_indicator_point{};
    float m_indicator_fov{};
    std::chrono::steady_clock::time_point m_indicator_time{};
    std::chrono::steady_clock::time_point m_next_visual_scan{};
    std::uintptr_t m_indicator_pawn{};
    std::uintptr_t m_indicator_bone_cache{};
    int m_indicator_bone{-1};
    foundation::vec3 m_indicator_offset{};

    foundation::vec2 m_aim_error{};

    int m_aim_last_input_sequence{-1};
    foundation::vec3 m_aim_last_camera_angles{};
    foundation::vec3 m_aim_sent_since_sample{};
    std::chrono::steady_clock::time_point m_aim_pending_since{};
    foundation::vec3 m_aim_virtual_angles{};
    bool m_aim_virtual_angles_valid{};
    float m_aim_degrees_per_pixel{};
    float m_aim_degrees_candidate{};
    int m_aim_degrees_confirmations{};

    // Humanization state: each approach to a target gets its own parameters.
    std::uintptr_t m_aim_pawn{};
    std::shared_ptr<const std::vector<game::player_snapshot>> m_aim_selection_world{};
    target m_aim_selection_target{};
    foundation::vec3 m_aim_selection_view{};
    std::uintptr_t m_aim_selection_weapon{};
    std::chrono::steady_clock::time_point m_aim_selection_at{};
    int m_aim_last_bone{-1};
    std::chrono::steady_clock::time_point m_aim_last_call{};
    std::chrono::steady_clock::time_point m_aim_last_seen{};
    std::chrono::steady_clock::time_point m_aim_reaction_until{};
    float m_aim_ramp{};        // hand acceleration 0..1
    float m_aim_flick_speed{}; // deg/s of the current large turn
    // The current flick: a planned hand movement over the angle the target was away when it started.
    bool m_flick_active{};
    float m_flick_progress{};    // 0..1 of its duration
    float m_flick_duration{};    // s
    float m_flick_bias{};        // where the hand lands: below 0 short of the target, above past it
    float m_aim_initial_dist{};  // angular distance when the target was acquired
    float m_aim_curve{};         // arc magnitude and side
    float m_aim_overshoot{1.0f}; // overshoot multiplier
    float m_aim_wander_phase{};
    float m_aim_wander_freq{};
    float m_aim_tracking_lag{};
    // Recoil-sync state: compensation lags behind recoil and never matches it exactly.
    foundation::vec3 m_rcs_raw{};
    foundation::vec3 m_rcs_target{};
    foundation::vec3 m_rcs_velocity{}; // continuous compensation velocity
    float m_rcs_gain{1.0f};            // under/over-compensation for the current burst
    float m_rcs_response_scale{1.0f};
    float m_rcs_phase{};
    float m_rcs_freq{2.5f};
    foundation::vec3 m_rcs_applied{};
    foundation::vec2 m_rcs_mouse_error{};
    foundation::vec2 m_rcs_pending_mouse{};
    std::chrono::steady_clock::time_point m_rcs_last_call{};
    std::chrono::steady_clock::time_point m_rcs_last_input_time{};
    int m_rcs_last_input_sequence{-1};
    std::uintptr_t m_rcs_weapon{};
    int m_rcs_last_clip{-1};
    float m_rcs_last_shot_time{-1.0f};
    int m_rcs_burst_shots{};
    int m_rcs_input_step{24};
    bool m_rcs_active{false}; // a burst is in progress (recoil accumulated)
    std::uint64_t m_combat_sequence{};

    foundation::vec3 m_aim_previous_relative_velocity{};
    foundation::vec3 m_aim_relative_acceleration{};
    bool m_aim_velocity_valid{};
    int m_aim_simulation_tick{-1};
    float m_aim_simulation_time{};
    int m_aim_velocity_samples{};

    bool m_trigger_held{false};
    std::atomic<std::int64_t> m_view_engaged_ticks{};
    std::atomic<std::uintptr_t> m_view_engaged_target{};
    void note_view_engaged(const std::uintptr_t target) noexcept
    {
        if (target)
            m_view_engaged_target.store(target, std::memory_order_release);
        m_view_engaged_ticks.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                                   std::memory_order_release);
    }
    std::chrono::steady_clock::time_point m_trigger_release_time{};
    int m_trigger_press_shots{-1};
    bool m_trigger_shot_scheduled{false};
    std::chrono::steady_clock::time_point m_trigger_fire_time{};
    std::chrono::steady_clock::time_point m_trigger_cooldown_until{};
    diagnostics::rate_gate m_trigger_note{std::chrono::milliseconds(250)};
    // Each worker that reads user commands keeps its own ring search state.
    command_ring_reader m_recoil_commands{};
    command_ring_reader m_seed_commands{};

    game::player_snapshot m_trigger_target{};
    game::skeleton_reader::data m_trigger_target_bones{};
    foundation::vec3 m_trigger_target_eye{};
    foundation::vec3 m_trigger_target_angles{};
    int m_trigger_target_parts{};
    bool m_trigger_target_valid{};
    bool m_seed_press_active{false};
    bool m_seed_held_secondary{false};
    bool m_seed_held_proxy{false};
    bool m_revolver_pre_cock_down{false};
    bool m_revolver_committed{false};
    std::uintptr_t m_revolver_weapon{};
    int m_seed_last_shots{-1};
    int m_seed_pending_target_tick{-1};
    std::chrono::steady_clock::time_point m_seed_pending_time{};
    struct network_phase_receipt
    {
        std::uintptr_t weapon{};
        int selected_tick{};
        int phase_us{};
        float previous_shot_time{};
        std::chrono::steady_clock::time_point pressed_at{};
        bool shot_seen{};
        int player_tick{};
        bool feedback_valid{};
    };
    network_phase_receipt m_seed_phase_receipt{};
    simulation::seed_timing::network_phase_window m_seed_network_window{};
    simulation::seed_timing::api_call_budget m_seed_pointer_budget{}, m_seed_key_budget{};

    simulation::seed_reaction m_seed_reaction{};
    int m_seed_memo_sequence{-1};
    float m_seed_memo_pitch{};
    float m_seed_memo_yaw{};

    float m_seed_memo_next_pitch{};
    float m_seed_memo_next_yaw{};
    seed_prediction_phase m_seed_memo_phase{seed_prediction_phase::current};
    std::array<seed_angle_history_entry, 3> m_seed_angle_history{};
    std::size_t m_seed_angle_history_count{};
    int m_seed_phase_tick{-1};
    std::chrono::steady_clock::time_point m_seed_phase_tick_at{};
    std::chrono::steady_clock::time_point m_seed_phase_sampled_at{};
    simulation::seed_timing::phase_observation m_seed_phase_observation{};
    std::unordered_map<std::uintptr_t, seed_target_state> m_seed_targets{};

    std::vector<game::player_snapshot> m_seed_player_buffer{};
    std::uintptr_t m_seed_last_controller{};
    std::uintptr_t m_seed_last_pawn{};
    int m_seed_last_tick{-1};
    int m_seed_last_host_mode{-1};
    std::chrono::steady_clock::time_point m_seed_tick_observed_at{};

    mutable std::shared_ptr<const config::runtime_snapshot> m_runtime_config{};
    float m_last_time{0.0f};
    [[nodiscard]] std::shared_ptr<const config::runtime_snapshot> runtime_config() const noexcept
    {
        auto snapshot = std::atomic_load_explicit(&m_runtime_config, std::memory_order_acquire);
        return snapshot ? snapshot : config::get_runtime_snapshot();
    }
    void refresh_runtime_config() const noexcept
    {
        std::atomic_store_explicit(&m_runtime_config, config::get_runtime_snapshot(), std::memory_order_release);
    }
};

class grenade_aim_t
{
public:
    void tick();

private:
    enum class grenade_kind : std::uint8_t
    {
        unknown,
        he,
        flash,
        smoke,
        molotov,
        decoy,
    };

    struct trajectory_result
    {
        foundation::vec3 end_pos{};
        float duration{};
        float closest_head_distance{std::numeric_limits<float>::max()};
        int bounces{};
        bool valid{};
    };

    struct target_info
    {
        game::player_snapshot player{};
        std::uintptr_t pawn{};
        foundation::vec3 predicted_pos{};
        foundation::vec3 head_pos{};
        foundation::vec3 velocity{};
        foundation::vec3 optimal_angles{};
        float score{std::numeric_limits<float>::max()};
        bool found_trajectory{false};
    };

    void find_target(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                     const std::vector<game::player_snapshot>& players, float max_fov);
    void calculate_trajectory(const foundation::vec3& eye_pos, float throw_vel_vdata, float throw_strength,
                              grenade_kind kind);
    void smooth_aim(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                    const config::combat_profile::global_settings::grenade_aim_config& cfg);
    [[nodiscard]] grenade_kind resolve_grenade_kind(std::uintptr_t weapon_vdata) const;
    [[nodiscard]] trajectory_result simulate_fast(const foundation::vec3& start, const foundation::vec3& angles,
                                                  const foundation::vec3& player_vel, float throw_vel_vdata,
                                                  float throw_strength, float sv_gravity, float molotov_floor_normal,
                                                  grenade_kind kind, const foundation::vec3& target_head) const;

    bool m_is_active{false};
    target_info m_current_target{};
    foundation::vec2 m_aim_error{};
    std::chrono::steady_clock::time_point m_last_calculation{};
    grenade_kind m_last_kind{grenade_kind::unknown};
    std::uintptr_t m_last_weapon{};
    mutable std::shared_ptr<const config::runtime_snapshot> m_runtime_config{};
    [[nodiscard]] std::shared_ptr<const config::runtime_snapshot> runtime_config() const noexcept
    {
        auto snapshot = std::atomic_load_explicit(&m_runtime_config, std::memory_order_acquire);
        return snapshot ? snapshot : config::get_runtime_snapshot();
    }
    void refresh_runtime_config() const noexcept
    {
        std::atomic_store_explicit(&m_runtime_config, config::get_runtime_snapshot(), std::memory_order_release);
    }
};

inline grenade_aim_t& grenade_aim()
{
    static grenade_aim_t value{};
    return value;
}
inline aimbot_t& aim()
{
    static aimbot_t value{};
    return value;
}

} // namespace features::aimbot
