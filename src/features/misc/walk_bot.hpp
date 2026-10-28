#pragma once

#include <features/misc/input_lifecycle.hpp>
#include <features/misc/walk_bot_logic.hpp>

#include <unordered_set>
#include <diagnostics/rate_gate.hpp>
#include <foundation/math/vector.hpp>
#include <render/draw.hpp>
#include <simulation/nav_mesh.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace features::misc
{
// Walks the local player along a route over the map's navigation mesh: to random points, at the nearest enemy, or along
// a route the player recorded. The route is pulled tight and rounded into a smooth line and followed by looking ahead
// along it. It presses the movement, jump, duck, use and attack keys against the current view, and (when allowed) turns
// the view the way a player looks where they walk. The view is shared: aim, triggers, game menus, the Vesta menu and
// the player's own mouse always win; the walker comes back after a human pause (at once after a kill).
class walk_bot_t
{
public:
    void tick();                                        // movement worker
    void on_render(render::draw::draw_list& draw_list); // render thread

private:
    using owned_control = input_lifecycle::owned_control;
    using step = simulation::nav_graph::step;
    using move = simulation::nav_graph::move;
    using clock = std::chrono::steady_clock;

    enum class action : std::uint8_t
    {
        none,
        jump,
        crouch_jump,
        ladder_up,
        ladder_down,
    };

    // The smooth line the player follows, resampled at a fixed spacing. An action starts at its point; `goal` is where
    // it ends (landing, top or bottom of the ladder).
    struct path_point
    {
        foundation::vec3 position{};
        float distance{};
        action act{action::none};
        foundation::vec3 goal{};
        float goal_distance{};
        std::int32_t ladder{-1};
        std::uint32_t from{};
        std::uint32_t to{};
        bool duck{};
        bool in_action{};
    };
    using path = std::vector<path_point>;

    struct player_state
    {
        std::uintptr_t pawn{};
        std::uintptr_t controller{};
        foundation::vec3 origin{};
        foundation::vec3 velocity{};
        foundation::vec3 view{}; // pitch, yaw
        foundation::vec3 eye{};
        int team{};
        bool on_ground{};
        bool on_ladder{};
        bool in_buy_zone{};
        bool buy_menu_open{};
        bool immune{};          // respawn protection (deathmatch)
        bool moved{};           // has moved since the spawn (deathmatch closes the buy menu then)
        bool has_main_weapon{}; // carries an SMG, rifle, shotgun, sniper rifle or machine gun
        float spawn_time{};     // game time of the last spawn (changes on every spawn, also without a death)
        int money{};
    };

    // A door or a breakable lying across the route ahead.
    struct obstacle
    {
        enum class kind : std::uint8_t
        {
            none,
            door,
            breakable,
        };
        kind type{kind::none};
        std::uintptr_t entity{};
        foundation::vec3 center{};
        float distance{}; // along the route from the player
    };

    // How the view is steered this tick.
    struct gaze
    {
        enum class mode : std::uint8_t
        {
            route,       // look ahead along the way, a resting pitch
            fixed_pitch, // look towards `point` horizontally, at `pitch`
            aim,         // look straight at `point` (doors, breakables)
            check,       // look at `point` at a hand's pace: a corner or a doorway where an enemy could show up
            fight,       // the walk bot's own aim: onto `point` (an enemy's head) quickly, then exactly on it
        };
        mode how{mode::route};
        foundation::vec3 point{};
        float pitch{};
        float urgency{}; // check: 0 a far corner (slow, calm hand) .. 1 right at it (quick)
    };

    // ---- reading the game
    [[nodiscard]] bool read_player(player_state& out) const;
    // Fighting: what the view and the feet do about enemies this tick.
    struct combat_plan
    {
        enum class kind : std::uint8_t
        {
            none,
            engage, // an enemy in sight: stand, aim, let autofire shoot
            evade,  // a spawn-protected enemy close by: watch it and walk round it sideways
            react,  // hit by someone unseen: turn roughly towards where it came from, keep walking
        };
        kind what{kind::none};
        gaze view{};
        foundation::vec3 wish{}; // evade: the way to move
    };
    [[nodiscard]] combat_plan combat(const player_state& player, clock::time_point now);
    // Instead of staring along the route: now and then a look at a spot an enemy could come from.
    [[nodiscard]] gaze check_corners(const simulation::nav_graph& graph, const player_state& player, const gaze& along,
                                     clock::time_point now);
    [[nodiscard]] bool game_ui_open(const player_state& player) const;
    [[nodiscard]] static bool same_level(const player_state& player, const foundation::vec3& point);

    // ---- planning (walk_bot_path.cpp)
    [[nodiscard]] std::shared_ptr<const path>
    build_path(const simulation::nav_graph& graph, const std::vector<step>& steps, const foundation::vec3& start) const;
    [[nodiscard]] bool plan_to(const simulation::nav_graph& graph, const player_state& player, std::uint32_t goal_area,
                               const foundation::vec3& goal_point, bool require_direct);
    [[nodiscard]] bool plan_random(const simulation::nav_graph& graph, const player_state& player);
    [[nodiscard]] bool plan_hunt(const simulation::nav_graph& graph, const player_state& player, clock::time_point now);
    [[nodiscard]] bool plan_recording(const simulation::nav_graph& graph, const player_state& player);
    void install_path(std::shared_ptr<const path> route, const foundation::vec3& destination);
    void reset_route();
    [[nodiscard]] float route_length(const std::vector<step>& steps, const foundation::vec3& start) const;

    // ---- moving (walk_bot_drive.cpp)
    [[nodiscard]] gaze drive(const simulation::nav_graph& graph, const path& route, const player_state& player,
                             clock::time_point now);
    [[nodiscard]] foundation::vec3 clear_steering(const path& route, const player_state& player, float progress,
                                                  float cap) const;
    void watch_progress(const simulation::nav_graph& graph, const path& route, const player_state& player,
                        clock::time_point now, bool pressing);
    void scan_obstacles(const path& route, const player_state& player, clock::time_point now);
    void press_towards(const foundation::vec3& direction, float view_yaw);
    [[nodiscard]] bool set_control(owned_control& control, bool pressed);
    void bind_controls();
    void gate_movement(bool enabled);
    void release_keys();
    void stop_walking();
    void tap_key(std::uint16_t key);
    void vary_route();
    // Deathmatch right after a respawn, before the buy is done: the player must not move or the game takes the buy
    // menu away.
    [[nodiscard]] bool buy_holds_movement(const player_state& player) const;
    void release_tapped_key(bool now_anyway);
    void tap_attack(clock::time_point now);

    // ---- view (walk_bot_view.cpp)
    void look(const player_state& player, const gaze& target, bool blocked, clock::time_point now);

    // ---- extras (walk_bot_extras.cpp)
    void auto_buy(const player_state& player, clock::time_point now);
    void auto_reload(const player_state& player, const path* route, clock::time_point now);
    void record(const player_state& player, clock::time_point now);
    void load_recordings(const std::string& map, int team);
    void note_idle(std::uint32_t reason);

    // Route state, shared with the render thread.
    mutable std::mutex m_route_mutex{};
    std::shared_ptr<const simulation::nav_graph> m_graph{};
    std::shared_ptr<const path> m_path{};
    std::atomic<float> m_progress{};
    foundation::vec3 m_destination{};
    foundation::vec3 m_last_origin{};
    bool m_has_destination{};
    bool m_walking{};
    bool m_recording_active{};

    // Movement keys.
    owned_control m_forward{}, m_back{}, m_left{}, m_right{}, m_duck{}, m_jump{};
    std::vector<std::uint16_t> m_gated_keys{};
    clock::time_point m_player_until{}; // the player moved on their own: their keys win until then

    // Jump or ladder in progress on the action at m_action_index.
    std::size_t m_action_index{static_cast<std::size_t>(-1)};
    clock::time_point m_action_started{};
    clock::time_point m_jump_pressed{};
    int m_jump_attempts{};
    bool m_left_ground{};
    std::set<std::pair<std::uint32_t, std::uint32_t>> m_banned{}; // crossings that failed this life

    // Progress watch: off the line, or pressing keys without getting anywhere.
    clock::time_point m_off_path_since{};
    clock::time_point m_stuck_since{};
    int m_stuck_stage{};
    int m_replans_here{};
    foundation::vec3 m_stuck_at{};
    clock::time_point m_stuck_jump_until{};

    // Planning.
    std::uintptr_t m_pawn{};
    bool m_was_alive{};
    foundation::vec3 m_previous_origin{};
    bool m_previous_origin_valid{};
    foundation::vec3 m_heading{}; // smoothed walking direction
    std::deque<foundation::vec3> m_recent{};
    std::uintptr_t m_hunt_target{};
    foundation::vec3 m_hunt_planned_at{};
    clock::time_point m_next_hunt_plan{};
    clock::time_point m_next_plan{};
    diagnostics::rate_gate m_idle_note{std::chrono::seconds(3)};
    std::mt19937 m_random{std::random_device{}()};

    // Doors and breakables.
    obstacle m_obstacle{};
    std::uintptr_t m_noted_obstacle{};                                    // the last one written to the journal
    std::unordered_map<std::uintptr_t, clock::time_point> m_used_doors{}; // doors opened, when

    std::atomic<foundation::vec3> m_render_obstacle{}; // the obstacle's centre for drawing, zero when none
    clock::time_point m_next_obstacle_scan{};
    clock::time_point m_next_use{};
    clock::time_point m_next_attack{};
    clock::time_point m_attack_release{};
    bool m_attack_down{};

    // View control.
    foundation::vec3 m_view_observed{};
    bool m_view_observed_valid{};
    foundation::vec2 m_view_pending{};  // degrees sent but not yet seen in the view (x pitch, y yaw)
    foundation::vec2 m_view_velocity{}; // deg/s
    // The hand: a turn in progress (minimum-jerk, see features/aimbot/flick_profile.hpp) towards `m_turn_goal`.
    bool m_turn_active{};
    float m_turn_progress{};
    float m_turn_duration{};
    float m_turn_curve{};
    foundation::vec2 m_turn_goal{};  // pitch, yaw the turn was planned for
    foundation::vec3 m_turn_point{}; // the spot the last turn went to
    foundation::vec2 m_mouse_remainder{};
    clock::time_point m_view_resume_at{};
    clock::time_point m_view_ramp_from{};
    clock::time_point m_view_last_step{};
    std::chrono::milliseconds m_view_pause{};
    bool m_view_holding{};
    std::uintptr_t m_view_target{}; // what the aimbot held when the view was taken
    float m_noise_phase{};
    float m_rest_pitch{4.0f};

    // Fighting.
    std::int32_t m_last_health{};
    clock::time_point m_hit_at{};
    std::uintptr_t m_fight_target{};
    clock::time_point m_fight_seen_at{};
    clock::time_point m_react_until{};
    foundation::vec3 m_react_point{};
    float m_evade_side{1.0f};

    // Auto buy and auto reload.
    std::vector<std::uint16_t> m_buy_queue{};
    clock::time_point m_next_buy_key{};
    bool m_buy_deathmatch{}; // the keys being typed are a deathmatch purchase
    // The first key of the sequence was sent and the buy menu has not opened yet (m_bIsBuyMenuOpen).
    bool m_buy_waiting_menu{};
    clock::time_point m_buy_first_key{};
    // Where the feet were when they last made real headway: the stuck test by displacement. Speed alone misses
    // counter-strafing on one spot (the velocity flips back and forth and stays high).
    walk_logic::headway m_headway{};
    clock::time_point m_runup_until{}; // stepping back from a gap's edge to run at it again
    int m_runups{};
    std::unordered_set<std::uint32_t> m_unreachable{}; // destination areas the bot got stuck on the way to
    // Prefire: the corner held (where the way to the nearest hidden enemy goes round a wall), since when, and when it
    // is worked out again.
    foundation::vec3 m_prefire_point{};
    bool m_prefire_valid{};
    std::uintptr_t m_prefire_enemy{};
    clock::time_point m_prefire_since{};
    clock::time_point m_prefire_next{};
    // Ladder climbing: the highest the feet got and when they last rose (a stall under a ceiling ends the climb).
    float m_ladder_peak_z{};
    clock::time_point m_ladder_exit_since{}; // leaving the top of the ladder since
    clock::time_point m_ladder_rising_at{};
    // Per-plan variation so two walks between the same points differ: a seed for random area weights, the side of
    // wide passages the line keeps to (-1 right .. 1 left) and how wide corners are taken.
    std::uint32_t m_route_seed{};
    float m_route_lane{};
    float m_route_corner_margin{10.0f};
    // A tapped key is held for a few frames, then released here.
    std::uint16_t m_tapped_key{};
    clock::time_point m_tapped_release{};
    std::uint64_t m_life{}; // counts spawns (CS2 keeps the same pawn across lives)
    std::uint64_t m_bought_life{};
    std::uint64_t m_bought_life_round{};
    int m_bought_round{-1};
    clock::time_point m_spawned_at{};
    float m_spawn_time{};
    clock::time_point m_next_reload_check{};
    clock::time_point m_reload_cooldown{};

    // Recording.
    walk_logic::recording m_recording{};
    bool m_record_key_was_down{};
    std::string m_recordings_map{};
    int m_recordings_team{};
    std::vector<walk_logic::recording> m_recordings{};
    std::shared_ptr<const std::vector<walk_logic::recording>> m_recordings_shown{}; // for drawing, under m_route_mutex

    // Nav mesh drawing: which edges were last seen from the eye (render thread only).
    std::vector<std::uint8_t> m_edge_visible{};
    std::size_t m_edge_cursor{};
    const void* m_edge_graph{};
};

walk_bot_t& walk_bot();
} // namespace features::misc
