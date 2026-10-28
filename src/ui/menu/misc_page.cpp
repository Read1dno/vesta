#include <stdafx.hpp>
#include <platform/windows/storage.hpp>
#include <diagnostics/health.hpp>
#include <ui/presentation.hpp>
#include <scripting/runtime.hpp>
#include <game/input/bindings.hpp>
#include <config/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <render/text/localization.hpp>
#include <diagnostics/report.hpp>
#include <platform/windows/open_folder.hpp>
#include <config/first_run.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/input.hpp>
#include <imgui.h>
#include <unordered_map>
#include <cstdio>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <ui/menu/internal.hpp>
#include <ui/menu/library_widgets.hpp>
#include <dxgi.h>
#include <wrl/client.h>
#include <ui/menu/card_metrics.hpp>
#include <ui/overlay/overlay.hpp>

namespace ui
{
using namespace ui::menu::detail;

namespace
{
enum class report_outcome
{
    none,
    saved,
    failed
};
report_outcome g_report_outcome{}; // Render thread only.

// The "System" line of a bug report: Windows build number and the first graphics adapter's marketing name. Neither
// identifies a person; both are what the issue template asks for.
std::string system_summary()
{
    std::string text;
    using rtl_get_version = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto ntdll = ::GetModuleHandleW(L"ntdll.dll"))
    {
        if (const auto get = reinterpret_cast<rtl_get_version>(::GetProcAddress(ntdll, "RtlGetVersion")))
        {
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (get(&info) == 0)
                text += std::format(" windows={}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
        }
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0, &adapter)))
    {
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)))
        {
            std::string gpu;
            for (const wchar_t c : std::wstring_view{description.Description})
                gpu += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
            text += " gpu=\"" + gpu + "\"";
        }
    }
    return text;
}

// One user-initiated action: write the report; only fixed state names go into the summary.
void export_diagnostic_report()
{
    namespace readiness = diagnostics::readiness;
    const auto geometry = diagnostics::health().geometry.snapshot();
    std::string summary{"geometry="};
    summary += readiness::name(geometry.value);
    summary += " reason=";
    summary += readiness::name(geometry.cause);
    summary += " overlay=";
    summary += readiness::name(diagnostics::health().overlay.snapshot().value);
    summary += " scripting=";
    summary += readiness::name(diagnostics::health().scripting.snapshot().value);
    summary += system_summary();
    const auto result =
        diagnostics::export_report(diagnostics::default_logs_directory(), diagnostics::desktop_directory(), summary);
    g_report_outcome = result.ok ? report_outcome::saved : report_outcome::failed;
}

void open_log_folder()
{
    (void)platform::windows::open_folder(diagnostics::default_logs_directory());
}
void draw_general_tab()
{
    auto& p = config::general_settings;
    // The game's own binds: what the features press (movement, attack, use, reload, buy menu) has to match them.
    auto& keys = p.m_game_controls;
    card_in_column("game_keyboard", "GAME KEY BINDS", 10, 0,
                   [&]
                   {
                       keybind_row("Forward", keys.forward, true, true);
                       keybind_row("Back", keys.back, true, true);
                       keybind_row("Left", keys.left, true, true);
                       keybind_row("Right", keys.right, true, true);
                       keybind_row("Walk", keys.walk, true, true);
                       keybind_row("Duck", keys.duck, true, true);
                       keybind_row("Jump", keys.jump, true, true);
                       keybind_row("Use", keys.use, true, true);
                       keybind_row("Reload Weapon", keys.reload, true, true);
                       keybind_row("Buy Menu", keys.buy_menu, true, true);
                   });
    card_in_column("game_attack", "GAME ATTACK BINDS", 3, 1,
                   [&]
                   {
                       keybind_row("Attack", keys.attack, false, true);
                       keybind_row("Secondary Attack", keys.attack2, false, true);
                       if (button_row("Reset Game Binds", "Reset", row_action_icon::none))
                           keys = {};
                   });
    card_in_column("automation", "AUTOMATION", 1, 1, [&] { toggle_row("Auto Accept Match", p.auto_accept); });
    // A short status under the engine rows: the last report, or a crash of the previous session.
    const char* status{};
    bool status_failed{};
    if (g_report_outcome != report_outcome::none)
    {
        status = g_report_outcome == report_outcome::saved ? "Report saved to Desktop" : "Report failed";
        status_failed = g_report_outcome == report_outcome::failed;
    }
    else if (diagnostics::health().previous_session_crashed.load(std::memory_order_acquire))
    {
        status = "Last session crashed";
        status_failed = true;
    }
    card_in_column("support", "SUPPORT", 3 + (status ? 1 : 0), 1,
                   [&]
                   {
                       if (button_row("Diagnostic Report", "Save", row_action_icon::save))
                           export_diagnostic_report();
                       if (button_row("Logs", "Open", row_action_icon::folder))
                           open_log_folder();
                       if (button_row("First-run Guide", "Show", row_action_icon::none))
                           config::first_run::repeat(platform::windows::runtime_storage::paths().configs);
                       if (status)
                           ImGui::TextColored(status_failed ? ImVec4{1.0f, 0.42f, 0.45f, 1.0f} : k_text_muted, "%s",
                                              render::localization::tr(status));
                   });
}

void draw_grenades_tab()
{
    auto& p = config::general_settings;
    auto& n = p.m_nade_helper;
    auto& assist = config::combat_settings.global.grenade_aim;
    card_in_column(
        "lineups", "LINEUP HELPER", 2, 0,
        [&]
        {
            toggle_popup_row("Lineup Helper", n.enabled, 4,
                             [&]
                             {
                                 settings_popup_row(
                                     "Geometry", 5,
                                     [&]
                                     {
                                         slider_row("Draw Distance", n.draw_distance, 200.0f, 4000.0f, "u", 10.0f);
                                         slider_row("Marker Distance", n.stand_distance, 60.0f, 800.0f, "u", 5.0f);
                                         slider_row("Stand Radius", n.stand_radius, 6.0f, 64.0f, "u", 1.0f);
                                         slider_row("Release Radius", n.release_radius, 1.0f, 16.0f, "u", 0.5f);
                                         slider_row("Height Tolerance", n.height_tolerance, 1.0f, 24.0f, "u", 0.5f);
                                     });
                                 settings_popup_row("Display", 2,
                                                    [&]
                                                    {
                                                        toggle_row("Show Throw Type", n.show_action);
                                                        toggle_row("Show Distance", n.show_distance);
                                                    });
                                 settings_popup_row("Plaque Style", 3,
                                                    [&]
                                                    {
                                                        color_row("Background", n.plaque_background);
                                                        color_row("Text", n.plaque_text);
                                                        color_row("Accent", n.plaque_accent);
                                                    });
                                 settings_popup_row("Marker Style", 3,
                                                    [&]
                                                    {
                                                        color_row("Stand Marker", n.stand_marker);
                                                        color_row("Stand Marker Active", n.stand_marker_active);
                                                        color_row("Aim Marker", n.aim_marker);
                                                    });
                             });
            toggle_popup_row("Lineup Aim Assist", n.aim_assist, 3,
                             [&]
                             {
                                 keybind_row("Aim Key", n.aim_key);
                                 toggle_row("Auto Release", n.auto_release);
                                 settings_popup_row("Aim Tuning", 3,
                                                    [&]
                                                    {
                                                        slider_row("Smoothing", n.aim_smoothing, 1, 30);
                                                        slider_row("Lock Threshold", n.aim_threshold, 0.05f, 3.0f,
                                                                   "deg", 0.05f);
                                                        slider_row("Settle Time", n.lock_time_ms, 0, 250, "ms");
                                                    });
                             });
        });

    card_in_column("grenade_assist", "GRENADE ASSIST", 1, 1,
                   [&]
                   {
                       toggle_popup_row("Enemy Aim Assist", assist.enabled, 2,
                                        [&]
                                        {
                                            keybind_row("Activation Key", assist.key);
                                            settings_popup_row("Aim Tuning", 2,
                                                               [&]
                                                               {
                                                                   slider_row("Target FOV", assist.fov, 1, 180);
                                                                   slider_row("Smoothing", assist.smoothing, 1, 50);
                                                               });
                                        });
                   });

    card_in_column(
        "trajectory", "TRAJECTORY", 1, 1,
        [&]
        {
            toggle_popup_row(
                "Trajectory Preview", p.m_grenades.enabled, 4,
                [&]
                {
                    toggle_row("Local Prediction Only", p.m_grenades.local_only);
                    settings_popup_row("Line Style", 3,
                                       [&]
                                       {
                                           color_row("Line Color", p.m_grenades.color);
                                           slider_row("Line Thickness", p.m_grenades.thickness, 0.5f, 8.0f, "px",
                                                      0.25f);
                                           toggle_popup_row("Bloom", p.m_grenades.bloom, 2,
                                                            [&]
                                                            {
                                                                color_row("Bloom Color", p.m_grenades.bloom_color);
                                                                slider_row("Bloom Radius", p.m_grenades.bloom_radius,
                                                                           0.5f, 12.0f, "px", 0.5f);
                                                            });
                                       });
                    toggle_popup_row("Bounce Points", p.m_grenades.show_bounces, 2,
                                     [&]
                                     {
                                         color_row("Point Color", p.m_grenades.bounce_color);
                                         slider_row("Point Size", p.m_grenades.bounce_size, 1.0f, 16.0f, "px", 0.5f);
                                     });
                    toggle_popup_row("End Point", p.m_grenades.show_endpoint, 2,
                                     [&]
                                     {
                                         color_row("Point Color", p.m_grenades.endpoint_color);
                                         slider_row("Point Size", p.m_grenades.endpoint_size, 2.0f, 24.0f, "px", 0.5f);
                                     });
                });
        });
}

void draw_movement_tab()
{
    auto& p = config::general_settings;
    card("movement", "MOVEMENT", 2,
         [&]
         {
             toggle_popup_row("Enable Bunny Hop", p.m_bunny_hop.enabled, 1,
                              [&] { keybind_row("Activation Key", p.m_bunny_hop.activation_key); });
             toggle_popup_row("Enable Edge Jump", p.m_edge_jump.enabled, 1,
                              [&] { keybind_row("Activation Key", p.m_edge_jump.activation_key); });
         });
}

void draw_walk_bot_tab()
{
    auto& walk = config::general_settings.m_walk_bot;
    card_in_column("walk_bot", "WALK BOT", 5, 0,
                   [&]
                   {
                       toggle_row("Enable Walk Bot", walk.enabled);
                       static constexpr const char* sources[]{"Random Points", "Recorded Routes"};
                       select_row("Routes", walk.mode, sources);
                       walk.mode = std::clamp(walk.mode, 0, config::general_profile::walk_bot::mode_count - 1);
                       toggle_row("Hunt Enemies", walk.hunt);
                       toggle_row("Turn Camera", walk.look);
                       toggle_row("Break Obstacles", walk.breakables);
                   });
    card_in_column("walk_combat", "COMBAT", 3, 0,
                   [&]
                   {
                       toggle_popup_row("Own Aim", walk.aim, 1,
                                        [&] { slider_row("Hand Speed", walk.aim_speed, 0.4f, 2.5f, "x", 0.1f); });
                       toggle_row("Autofire (Triggerbot Settings)", walk.autofire);
                       toggle_row("React To Hits", walk.react_to_hits);
                   });
    // Recording: press the key, walk the route yourself (jumps and crouches are kept), press it again. Routes are
    // kept per map and side and played by "Recorded Routes".
    card_in_column(
        "walk_display", "ROUTE DISPLAY", 3, 1,
        [&]
        {
            toggle_popup_row("Show Route", walk.show_path, 2,
                             [&]
                             {
                                 color_row("Route Color", walk.path_color);
                                 slider_row("Route Thickness", walk.path_thickness, 0.5f, 6.0f, "px", 0.5f);
                             });
            toggle_popup_row("Show Nav Mesh", walk.show_mesh, 2,
                             [&]
                             {
                                 color_row("Mesh Color", walk.mesh_color);
                                 slider_row("Mesh Distance", walk.mesh_distance, 200.0f, 4000.0f, "u", 50.0f);
                             });
            // Recording: press the key, walk the route yourself (jumps and crouches are kept), press it
            // again. Routes are kept per map and side and played by "Recorded Routes"; the switch draws them.
            toggle_popup_row("Recorded Routes", walk.show_recordings, 2,
                             [&]
                             {
                                 keybind_row("Record Route Key", walk.record_key, true);
                                 if (button_row("Folder", "Open", row_action_icon::folder))
                                     (void)platform::windows::open_folder(
                                         platform::windows::runtime_storage::paths().root / L"walkbot");
                             });
        });
    // Auto buy, in the switch's popup: one preset per mode (or the default one), each a list of buy-menu codes with a
    // side and a money condition ("25" for CT under $2000), items added and removed as needed.
    using walk_settings = config::general_profile::walk_bot;
    walk.buy_edit = std::clamp(walk.buy_edit, 0, static_cast<int>(walk_settings::buy_mode_count) - 1);
    auto& preset = walk.buy_presets[static_cast<std::size_t>(walk.buy_edit)];
    preset.count = std::clamp(preset.count, 0, walk_settings::k_buy_items);
    const bool preset_editable = walk.buy_edit == walk_settings::buy_default || preset.custom;
    const int buy_rows = 1 + (walk.buy_edit != walk_settings::buy_default ? 1 : 0) +
                         (preset_editable ? 1 + preset.count + (preset.count < walk_settings::k_buy_items ? 1 : 0) : 0);
    card_in_column(
        "walk_round", "ROUND", 2, 1,
        [&]
        {
            toggle_popup_row(
                "Auto Buy", walk.auto_buy, buy_rows,
                [&]
                {
                    static constexpr const char* presets[]{"Default", "Premier", "Competitive",
                                                           "Wingman", "Casual",  "Deathmatch"};
                    select_row("Preset", walk.buy_edit, presets);
                    if (walk.buy_edit != walk_settings::buy_default)
                        toggle_row("Own Preset", preset.custom);
                    if (!preset_editable)
                        return;
                    toggle_row("One Random Item", preset.random_one);
                    int remove = -1;
                    for (int i = 0; i < preset.count; ++i)
                    {
                        auto& item = preset.items[static_cast<std::size_t>(i)];
                        // The row names the item by its code and condition; the popup edits it.
                        static constexpr const char* side_names[]{"", " T", " CT"};
                        const auto label =
                            std::format("{} {}{}{}", render::localization::tr("Item"),
                                        item.code[0] ? item.code.data() : "?", side_names[std::clamp(item.side, 0, 2)],
                                        item.money_rule == 1   ? std::format(" < ${}", item.money)
                                        : item.money_rule == 2 ? std::format(" >= ${}", item.money)
                                                               : std::string{});
                        const auto row = std::format("{}###buy_item_{}", label, i);
                        settings_popup_row(row.c_str(), 6,
                                           [&]
                                           {
                                               toggle_row("Enabled", item.enabled);
                                               text_input_row("Buy Code", item.code.data(), item.code.size());
                                               static constexpr const char* sides[]{"Both Sides", "T", "CT"};
                                               select_row("Side", item.side, sides);
                                               static constexpr const char* rules[]{"Any Money", "Less Than",
                                                                                    "At Least"};
                                               select_row("Money", item.money_rule, rules);
                                               slider_row("Amount", item.money, 0, 16000, "$");
                                               if (button_row("Remove Item", "Remove", row_action_icon::none))
                                                   remove = i;
                                           });
                    }
                    if (remove >= 0)
                    {
                        for (int i = remove; i + 1 < preset.count; ++i)
                            preset.items[static_cast<std::size_t>(i)] = preset.items[static_cast<std::size_t>(i + 1)];
                        preset.items[static_cast<std::size_t>(--preset.count)] = {};
                    }
                    if (preset.count < walk_settings::k_buy_items &&
                        button_row("Add Item", "Add", row_action_icon::none))
                    {
                        preset.items[static_cast<std::size_t>(preset.count)] = {true, {}, 0, 0, 2000};
                        ++preset.count;
                    }
                });
            toggle_row("Auto Reload", walk.auto_reload);
        });
    card_in_column("walk_match", "MATCH", 2, 1,
                   [&]
                   {
                       toggle_popup_row("Auto Rejoin", walk.auto_rejoin, 2,
                                        [&]
                                        {
                                            static constexpr const char* game_modes[]{
                                                "Deathmatch", "Casual", "Competitive", "Wingman", "Premier"};
                                            select_row("Game Mode", walk.game_mode, game_modes);
                                            walk.game_mode = std::clamp(walk.game_mode, 0, 4);
                                            static constexpr const char* teams[]{"Terrorists", "Counter-Terrorists"};
                                            select_row("Team", walk.team, teams);
                                            walk.team = std::clamp(walk.team, 0, 1);
                                        });
                       toggle_popup_row("Auto Disconnect", walk.auto_disconnect, 3,
                                        [&]
                                        {
                                            slider_row("Score", walk.disconnect_score, 0, 2000);
                                            slider_row("Minutes On Server", walk.disconnect_minutes, 0, 60, " min");
                                            slider_row("Random Extra Time", walk.disconnect_spread, 0, 100, "%");
                                        });
                   });
}

void draw_overlay_tab()
{
    auto& p = config::general_settings;
    card_in_column("interface", "INTERFACE", 3, 0,
                   [&]
                   {
                       static constexpr const char* languages[]{"English", "Русский"};
                       select_row("Language", p.language, languages);
                       p.language = std::clamp(p.language, 0, config::general_profile::k_language_count - 1);
                       static constexpr std::array dpi_scales{0.50f, 0.75f, 1.00f, 1.25f, 1.50f};
                       static constexpr const char* dpi_labels[]{"50%", "75%", "100%", "125%", "150%"};
                       const auto closest_scale = std::min_element(
                           dpi_scales.begin(), dpi_scales.end(), [&](const float left, const float right)
                           { return std::abs(left - p.menu_scale) < std::abs(right - p.menu_scale); });
                       int dpi_index = static_cast<int>(std::distance(dpi_scales.begin(), closest_scale));
                       select_row("DPI Scale", dpi_index, dpi_labels);
                       p.menu_scale = dpi_scales[std::clamp(dpi_index, 0, static_cast<int>(dpi_scales.size()) - 1)];
                       settings_popup_row("Interface Colors", 3,
                                          [&]
                                          {
                                              settings_popup_row("Typography", 2,
                                                                 [&]
                                                                 {
                                                                     color_row("Primary Text", p.palette.text);
                                                                     color_row("Muted Text", p.palette.muted_text);
                                                                 });
                                              settings_popup_row("Surfaces", 4,
                                                                 [&]
                                                                 {
                                                                     color_row("Menu Background", p.palette.background);
                                                                     color_row("Controls", p.palette.panel);
                                                                     color_row("Containers", p.palette.card);
                                                                     color_row("Popups", p.palette.popup);
                                                                 });
                                              settings_popup_row("Interaction", 3,
                                                                 [&]
                                                                 {
                                                                     color_row("Accent", p.palette.accent);
                                                                     color_row("Hover", p.palette.hover);
                                                                     color_row("Borders", p.palette.border);
                                                                 });
                                          });
                   });
    card_in_column("engine", "ENGINE", 4, 1,
                   [&]
                   {
                       toggle_popup_row("FPS Limiter", p.limit_fps, 1,
                                        [&] { slider_row("Maximum FPS", p.fps_limit, 30, 1000); });
                       toggle_row("GPU Effects", p.use_gpu);
                       static constexpr const char* priorities[]{"Game First", "Balanced", "Overlay First"};
                       select_row("CPU Priority", p.cpu_priority, priorities);
                       select_row("GPU Priority", p.gpu_priority, priorities);
                   });

    card_in_column("overlay", "OVERLAY", 6, 0,
                   [&]
                   {
                       toggle_row("OBS Bypass", p.obs_bypass);
                       toggle_popup_row("Show Watermark", p.m_watermark.enabled, 4,
                                        [&]
                                        {
                                            toggle_row("Show Ping", p.m_watermark.show_ping);
                                            toggle_row("Show Loss", p.m_watermark.show_loss);
                                            toggle_row("Show CPU Load", p.m_watermark.show_cpu);
                                            toggle_row("Show FPS", p.m_watermark.show_fps);
                                        });
                       toggle_row("Show Spectators", p.m_spectator_list.enabled);
                       toggle_popup_row("Show Event Log", p.m_event_log.enabled, 3,
                                        [&]
                                        {
                                            slider_row("Duration", p.m_event_log.duration, 0.5f, 20.0f, " s", 0.5f);
                                            slider_row("Maximum Entries", p.m_event_log.max_entries, 1, 5);
                                            int event_mask = (p.m_event_log.show_shots ? 1 : 0) |
                                                             (p.m_event_log.show_hits ? 2 : 0) |
                                                             (p.m_event_log.show_kills ? 4 : 0) |
                                                             (p.m_event_log.show_misses ? 8 : 0) |
                                                             (p.m_event_log.show_blocked ? 16 : 0) |
                                                             (p.m_event_log.show_info ? 32 : 0);
                                            static constexpr std::pair<const char*, int> event_options[]{
                                                {"Shots", 1},  {"Hits", 2},     {"Kills", 4},
                                                {"Misses", 8}, {"Blocked", 16}, {"Info", 32}};
                                            multiselect_row("Events", event_mask, event_options, 63);
                                            p.m_event_log.show_shots = (event_mask & 1) != 0;
                                            p.m_event_log.show_hits = (event_mask & 2) != 0;
                                            p.m_event_log.show_kills = (event_mask & 4) != 0;
                                            p.m_event_log.show_misses = (event_mask & 8) != 0;
                                            p.m_event_log.show_blocked = (event_mask & 16) != 0;
                                            p.m_event_log.show_info = (event_mask & 32) != 0;
                                        });
                       toggle_popup_row("Show Active Binds", p.m_keybind_list.enabled, 3,
                                        [&]
                                        {
                                            toggle_row("Always On Binds", p.m_keybind_list.show_always);
                                            toggle_row("Hold Binds", p.m_keybind_list.show_hold);
                                            toggle_row("Toggle Binds", p.m_keybind_list.show_toggle);
                                        });
                       toggle_row("Show Bomb Info", config::visual_settings.m_bomb.show_info_panel);
                   });
}

void draw_lua_tab()
{
    draw_script_library();
}

void draw_configs_tab()
{
    draw_config_library();
}
} // namespace

void menu_t::draw_misc()
{
    static constexpr void (*tab_pages[])(){draw_general_tab,  draw_overlay_tab, draw_movement_tab, draw_walk_bot_tab,
                                           draw_grenades_tab, draw_lua_tab,     draw_configs_tab};
    static constexpr const char* tabs[]{"General", "Overlay", "Movement", "Walk Bot", "Grenades", "Lua API", "Configs"};
    static_assert(std::size(tabs) == std::size(tab_pages));
    ImGui::SetCursorPos({24.0f, 24.0f});
    for (int i = 0; i < static_cast<int>(std::size(tabs)); ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 10.0f);
        if (tab_button(tabs[i], m_misc_group == i) && m_misc_group != i)
        {
            m_misc_group = i;
            reset_content_animation();
        }
    }

    ImGui::SetCursorPos({14.0f, 68.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##misc_cards", {682.0f, 568.0f}, false);
    begin_cards("##misc_grid");
    tab_pages[std::clamp(m_misc_group, 0, static_cast<int>(std::size(tab_pages)) - 1)]();
    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui
