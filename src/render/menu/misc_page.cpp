#include <stdafx.hpp>
#include <scripting/runtime.hpp>
#include <app/context.hpp>
#include <core/input/bindings.hpp>
#include <core/input/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/menu/localization.hpp>
#include <render/menu/menu.hpp>
#include <render/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <render/menu/internal.hpp>

using namespace render::menu::detail;

void menu_t::draw_misc()
{
    static constexpr const char *tabs[]{"General", "Grenades", "Movement", "Overlay", "Lua API", "Configs"};
    ImGui::SetCursorPos({24.0f, 24.0f});
    for (int i = 0; i < 6; ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 10.0f);
        if (tab_button(tabs[i], this->m_misc_group == i) && this->m_misc_group != i)
        {
            this->m_misc_group = i;
            this->reset_content_animation();
        }
    }

    ImGui::SetCursorPos({14.0f, 68.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##misc_cards", {682.0f, 568.0f}, false);
    begin_cards("##misc_grid");
    auto &p = config::general_settings;

    if (this->m_misc_group == 0)
    {
        card_in_column("interface", "INTERFACE", 3, 0, [&] {
            static constexpr const char *languages[]{"English", "Русский"};
            const int previous = p.language;
            select_row("Language", p.language, languages);
            if (p.language != previous)
            {
                p.language = std::clamp(p.language, 0, static_cast<int>(render::localization::id::count) - 1);
                render::localization::set(static_cast<render::localization::id>(p.language));
            }
            static constexpr std::array dpi_scales{0.50f, 0.75f, 1.00f, 1.25f, 1.50f};
            static constexpr const char *dpi_labels[]{"50%", "75%", "100%", "125%", "150%"};
            const auto closest_scale = std::min_element(
                dpi_scales.begin(), dpi_scales.end(), [&](const float left, const float right) {
                    return std::abs(left - p.menu_scale) < std::abs(right - p.menu_scale);
                });
            int dpi_index = static_cast<int>(std::distance(dpi_scales.begin(), closest_scale));
            select_row("DPI Scale", dpi_index, dpi_labels);
            p.menu_scale = dpi_scales[std::clamp(dpi_index, 0, static_cast<int>(dpi_scales.size()) - 1)];
            settings_popup_row("Interface Colors", 3, [&] {
                settings_popup_row("Typography", 2, [&] {
                    color_row("Primary Text", p.palette.text);
                    color_row("Muted Text", p.palette.muted_text);
                });
                settings_popup_row("Surfaces", 4, [&] {
                    color_row("Menu Background", p.palette.background);
                    color_row("Controls", p.palette.panel);
                    color_row("Containers", p.palette.card);
                    color_row("Popups", p.palette.popup);
                });
                settings_popup_row("Interaction", 3, [&] {
                    color_row("Accent", p.palette.accent);
                    color_row("Hover", p.palette.hover);
                    color_row("Borders", p.palette.border);
                });
            });
        });
        card_in_column("automation", "AUTOMATION", 1, 1,
                       [&] { toggle_row("Auto Accept Match", p.auto_accept); });
        card_in_column("engine", "ENGINE", 1, 1, [&] {
            toggle_popup_row("FPS Limiter", p.limit_fps, 1,
                             [&] { slider_row("Maximum FPS", p.fps_limit, 30, 1000); });
        });
    }
    else if (this->m_misc_group == 1)
    {
        auto &n = p.m_nade_helper;
        auto &assist = config::combat_settings.global.grenade_aim;
        card_in_column("lineups", "LINEUP HELPER", 2, 0, [&] {
            toggle_popup_row("Lineup Helper", n.enabled, 4, [&] {
                settings_popup_row("Geometry", 5, [&] {
                    slider_row("Draw Distance", n.draw_distance, 200.0f, 4000.0f, "u", 10.0f);
                    slider_row("Marker Distance", n.stand_distance, 60.0f, 800.0f, "u", 5.0f);
                    slider_row("Stand Radius", n.stand_radius, 6.0f, 64.0f, "u", 1.0f);
                    slider_row("Release Radius", n.release_radius, 1.0f, 16.0f, "u", 0.5f);
                    slider_row("Height Tolerance", n.height_tolerance, 1.0f, 24.0f, "u", 0.5f);
                });
                settings_popup_row("Display", 2, [&] {
                    toggle_row("Show Throw Type", n.show_action);
                    toggle_row("Show Distance", n.show_distance);
                });
                settings_popup_row("Plaque Style", 3, [&] {
                    color_row("Background", n.plaque_background);
                    color_row("Text", n.plaque_text);
                    color_row("Accent", n.plaque_accent);
                });
                settings_popup_row("Marker Style", 3, [&] {
                    color_row("Stand Marker", n.stand_marker);
                    color_row("Stand Marker Active", n.stand_marker_active);
                    color_row("Aim Marker", n.aim_marker);
                });
            });
            toggle_popup_row("Lineup Aim Assist", n.aim_assist, 3, [&] {
                keybind_row("Aim Key", n.aim_key);
                toggle_row("Auto Release", n.auto_release);
                settings_popup_row("Aim Tuning", 3, [&] {
                    slider_row("Smoothing", n.aim_smoothing, 1, 30);
                    slider_row("Lock Threshold", n.aim_threshold, 0.05f, 3.0f, "deg", 0.05f);
                    slider_row("Settle Time", n.lock_time_ms, 0, 250, "ms");
                });
            });
        });

        card_in_column("grenade_assist", "GRENADE ASSIST", 1, 1, [&] {
            toggle_popup_row("Enemy Aim Assist", assist.enabled, 2, [&] {
                keybind_row("Activation Key", assist.key);
                settings_popup_row("Aim Tuning", 2, [&] {
                    slider_row("Target FOV", assist.fov, 1, 180);
                    slider_row("Smoothing", assist.smoothing, 1, 50);
                });
            });
        });

        card_in_column("trajectory", "TRAJECTORY", 1, 1, [&] {
            toggle_popup_row("Trajectory Preview", p.m_grenades.enabled, 4, [&] {
                toggle_row("Local Prediction Only", p.m_grenades.local_only);
                settings_popup_row("Line Style", 3, [&] {
                    color_row("Line Color", p.m_grenades.color);
                    slider_row("Line Thickness", p.m_grenades.thickness, 0.5f, 8.0f, "px", 0.25f);
                    toggle_popup_row("Bloom", p.m_grenades.bloom, 2, [&] {
                        color_row("Bloom Color", p.m_grenades.bloom_color);
                        slider_row("Bloom Radius", p.m_grenades.bloom_radius, 0.5f, 12.0f, "px", 0.5f);
                    });
                });
                toggle_popup_row("Bounce Points", p.m_grenades.show_bounces, 2, [&] {
                    color_row("Point Color", p.m_grenades.bounce_color);
                    slider_row("Point Size", p.m_grenades.bounce_size, 1.0f, 16.0f, "px", 0.5f);
                });
                toggle_popup_row("End Point", p.m_grenades.show_endpoint, 2, [&] {
                    color_row("Point Color", p.m_grenades.endpoint_color);
                    slider_row("Point Size", p.m_grenades.endpoint_size, 2.0f, 24.0f, "px", 0.5f);
                });
            });
        });
    }
    else if (this->m_misc_group == 2)
    {
        card("movement", "MOVEMENT", 3, [&] {
            toggle_popup_row("Enable Bunny Hop", p.m_bunny_hop.enabled, 1,
                             [&] { keybind_row("Activation Key", p.m_bunny_hop.activation_key); });
            toggle_popup_row("Enable Edge Jump", p.m_edge_jump.enabled, 1,
                             [&] { keybind_row("Activation Key", p.m_edge_jump.activation_key); });
            toggle_popup_row("Enable Auto Stop", p.m_auto_stop.enabled, 2, [&] {
                slider_row("Stop Speed", p.m_auto_stop.stop_speed, 0.0f, 150.0f, " u/s", 1.0f);
                slider_row("Shoot Speed", p.m_auto_stop.required_shoot_speed, 0.0f, 60.0f, "%", 1.0f);
            });
        });
    }
    else if (this->m_misc_group == 3)
    {
        card("overlay", "OVERLAY", 6, [&] {
            toggle_row("OBS Bypass", p.obs_bypass);
            toggle_popup_row("Show Watermark", p.m_watermark.enabled, 4, [&] {
                toggle_row("Show Ping", p.m_watermark.show_ping);
                toggle_row("Show Loss", p.m_watermark.show_loss);
                toggle_row("Show CPU Load", p.m_watermark.show_cpu);
                toggle_row("Show FPS", p.m_watermark.show_fps);
            });
            toggle_row("Show Spectators", p.m_spectator_list.enabled);
            toggle_popup_row("Show Event Log", p.m_event_log.enabled, 3, [&] {
                slider_row("Duration", p.m_event_log.duration, 0.5f, 20.0f, " s", 0.5f);
                slider_row("Maximum Entries", p.m_event_log.max_entries, 1, 5);
                int event_mask = (p.m_event_log.show_shots ? 1 : 0) | (p.m_event_log.show_hits ? 2 : 0) |
                                 (p.m_event_log.show_kills ? 4 : 0) | (p.m_event_log.show_misses ? 8 : 0) |
                                 (p.m_event_log.show_blocked ? 16 : 0) | (p.m_event_log.show_info ? 32 : 0);
                static constexpr std::pair<const char *, int> event_options[]{
                    {"Shots", 1}, {"Hits", 2}, {"Kills", 4}, {"Misses", 8}, {"Blocked", 16}, {"Info", 32}};
                multiselect_row("Events", event_mask, event_options, 63);
                p.m_event_log.show_shots = (event_mask & 1) != 0;
                p.m_event_log.show_hits = (event_mask & 2) != 0;
                p.m_event_log.show_kills = (event_mask & 4) != 0;
                p.m_event_log.show_misses = (event_mask & 8) != 0;
                p.m_event_log.show_blocked = (event_mask & 16) != 0;
                p.m_event_log.show_info = (event_mask & 32) != 0;
            });
            toggle_popup_row("Show Active Binds", p.m_keybind_list.enabled, 3, [&] {
                toggle_row("Always On Binds", p.m_keybind_list.show_always);
                toggle_row("Hold Binds", p.m_keybind_list.show_hold);
                toggle_row("Toggle Binds", p.m_keybind_list.show_toggle);
            });
            toggle_row("Show Bomb Info", config::visual_settings.m_bomb.show_info_panel);
        });
    }
    else if (this->m_misc_group == 4)
    {
        const auto scripts = scripting::runtime().scripts();
        card_in_column("lua_runtime", "LUA RUNTIME", 3, 0, [&] {
            const auto previous = p.lua_enabled;
            toggle_row("Enable Lua API", p.lua_enabled);
            if (p.lua_enabled != previous && p.lua_enabled)
                for (const auto &script : scripts)
                    if (script.autoload)
                        scripting::runtime().set_enabled(script.id, true);
            if (button_row("Script Directory", "Open Folder", row_action_icon::folder))
            {
                const auto path = scripting::runtime().scripts_path().u8string();
                auto &platform = ImGui::GetPlatformIO();
                if (platform.Platform_OpenInShellFn)
                    platform.Platform_OpenInShellFn(ImGui::GetCurrentContext(),
                                                    reinterpret_cast<const char *>(path.c_str()));
            }
            if (button_row("Load Lua Script", "Browse...", row_action_icon::folder))
                app::context().overlay.request_lua_import();
        });

        int script_rows = std::max(1, static_cast<int>(scripts.size()));
        for (const auto &script : scripts)
            if (_stricmp(script.id.c_str(), "vesta_web_radar") == 0 && script.enabled)
                ++script_rows;
        card_in_column("lua_scripts", "SCRIPTS", script_rows, 1, [&] {
            if (scripts.empty())
            {
                begin_row("No Lua scripts found", 148.0f);
                clipped_row_text(render::localization::tr("Copy scripts into the Lua directory"));
                end_row();
            }
            for (const auto &script : scripts)
            {
                ImGui::PushID(script.id.c_str());
                auto running = script.enabled;
                const auto controls = scripting::runtime().controls(script.id);
                if (_stricmp(script.id.c_str(), "vesta_web_radar") == 0)
                {
                    toggle_row(script.name.c_str(), running);
                    if (running)
                    {
                        for (const auto &control : controls)
                        {
                            if (control.id != "copy" || control.kind != scripting::control_kind::button)
                                continue;
                            if (button_row("Copy link", control.action_text.empty()
                                                            ? "Copy"
                                                            : control.action_text.c_str()))
                                scripting::runtime().press_control(script.id, control.id);
                        }
                    }
                    if (running != script.enabled)
                        scripting::runtime().set_enabled(script.id, running);
                    ImGui::PopID();
                    continue;
                }
                const auto popup_rows = std::clamp(5 + static_cast<int>(controls.size()), 5, 16);
                toggle_popup_row(script.name.c_str(), running, popup_rows, [&] {
                    auto autoload = script.autoload;
                    toggle_row("Autoload", autoload);
                    if (autoload != script.autoload)
                        scripting::runtime().set_autoload(script.id, autoload);
                    auto hot_reload = script.hot_reload;
                    toggle_row("Hot Reload", hot_reload);
                    if (hot_reload != script.hot_reload)
                        scripting::runtime().set_hot_reload(script.id, hot_reload);

                    begin_row("Status", 116.0f);
                    ImGui::TextColored(script.state == scripting::script_state::running
                                           ? ImVec4{0.38f, 0.86f, 0.55f, 1.0f}
                                       : (script.state == scripting::script_state::error ||
                                          script.state == scripting::script_state::over_budget)
                                           ? ImVec4{1.0f, 0.38f, 0.42f, 1.0f}
                                           : k_text_muted,
                                       "%s", scripting::runtime_t::state_name(script.state));
                    end_row();
                    begin_row("Runtime", 116.0f);
                    ImGui::TextColored(k_text_muted, "%.2f ms / %.1f MiB", script.last_callback_ms,
                                       static_cast<double>(script.memory_bytes) / (1024.0 * 1024.0));
                    end_row();
                    if (!script.error.empty())
                    {
                        begin_row("Last Error", 116.0f);
                        clipped_row_text(script.error, ImVec4{1.0f, 0.38f, 0.42f, 1.0f});
                        end_row();
                    }

                    for (const auto &control : controls)
                    {
                        ImGui::PushID(control.id.c_str());
                        switch (control.kind)
                        {
                        case scripting::control_kind::text:
                            begin_row(control.label.c_str(), 116.0f);
                            if (const auto *value = std::get_if<std::string>(&control.value))
                                clipped_row_text(*value);
                            end_row();
                            break;
                        case scripting::control_kind::button:
                            if (button_row(control.label.c_str(),
                                           control.action_text.empty() ? "Run" : control.action_text.c_str()))
                                scripting::runtime().press_control(script.id, control.id);
                            break;
                        case scripting::control_kind::toggle: {
                            auto value =
                                std::get_if<bool>(&control.value) ? std::get<bool>(control.value) : false;
                            const auto before = value;
                            toggle_row(control.label.c_str(), value);
                            if (value != before)
                                scripting::runtime().set_control(script.id, control.id, value);
                            break;
                        }
                        case scripting::control_kind::slider: {
                            auto value = static_cast<float>(
                                std::get_if<double>(&control.value) ? std::get<double>(control.value) : 0.0);
                            const auto before = value;
                            slider_row(control.label.c_str(), value, static_cast<float>(control.minimum),
                                       static_cast<float>(control.maximum), "",
                                       static_cast<float>(control.step));
                            if (value != before)
                                scripting::runtime().set_control(script.id, control.id,
                                                                 static_cast<double>(value));
                            break;
                        }
                        case scripting::control_kind::select: {
                            auto value = std::get_if<int>(&control.value) ? std::get<int>(control.value) : 0;
                            std::vector<const char *> labels{};
                            labels.reserve(control.options.size());
                            for (const auto &option : control.options)
                                labels.push_back(option.c_str());
                            const auto before = value;
                            select_row(control.label.c_str(), value, labels);
                            if (value != before)
                                scripting::runtime().set_control(script.id, control.id, value);
                            break;
                        }
                        case scripting::control_kind::input: {
                            std::array<char, 512> buffer{};
                            if (const auto *value = std::get_if<std::string>(&control.value))
                                std::memcpy(buffer.data(), value->data(),
                                            std::min(value->size(), buffer.size() - 1));
                            const auto before = std::string(buffer.data());
                            text_input_row(control.label.c_str(), buffer.data(), buffer.size());
                            if (before != buffer.data())
                                scripting::runtime().set_control(script.id, control.id,
                                                                 std::string(buffer.data()));
                            break;
                        }
                        case scripting::control_kind::color: {
                            auto value = std::get_if<zdraw::rgba>(&control.value)
                                             ? std::get<zdraw::rgba>(control.value)
                                             : zdraw::rgba{255, 255, 255, 255};
                            const auto before = value.val;
                            color_row(control.label.c_str(), value);
                            if (before != value.val)
                                scripting::runtime().set_control(script.id, control.id, value);
                            break;
                        }
                        case scripting::control_kind::keybind: {
                            auto value = std::get_if<int>(&control.value) ? std::get<int>(control.value) : 0;
                            const auto before = value;
                            keybind_row(control.label.c_str(), value);
                            if (before != value)
                                scripting::runtime().set_control(script.id, control.id, value);
                            break;
                        }
                        case scripting::control_kind::separator:
                            begin_row(control.label.c_str(), 116.0f);
                            ImGui::SeparatorText(control.label.c_str());
                            end_row();
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (button_row("Reload Script", "Reload"))
                        scripting::runtime().reload(script.id);
                });
                if (running != script.enabled)
                    scripting::runtime().set_enabled(script.id, running);
                ImGui::PopID();
            }
        });
    }
    else
    {
        card("config", "CFG FILE", 3, [&] {
            auto &cfg = config::storage;
            text_input_row("File Name", cfg.name_buffer, sizeof(cfg.name_buffer));
            if (button_row("Export Named CFG", "Save As...", row_action_icon::save))
                app::context().overlay.request_config_save();
            if (button_row("Open CFG", "Browse...", row_action_icon::folder))
                app::context().overlay.request_config_load();
        });
    }

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
