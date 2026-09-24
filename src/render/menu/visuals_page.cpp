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

void menu_t::draw_visuals()
{
    static constexpr const char *tabs[]{"Players", "Items",   "Projectiles", "Bomb",
                                        "Radar",   "Effects", "Crosshair"};
    ImGui::SetCursorPos({24.0f, 24.0f});
    for (int i = 0; i < 7; ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 10.0f);
        if (tab_button(tabs[i], this->m_visual_group == i) && this->m_visual_group != i)
        {
            this->m_visual_group = i;
            this->reset_content_animation();
        }
    }

    ImGui::SetCursorPos({14.0f, 68.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##visual_cards", {682.0f, 568.0f}, false);
    begin_cards("##visual_grid");

    if (this->m_visual_group == 0)
    {
        auto &p = config::visual_settings.m_player;
        card("master", "MASTER", 2, [&] {
            const auto activation_rows =
                p.activation_mode == config::visual_profile::player::always_on ? 2 : 3;
            toggle_popup_row("Enable Player ESP", p.enabled, activation_rows, [&] {
                static constexpr const char *modes[]{"Always On", "Hold", "Toggle"};
                select_row("Mode", p.activation_mode, modes);
                if (p.activation_mode != config::visual_profile::player::always_on)
                    keybind_row("Key", p.activation_key);
                toggle_row("Spectator Sync", p.spectator_sync);
            });
            toggle_popup_row("Legit Sync", p.m_legit_sync.enabled, 9, [&] {
                toggle_row("Direct Visibility", p.m_legit_sync.direct_visible);
                toggle_row("Radar Spotted", p.m_legit_sync.radar);
                toggle_row("Audible Sounds", p.m_legit_sync.sound);
                slider_row("Radar Hold", p.m_legit_sync.radar_hold, 0.1f, 10.0f, "s", 0.1f);
                slider_row("Sound Hold", p.m_legit_sync.sound_hold, 0.1f, 10.0f, "s", 0.1f);
                slider_row("Hearing Distance", p.m_legit_sync.sound_distance, 100.0f, 2500.0f, "u", 10.0f);
                auto minimum_opacity = p.m_legit_sync.pulse_min_opacity * 100.0f;
                auto maximum_opacity = p.m_legit_sync.pulse_max_opacity * 100.0f;
                slider_row("Pulse Minimum", minimum_opacity, 0.0f, 100.0f, "%", 1.0f);
                slider_row("Pulse Maximum", maximum_opacity, minimum_opacity, 100.0f, "%", 1.0f);
                p.m_legit_sync.pulse_min_opacity = minimum_opacity / 100.0f;
                p.m_legit_sync.pulse_max_opacity = maximum_opacity / 100.0f;
                slider_row("Pulse Period", p.m_legit_sync.pulse_period, 0.4f, 5.0f, "s", 0.1f);
            });
        });

        card("box", "BOUNDING BOX",
             p.m_box.style == config::visual_profile::player::box::style_type::cornered ? 4 : 3, [&] {
                 toggle_row("Show Box", p.m_box.enabled);
                 int style = static_cast<int>(p.m_box.style);
                 static constexpr const char *styles[]{"Full", "Cornered"};
                 select_row("Style", style, styles);
                 p.m_box.style = static_cast<config::visual_profile::player::box::style_type>(style);
                 if (style == 1)
                     slider_row("Corner Length", p.m_box.corner_length, 4.0f, 30.0f, "", 0.5f);
                 toggle_popup_row("Background Fill", p.m_box.fill, 4, [&] {
                     color_row("Box Visible Color", p.m_box.visible_color);
                     color_row("Box Occluded Color", p.m_box.occluded_color);
                     color_row("Fill Visible Color", p.m_box.fill_visible_color);
                     color_row("Fill Occluded Color", p.m_box.fill_occluded_color);
                 });
             });
        card("details", "DETAILS", 8, [&] {
            toggle_color_row("Show Name", p.m_name.enabled, p.m_name.color);
            toggle_popup_row("Show Weapon", p.m_weapon.enabled, k_weapon_settings_rows,
                             [&] { player_weapon_settings_rows(p.m_weapon); });
            toggle_popup_row("Health Bar", p.m_health_bar.enabled, k_bar_settings_rows,
                             [&] { player_bar_settings_rows(p.m_health_bar, p.m_layout.health); });
            toggle_popup_row("Armor Bar", p.m_armor_bar.enabled, k_bar_settings_rows,
                             [&] { player_bar_settings_rows(p.m_armor_bar, p.m_layout.armor); });
            toggle_popup_row("Info Flags", p.m_info_flags.enabled, k_info_flag_settings_rows,
                             [&] { player_info_flag_settings_rows(p.m_info_flags); });
            auto &chams_cfg = config::visual_settings.m_chams;
            // Antialiasing lives in the Chams row's popup: as a plain row it pushed
            // the "Chams Occluded" row out of the card.
            toggle_popup_row("Chams", chams_cfg.enabled, 5, [&] {
                toggle_row("Antialiasing", chams_cfg.antialiasing);
                toggle_popup_row("Model Glow", chams_cfg.glow_effect.enabled, 3, [&] {
                    color_row("Glow Color", chams_cfg.glow_effect.color);
                    slider_row("Glow Radius", chams_cfg.glow_effect.radius, 0.5f, 16.0f, "u", 0.5f);
                    slider_row("Glow Strength", chams_cfg.glow_effect.strength, 0.0f, 1.0f, "", 0.01f);
                });
                toggle_popup_row("Death Shatter", chams_cfg.kill_effect.enabled, 4, [&] {
                    color_row("Particle Color", chams_cfg.kill_effect.color);
                    slider_row("Particle Duration", chams_cfg.kill_effect.duration, 0.2f, 3.0f, "s", 0.05f);
                    slider_row("Particle Size", chams_cfg.kill_effect.size, 1.0f, 8.0f, "px", 0.5f);
                    slider_row("Particle Count", chams_cfg.kill_effect.count, 4, 32);
                });
                toggle_popup_row("On Hit Chams", chams_cfg.on_shot.enabled, 2, [&] {
                    slider_row("Ghost Duration", chams_cfg.on_shot.duration, 0.1f, 3.0f, "s", 0.05f);
                    settings_popup_row("Ghost Appearance",
                                       chams_material_row_count(chams_cfg.on_shot.appearance),
                                       [&] { chams_material_rows(chams_cfg.on_shot.appearance); });
                });
                settings_popup_row("Occlusion", 2, [&] {
                    toggle_row("Dynamic Doors", chams_cfg.occlude_dynamic_doors);
                    toggle_row("Smoke Occlusion", chams_cfg.occlude_smoke);
                });
            });
            toggle_popup_row("Chams Visible", chams_cfg.visible.enabled,
                             chams_material_row_count(chams_cfg.visible),
                             [&] { chams_material_rows(chams_cfg.visible); });
            toggle_popup_row("Chams Occluded", chams_cfg.invisible.enabled,
                             chams_material_row_count(chams_cfg.invisible),
                             [&] { chams_material_rows(chams_cfg.invisible); });
        });
        card("skeleton", "SKELETON & HITBOXES", 6, [&] {
            toggle_color_row("Show Skeleton", p.m_skeleton.enabled, p.m_skeleton.visible_color);
            toggle_color_row("Show Head Circle", p.m_head_circle.enabled, p.m_head_circle.color);
            toggle_color_row("Show View Line", p.m_view_line.enabled, p.m_view_line.color);
            toggle_popup_row("Offscreen Arrows", p.m_offscreen_arrows.enabled, 4, [&] {
                color_row("Arrow Color", p.m_offscreen_arrows.color);
                slider_row("Arrow Size", p.m_offscreen_arrows.size, 6.0f, 40.0f, "", 0.5f);
                slider_row("Arrow Radius", p.m_offscreen_arrows.radius, 40.0f, 500.0f, "", 1.0f);
                toggle_popup_row("Bloom", p.m_offscreen_arrows.bloom, 5, [&] {
                    color_row("Bloom Color", p.m_offscreen_arrows.bloom_color);
                    slider_row("Bloom Radius", p.m_offscreen_arrows.bloom_radius, 1.0f, 16.0f, "px", 0.5f);
                    slider_row("Bloom Speed", p.m_offscreen_arrows.bloom_speed, 0.05f, 2.0f, "Hz", 0.05f);
                    slider_row("Minimum Glow", p.m_offscreen_arrows.bloom_min_alpha, 0.0f, 1.0f, "", 0.01f);
                    slider_row("Maximum Glow", p.m_offscreen_arrows.bloom_max_alpha, 0.0f, 1.0f, "", 0.01f);
                });
            });
            toggle_popup_row("Threat Hitboxes", p.m_threat_module.enabled, 3, [&] {
                color_row("Head Hitbox Color", p.m_threat_module.head_color);
                color_row("Body Hitbox Color", p.m_threat_module.body_color);
                slider_row("Fill Alpha", p.m_threat_module.fill_alpha, 0.0f, 255.0f, "", 1.0f);
            });
            // Sound ESP is a plain on/off row here; every knob lives behind the
            // three-dots popup so it does not turn this tab into a scroll region.
            auto &s = config::visual_settings.m_sound;
            toggle_popup_row("Sound ESP", s.enabled, 4, [&] {
                color_row("Ring Color", s.color);
                toggle_row("Local Sync", s.local_sync);
                slider_row("Duration", s.duration, 0.5f, 4.0f, " s", 0.05f);
                slider_row("Ring Radius", s.radius, 10.0f, 80.0f, "", 0.5f);
            });
        });
    }
    else if (this->m_visual_group == 1)
    {
        auto &p = config::visual_settings.m_item;
        card("item_settings", "SETTINGS", 4, [&] {
            toggle_row("Enable Item ESP", p.enabled);
            slider_row("Max Distance", p.max_distance, 5.0f, 150.0f, "", 1.0f);
            toggle_color_row("Show Icon", p.m_icon.enabled, p.m_icon.color);
            // Distinct from the player row's "Show Name": one is a nickname, the
            // other a weapon name, and they do not translate to the same word.
            toggle_row("Show Item Name", p.m_name.enabled);
        });
        card("item_filters", "FILTERS", 6, [&] {
            toggle_row("Rifles", p.m_filters.rifles);
            toggle_row("SMGs", p.m_filters.smgs);
            toggle_row("Snipers", p.m_filters.snipers);
            toggle_row("Pistols", p.m_filters.pistols);
            toggle_row("Grenades", p.m_filters.grenades);
            toggle_row("Utility", p.m_filters.utility);
        });
    }
    else if (this->m_visual_group == 2)
    {
        auto &p = config::visual_settings.m_projectile;
        card("projectile_elements", "VISUAL ELEMENTS", 5, [&] {
            toggle_row("Enable Projectiles", p.enabled);
            static constexpr const char *display_modes[]{"Indicator", "Text Only"};
            select_row("Display Mode", p.display_mode, display_modes);
            toggle_row(p.display_mode == config::visual_profile::projectile::text_only ? "Show Text"
                                                                                       : "Show Icon",
                       p.show_icon);
            toggle_row(p.display_mode == config::visual_profile::projectile::text_only ? "Effect Timer"
                                                                                       : "Effect Timer Ring",
                       p.show_timer_ring);
            toggle_row("Inferno Bounds", p.show_inferno_bounds);
        });
        card("projectile_colors", "COLORS", 5, [&] {
            color_row("HE Grenade", p.color_he);
            color_row("Flashbang", p.color_flash);
            color_row("Smoke", p.color_smoke);
            color_row("Molotov", p.color_molotov);
            color_row("Decoy", p.color_decoy);
        });
        card("projectile_indicator", "INDICATOR", 3, [&] {
            color_row("Timer Full", p.timer_high_color);
            color_row("Timer Low", p.timer_low_color);
            color_row("Indicator Background", p.indicator_background);
        });
        card("projectile_inferno", "INFERNO GRADIENT", 2, [&] {
            slider_row("Gradient Width", p.inferno_gradient_width, 8.0f, 80.0f, " px", 1.0f);
            slider_row("Gradient Opacity", p.inferno_gradient_opacity, 0.0f, 100.0f, "%", 1.0f);
        });
    }
    else if (this->m_visual_group == 3)
    {
        auto &p = config::visual_settings.m_bomb;
        card("bomb_states", "STATES", 6, [&] {
            toggle_row("Enable Bomb ESP", p.enabled);
            static constexpr const char *display_modes[]{"Indicator", "Text Only"};
            select_row("Display Mode", p.display_mode, display_modes);
            toggle_color_row("Show Carrier", p.show_active_bomb, p.active_bomb_color);
            toggle_popup_row("Show Planted", p.show_planted_bomb, 2, [&] {
                color_row("Planted Color", p.bomb_color_t);
                color_row("Defusing Color", p.bomb_color_ct);
            });
            toggle_row(p.display_mode == config::visual_profile::bomb::text_only ? "World Timer"
                                                                                 : "World Timer Ring",
                       p.show_timer);
            toggle_popup_row("Bomb Info Panel", p.show_info_panel, 2, [&] {
                color_row("Timer Color", p.timer_text_color);
                color_row("Panel Background", p.panel_background);
            });
        });
        card("bomb_safe_zone", "SAFE ZONE (BAKED)", 1, [&] {
            toggle_popup_row("Safe Zone Contour", p.show_safe_zone, 4, [&] {
                color_row("Zone Color", p.safe_zone_color);
                slider_row("Gradient Bands", p.safe_zone_bands, 1, 8);
                slider_row("Band Step", p.safe_zone_band_step, 4.0f, 40.0f, "HP", 1.0f);
                slider_row("Render Radius", p.safe_zone_draw_radius, 200.0f, 2500.0f, "u", 10.0f);
            });
        });
    }
    else if (this->m_visual_group == 4)
    {
        auto &p = config::visual_settings.m_radar;
        card_in_column("radar_players", "RADAR PLAYERS", 5, 0, [&] {
            const auto uses_key = p.activation_mode != config::visual_profile::radar::always_on;
            toggle_popup_row("Overlay Enemy Markers", p.enabled, uses_key ? 2 : 1, [&] {
                int mode = p.activation_mode;
                static constexpr const char *modes[]{"Always On", "Hold", "Toggle"};
                select_row("Activation", mode, modes);
                p.activation_mode =
                    std::clamp(mode, static_cast<int>(config::visual_profile::radar::always_on),
                               static_cast<int>(config::visual_profile::radar::toggle));
                if (p.activation_mode != config::visual_profile::radar::always_on)
                    keybind_row("Key", p.activation_key);
            });
            toggle_color_row("Player Names", p.show_names, p.name_color);
            toggle_color_row("Player Health", p.show_health, p.health_color);
            toggle_color_row("Player Armor", p.show_armor, p.armor_color);
            toggle_color_row("Player Weapon", p.show_weapon, p.weapon_color);
        });
        card_in_column("radar_style", "PLAYER STYLE", 5, 0, [&] {
            color_row("Enemy Marker", p.enemy_color);
            color_row("View Direction", p.direction_color);
            color_row("Status Text", p.status_color);
            slider_row("Marker Scale", p.marker_scale, 0.5f, 2.0f, "", 0.05f);
            toggle_popup_row("Text Outline", p.text_outline, 2, [&] {
                color_row("Outline Color", p.text_outline_color);
                slider_row("Outline Thickness", p.text_outline_thickness, 0.5f, 3.0f, " px", 0.5f);
            });
        });
        card_in_column("radar_grenades", "RADAR GRENADES", 3, 1, [&] {
            toggle_popup_row("Grenade Markers", p.show_projectiles, 6, [&] {
                color_row("HE Color", p.he_color);
                color_row("Flash Color", p.flash_color);
                color_row("Smoke Color", p.smoke_color);
                color_row("Molotov Color", p.molotov_color);
                color_row("Decoy Color", p.decoy_color);
                slider_row("Information Scale", p.information_scale, 0.5f, 1.5f, "", 0.05f);
            });
            toggle_popup_row("Grenade Trajectories", p.show_trajectories, 2, [&] {
                slider_row("Line Thickness", p.trajectory_thickness, 0.5f, 6.0f, " px", 0.25f);
                slider_row("End Point Size", p.trajectory_endpoint_size, 1.0f, 10.0f, " px", 0.5f);
            });
            toggle_popup_row("Grenade Zones", p.show_grenade_zones, 3, [&] {
                slider_row("Fill Opacity", p.zone_fill_alpha, 0.0f, 100.0f, "%", 1.0f);
                slider_row("Outline Opacity", p.zone_outline_alpha, 0.0f, 100.0f, "%", 1.0f);
                slider_row("Outline Thickness", p.zone_outline_thickness, 0.5f, 5.0f, " px", 0.25f);
            });
        });
    }
    else if (this->m_visual_group == 5)
    {
        auto &general = config::general_settings;
        card("bullet_effects", "BULLET EFFECTS", 3, [&] {
            auto &tracers = general.m_bullet_tracers;
            toggle_popup_row("Enable Bullet Tracers", tracers.enabled, 5, [&] {
                settings_popup_row("Appearance", 3, [&] {
                    color_row("Tracer Color", tracers.color);
                    slider_row("Thickness", tracers.thickness, 1.0f, 10.0f, "", 0.25f);
                    toggle_row("Bloom Effect", tracers.bloom);
                });
                settings_popup_row("Lifetime", 2, [&] {
                    slider_row("Fade Duration", tracers.duration, 1.0f, 10.0f, "s", 0.1f);
                    slider_row("Max Tracers", tracers.max_count, 1, 100);
                });
                toggle_row("Draw Trajectory Line", tracers.draw_line);
                toggle_popup_row("Impact Cubes", tracers.draw_cubes, 3, [&] {
                    slider_row("Cube Size", tracers.cube_half, 0.5f, 5.0f, "", 0.25f);
                    color_row("Cube Edge Color", tracers.cube_edge_color);
                    slider_row("Cube Face Alpha", tracers.cube_face_alpha, 0.0f, 160.0f, "", 1.0f);
                });
                settings_popup_row("Distance Fade", 2, [&] {
                    slider_row("Fade Near", tracers.fade_near, 5.0f, 200.0f, "u", 1.0f);
                    slider_row("Fade Far", tracers.fade_far, 50.0f, 500.0f, "u", 1.0f);
                });
            });

            auto &marker = general.m_hitmarker;
            toggle_popup_row("World Hitmarker", marker.enabled, 3, [&] {
                color_row("Marker Color", marker.color);
                settings_popup_row("Geometry", 3, [&] {
                    slider_row("Marker Size", marker.size, 3.0f, 18.0f, "", 0.5f);
                    slider_row("Center Gap", marker.gap, 0.0f, 10.0f, "", 0.5f);
                    slider_row("Marker Thickness", marker.thickness, 1.0f, 4.0f, "", 0.25f);
                });
                slider_row("Marker Duration", marker.duration, 0.15f, 1.5f, "s", 0.05f);
            });

            static constexpr std::array<const char *, 5> hitsound_styles{"Soft", "Glass", "Pluck", "Crisp",
                                                                         "Flesh"};
            auto &sound = general.m_hitsound;
            toggle_popup_row("Hit Sound", sound.enabled, 4, [&] {
                select_row("Sound", sound.style, hitsound_styles);
                slider_percent_row("Volume", sound.volume);
                if (button_row("Preview", "Play"))
                    features::visuals::hitsounds().play(sound.style, sound.volume);
                toggle_popup_row("Floating Damage", sound.show_damage, 4, [&] {
                    color_row("Damage Color", sound.damage_color);
                    slider_row("Damage Size", sound.damage_size, 8.0f, 28.0f, "", 0.5f);
                    slider_row("Damage Duration", sound.damage_duration, 0.15f, 2.0f, "s", 0.05f);
                    slider_row("Damage Rise", sound.damage_rise, 0.0f, 100.0f, "px", 1.0f);
                });
            });
        });

        auto &no_flash = config::visual_settings.m_no_flash;
        card("no_flash", "NO FLASH (WIREFRAME)", 1, [&] {
            toggle_popup_row("Enable Visual No Flash", no_flash.enabled, 3, [&] {
                slider_row("Render Distance", no_flash.max_distance, 200.0f, 3000.0f, "", 10.0f);
                color_row("Dimming Overlay Color", no_flash.background_color);
                color_row("Wireframe Color", no_flash.wireframe_color);
            });
        });

        auto &no_smoke = config::visual_settings.m_no_smoke;
        card("no_smoke", "NO SMOKE (WIREFRAME)", 1, [&] {
            toggle_popup_row("Enable Visual No Smoke", no_smoke.enabled, 1,
                             [&] { color_row("Smoke Wireframe Color", no_smoke.wireframe_color); });
        });
    }
    else
    {
        auto &p = config::visual_settings.m_crosshair;
        card("crosshair_settings", "CROSSHAIR SETTINGS", 5, [&] {
            toggle_row("Enable Crosshair", p.enabled);
            toggle_row("Copy Game Crosshair", p.copy_game);
            toggle_row("Draw Dot", p.dot);
            toggle_popup_row("Draw Lines", p.lines, 3, [&] {
                toggle_row("T-Style", p.t_style);
                slider_row("Length", p.length, 1.0f, 50.0f, "", 0.5f);
                slider_row("Gap", p.gap, 0.0f, 50.0f, "", 0.5f);
            });
            slider_row("Thickness", p.thickness, 1.0f, 10.0f, "", 0.25f);
        });
        card("crosshair_colors", "COLORS", 3, [&] {
            toggle_popup_row("Draw Outline", p.outline, 2, [&] {
                color_row("Outline Color", p.outline_color);
                slider_row("Outline Thickness", p.outline_thickness, 0.5f, 3.0f, "", 0.5f);
            });
            color_row("Primary Color", p.color);
            toggle_popup_row("Penetration Indicator", p.penetration_enabled, 3, [&] {
                color_row("Can Penetrate", p.penetration_color_yes);
                color_row("Cannot Penetrate", p.penetration_color_no);
                slider_row("Min Damage", p.penetration_min_damage, 1.0f, 200.0f, "", 1.0f);
            });
        });
    }

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
