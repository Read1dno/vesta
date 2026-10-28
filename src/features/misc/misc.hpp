#pragma once

#include <game/input/bindings.hpp>
#include <game/memory/memory_services.hpp>
#include <features/misc/input_lifecycle.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <foundation/math/vector.hpp>
#include <render/draw.hpp>
#include <vector>

namespace features::misc
{

class nade_helper_t
{
public:
    void on_render(render::draw::draw_list& draw_list);
    void tick();
    // True while a lineup throw drives the movement keys; other movement features stand back.
    [[nodiscard]] bool movement_taken() const noexcept { return m_movement_taken.load(std::memory_order_acquire); }

private:
    enum class throw_phase : std::uint8_t
    {
        idle,
        crouching,
        priming,
        running,
        jumping,
        complete,
    };

    using owned_control = input_lifecycle::owned_control;

    // Detached copy of one table row. Strings point into the generated
    // constexpr table, so they outlive any use of this struct.
    struct lineup_view
    {
        const char* name{};
        const char* action{};
        foundation::vec3 position{};
        float pitch{};
        float yaw{};
        std::uint16_t actions{};
        std::uint16_t run_ticks{};
        std::uint8_t after_jump_ticks{};
        float throw_strength{};
        bool manual{};
        float distance{};
    };

    // Pure with respect to feature state: both the render and combat threads
    // call it so their idea of "nearest lineup" cannot drift apart.
    [[nodiscard]] bool collect(const foundation::vec3& player_pos, std::vector<lineup_view>& out) const;
    [[nodiscard]] static std::uint8_t resolve_kind(std::uintptr_t weapon_vdata);
    // Index of the lineup nearest the crosshair among those armed at the
    // player's current stand point, or -1 when none are.
    [[nodiscard]] int select_armed(const std::vector<lineup_view>& lineups, const foundation::vec3& view_angles) const;
    [[nodiscard]] bool execution_position_ready(const lineup_view& lineup, const foundation::vec3& player_pos) const;
    void reset_lock();

    void aim_at(const lineup_view& lineup, const foundation::vec3& view_angles, float& out_error);
    [[nodiscard]] bool begin_throw(const lineup_view& lineup, std::uintptr_t pawn, std::uintptr_t weapon,
                                   std::uint32_t tick, std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool prime_throw(std::uint32_t tick, std::chrono::steady_clock::time_point now);
    void drive_throw(std::uintptr_t pawn, std::uintptr_t weapon, std::uint32_t tick,
                     std::chrono::steady_clock::time_point now);
    void finish_throw(std::uint32_t tick);
    [[nodiscard]] bool set_control(owned_control& control, bool pressed);
    void release_movement(bool include_jump = true);
    void release_attacks();
    void cancel_throw(bool latch);
    // False during freeze time, the team intro, a paused match or with the buy menu open.
    [[nodiscard]] bool gameplay_allows_throw(std::uintptr_t pawn);

    // Human-readable "hold X, do Y, release" for the marker label, built
    // from the parsed movement flags rather than the raw action text.
    [[nodiscard]] static std::string throw_instruction(const lineup_view& lineup);

    // Shared plaque primitive: dark rounded panel, white title, dimmed
    // subtitle, accent edge. Anchored by the top-centre of the box.
    void draw_text_plaque(render::draw::draw_list& draw_list, float center_x, float top_y, std::string_view title,
                          std::string_view subtitle, const render::draw::rgba& accent, float alpha) const;

    void draw_plaque(render::draw::draw_list& draw_list, const lineup_view& lineup,
                     const foundation::vec2& screen) const;
    void draw_stand_marker(render::draw::draw_list& draw_list, const lineup_view& lineup, bool standing) const;
    void draw_aim_guidance(render::draw::draw_list& draw_list, const lineup_view& lineup,
                           const foundation::vec3& eye_pos, bool selected, bool converged) const;

    foundation::vec2 m_aim_error{};
    foundation::vec2 m_aim_velocity{};
    std::vector<std::uint16_t> m_gated_keys{};
    std::vector<lineup_view> m_render_scratch{};
    std::vector<lineup_view> m_tick_scratch{};
    lineup_view m_active_lineup{};
    throw_phase m_throw_phase{throw_phase::idle};
    owned_control m_forward{};
    owned_control m_walk{};
    owned_control m_duck{};
    owned_control m_jump{};
    owned_control m_attack{};
    owned_control m_attack2{};
    std::uintptr_t m_active_pawn{};
    std::uintptr_t m_active_weapon{};
    std::uint32_t m_phase_tick{};
    std::uint32_t m_run_start_tick{};
    std::uint32_t m_jump_tick{};
    std::chrono::steady_clock::time_point m_phase_started{};
    std::chrono::steady_clock::time_point m_lock_started{};
    std::chrono::steady_clock::time_point m_last_aim_update{};
    const char* m_lock_name{};
    foundation::vec3 m_lock_position{};
    float m_lock_pitch{};
    float m_lock_yaw{};
    bool m_activation_latched{};
    bool m_release_pending{};
    std::atomic<bool> m_movement_taken{};
    std::uintptr_t m_game_rules{};
    std::chrono::steady_clock::time_point m_next_rules_scan{};

    // Distance the aim marker is projected out from the eye. Only affects
    // where the marker is drawn, never the angles that are aimed at.
    static constexpr float k_aim_marker_distance{220.0f};
};

class auto_accept_t
{
public:
    auto_accept_t();
    ~auto_accept_t();
    auto_accept_t(const auto_accept_t&) = delete;
    auto_accept_t& operator=(const auto_accept_t&) = delete;

    void tick();

private:
    enum class click_phase : std::uint8_t
    {
        idle,
        moved,
        pressed,
    };

public:
    // What one pass over the Panorama registry saw: panels read, panels with the CUIPanel vtable, button-class
    // panels among them, panels whose id is the accept button, and accept buttons inside the accept popup. Journaled
    // when it changes; it tells a broken registry walk from a broken button test.
    struct scan_stats
    {
        std::uint32_t panels{}, ui_panels{}, buttons{}, accept_ids{}, accept_layout{};
        [[nodiscard]] bool operator==(const scan_stats&) const = default;
    };

    // Incremental walk over the Panorama panel registry, a chunk per tick. A slot is keyed by (panel, generation):
    // a panel is registered before its id and parent are filled in, and a freed panel can return at the same address.
    struct registry_scan
    {
        std::uint32_t cursor{};
        std::vector<std::uintptr_t> panels{};
        std::vector<std::uint32_t> generations{};
        // Panels that appeared after the first full pass and were not recognised yet: looked at again for a few
        // seconds. Panels present at the first pass are the baseline and are not tracked.
        std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> young{};
        bool baseline_done{};
        std::uintptr_t best_panel{};
        std::uint32_t best_generation{};
        scan_stats stats{};
        scan_stats reported{};
    };

private:
    // Panorama engine, panel registry and class table, resolved once per game session.
    struct panorama;
    std::unique_ptr<panorama> m_panorama;
    std::uintptr_t m_signal_identity{};
    std::uintptr_t m_candidate_panel{};
    std::uintptr_t m_clicked_panel{};
    registry_scan m_scan{};
    std::chrono::steady_clock::time_point m_last_blocked_report{};
    click_phase m_click_phase{click_phase::idle};
    std::uintptr_t m_click_window{};
    std::uintptr_t m_click_panel{};
    std::int32_t m_saved_cursor_x{};
    std::int32_t m_saved_cursor_y{};
    std::int32_t m_click_x{};
    std::int32_t m_click_y{};
    bool m_saved_cursor_valid{};
    bool m_clicked_panel_hidden{};
};

class bhop_t
{
public:
    void tick();

private:
    [[nodiscard]] bool will_leave_ground(std::uintptr_t pawn, const foundation::vec3& origin,
                                         const foundation::vec3& velocity, bool require_current_support) const;

    int m_reported_jump_key{-1};
    std::uint16_t m_jump_key{};
    bool m_jump_down{false};
    std::uint16_t m_owned_jump_key{};
    bool m_gate_requested{};
    bool m_edge_was_on_ground{false};
    bool m_edge_armed{false};
    float m_last_bunny_simulation_time{-1.0f};
    std::uintptr_t m_bunny_pawn{};
    std::chrono::steady_clock::time_point m_last_bunny_tap{};
    // Every transition is held long enough to cross a game input sample. If a
    // held edge-jump press survives long enough for the game to sample it.
    std::chrono::steady_clock::time_point m_last_transition{};
};

inline nade_helper_t& nade_helper()
{
    static nade_helper_t value{};
    return value;
}
inline auto_accept_t& auto_accept()
{
    static auto_accept_t value{};
    return value;
}
inline bhop_t& bhop()
{
    static bhop_t value{};
    return value;
}

// Writes the live Auto Accept resolver report for a diagnostic run; 0 when the Panorama registry was readable.
int auto_accept_report(std::ostream& out, const game::attach_status& attached);

} // namespace features::misc
