#pragma once

#include <windows.h>

#include <cstdint>
#include <array>

#include <foundation/color.hpp>
#include <config/game_controls.hpp>

namespace config
{

struct general_profile
{
    enum class screen_anchor : std::uint8_t
    {
        top_left,
        top_right,
        bottom_left,
        bottom_right
    };

    struct screen_layout
    {
        constexpr screen_layout(const float x = 40.0f, const float y = 40.0f)
            : offset_x(x)
            , offset_y(y)
            , legacy_position_x(x)
            , legacy_position_y(y)
        {
        }

        screen_anchor anchor{screen_anchor::top_left};
        float offset_x{40.0f};
        float offset_y{40.0f};
        float scale{1.0f};
        int version{1};

        // Read only while migrating configs that stored absolute position_x/y.
        float legacy_position_x{40.0f};
        float legacy_position_y{40.0f};
    };

    struct bullet_tracers
    {
        bool enabled{false};
        foundation::rgba color{192, 203, 229, 200};
        float duration{2.0f};
        float thickness{1.5f};
        bool bloom{true};
        int max_count{40};
        bool draw_cubes{true};
        bool draw_line{true};
        float cube_half{1.25f};
        foundation::rgba cube_edge_color{192, 203, 229, 230};
        float cube_face_alpha{38.0f};
        float fade_near{45.0f};
        float fade_far{160.0f};
    } m_bullet_tracers{};

    struct hitmarker
    {
        bool enabled{true};
        foundation::rgba color{245, 247, 250, 255};
        float size{8.0f};
        float gap{3.0f};
        float thickness{1.5f};
        float duration{0.55f};
    } m_hitmarker{};

    struct hitsound
    {
        bool enabled{true};
        int style{4}; // 0 soft, 1 glass, 2 pluck, 3 crisp, 4 flesh
        float volume{0.55f};
        bool show_damage{true};
        foundation::rgba damage_color{255, 108, 112, 255};
        float damage_size{13.0f};
        float damage_duration{0.75f};
        float damage_rise{30.0f};
    } m_hitsound{};

    struct grenades
    {
        bool enabled{true};
        bool local_only{false};
        foundation::rgba color{255, 255, 255, 200};
        float thickness{2.0f};
        bool bloom{true};
        foundation::rgba bloom_color{120, 170, 255, 58};
        float bloom_radius{3.0f};
        bool show_bounces{true};
        foundation::rgba bounce_color{120, 170, 255, 235};
        float bounce_size{4.0f};
        bool show_endpoint{true};
        foundation::rgba endpoint_color{255, 255, 255, 235};
        float endpoint_size{8.0f};
    } m_grenades{};

    struct nade_helper
    {
        bool enabled{false};

        float draw_distance{800.0f};
        float stand_distance{220.0f};
        float stand_radius{22.0f};
        // Broad radius keeps the marker easy to acquire; automatic execution uses
        // the tighter horizontal/vertical tolerances below.
        float release_radius{6.0f};
        float height_tolerance{8.0f};

        bool show_action{true};
        bool show_distance{true};

        foundation::rgba plaque_background{12, 13, 18, 205};
        foundation::rgba plaque_text{245, 245, 250, 255};
        foundation::rgba plaque_accent{120, 170, 255, 255};
        foundation::rgba stand_marker{120, 170, 255, 235};
        foundation::rgba stand_marker_active{110, 235, 140, 245};
        foundation::rgba aim_marker{245, 245, 250, 235};

        bool aim_assist{true};
        bool auto_release{true};
        int aim_key{VK_XBUTTON2};
        int aim_smoothing{18};
        // Angular error (degrees) at which the aim counts as converged and the
        // marker switches from "aim here" to "throw now".
        float aim_threshold{0.35f};
        int lock_time_ms{45};
    } m_nade_helper{};

    struct watermark
    {
        bool enabled{true};
        screen_layout layout{40.0f, 40.0f};
        bool vertical{};
        bool show_ping{true};
        bool show_loss{true};
        bool show_cpu{true};
        bool show_fps{true};
    } m_watermark{};

    // Movable panel (like the watermark) listing the players currently
    // spectating the local player, with their observer mode.
    struct spectator_list
    {
        bool enabled{false};
        bool show_avatars{true};
        screen_layout layout{40.0f, 320.0f};
    } m_spectator_list{};

    struct event_log
    {
        bool enabled{false};
        screen_layout layout{40.0f, 410.0f};
        float duration{5.0f};
        int max_entries{5};
        bool show_shots{true};
        bool show_hits{true};
        bool show_kills{true};
        bool show_misses{true};
        bool show_blocked{true};
        bool show_info{true};
    } m_event_log{};

    struct keybind_list
    {
        bool enabled{false};
        screen_layout layout{40.0f, 260.0f};
        bool show_always{true};
        bool show_hold{true};
        bool show_toggle{true};
    } m_keybind_list{};

    struct bomb_info
    {
        screen_layout layout{40.0f, 200.0f};
    } m_bomb_info{};

    struct bunny_hop
    {
        bool enabled{false};
        int activation_key{VK_SPACE};
    } m_bunny_hop{};

    struct edge_jump
    {
        bool enabled{false};
        int activation_key{VK_XBUTTON1};
    } m_edge_jump{};

    // Walk bot (its own Misc tab): walks the player along routes over the map's navigation mesh by pressing the
    // movement keys and turning the view like a player, plus round and match automation around it.
    struct walk_bot
    {
        // Where the routes come from. Jumps and ladders are always used; the game keys (use, reload, buy menu) are the
        // game binds in Misc > General.
        enum mode_t : int
        {
            random_points = 0,
            recorded = 1,
            mode_count
        };
        bool enabled{false};
        int mode{random_points};
        bool hunt{false}; // heads for a known enemy instead, back to the route source when there is none
        bool look{true};  // turns the view along the way, yielding to aim, triggers, the menu and the player's mouse
        bool breakables{true};       // breaks vents, boards and glass on the way with attack (doors are always opened)
        int record_key{VK_F8};       // starts and stops recording a route
        bool show_recordings{false}; // draws the recorded routes of this map and side
        // Fighting: its own aim (a player's hand: a quick smooth turn onto the head, then exact tracking), the
        // trigger bot as autofire (its settings from the Triggerbot tab), and turning towards hits from unseen enemies.
        bool aim{true};
        float aim_speed{1.0f}; // the hand's speed: 1 is a quick player, 2 a very fast one
        bool autofire{true};
        bool react_to_hits{true};
        bool show_path{true};
        foundation::rgba path_color{120, 170, 255, 220};
        float path_thickness{2.0f};
        bool show_mesh{true};
        foundation::rgba mesh_color{235, 240, 255, 46};
        float mesh_distance{1400.0f};

        bool auto_buy{false};
        // Buy presets: the default one and one per mode (used instead of the default when `custom`). Each item is a
        // buy-menu code (the digits pressed in the menu), a side and a money condition; fixed buffers keep the
        // settings trivially copyable (the overlay watches them byte by byte).
        struct buy_item
        {
            bool enabled{};
            std::array<char, 4> code{};
            int side{};       // 0 both, 1 T, 2 CT
            int money_rule{}; // 0 any amount, 1 less than `money`, 2 at least `money`
            int money{2000};
        };
        static constexpr int k_buy_items = 10;
        struct buy_preset
        {
            bool custom{};     // a mode preset is used only when set; otherwise the default applies
            bool random_one{}; // buy one of the items that apply, at random, instead of all of them
            int count{};       // items in use, the first `count` of `items`
            std::array<buy_item, k_buy_items> items{};
        };
        enum buy_mode : int
        {
            buy_default,
            buy_premier,
            buy_competitive,
            buy_wingman,
            buy_casual,
            buy_deathmatch,
            buy_mode_count
        };
        // Every preset starts empty: the user adds the items a mode needs ("Add Item").
        std::array<buy_preset, buy_mode_count> buy_presets{};
        int buy_edit{}; // the preset the menu shows
        bool auto_reload{true};

        bool auto_rejoin{false};
        int game_mode{0}; // match_logic::game_mode: deathmatch, casual, competitive, wingman, premier
        int team{0};      // 0 T, 1 CT
        bool auto_disconnect{false};
        int disconnect_score{450};
        int disconnect_minutes{4};
        int disconnect_spread{20}; // percent of random extra time
    } m_walk_bot{};

    game_controls m_game_controls{};

    struct auto_stop
    {
        bool enabled{false};
        bool physical_fire{true};
        bool predictive_trigger{true};
        float stop_speed{8.0f};
        float required_shoot_speed{34.0f};
    } m_auto_stop{};

    // Interface language: 0 English, 1 Russian (render::localization::id).
    static constexpr int k_language_count = 2;
    int language{0};
    float menu_scale{1.0f};
    struct interface_palette
    {
        foundation::rgba background{13, 13, 18, 217};
        foundation::rgba panel{20, 20, 26, 179};
        foundation::rgba card{28, 28, 36, 153};
        foundation::rgba popup{13, 13, 18, 199};
        foundation::rgba accent{124, 58, 237, 255};
        foundation::rgba text{248, 248, 242, 255};
        foundation::rgba muted_text{161, 161, 170, 255};
        foundation::rgba border{255, 255, 255, 15};
        foundation::rgba hover{255, 255, 255, 20};
    } palette{};

    bool auto_accept{true};
    bool obs_bypass{false};
    // Lua runtime is local-only. The production binary exposes no network API;
    // scripts may explicitly launch an external helper through the OS library.
    bool limit_fps{true};
    int fps_limit{240};
    bool use_gpu{true};
    // ui::resource_priority levels: 0 game first, 1 balanced, 2 overlay first. The defaults are the long-standing
    // behaviour: the same CPU class as the game, GPU work below it.
    int cpu_priority{1};
    int gpu_priority{0};
};

} // namespace config
