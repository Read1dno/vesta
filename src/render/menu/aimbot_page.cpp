#include <stdafx.hpp>
#include <render/menu/internal.hpp>
#include <render/menu/combat_widgets.hpp>

using namespace render::menu::detail;
using namespace render::menu::combat;

void menu_t::draw_aimbot()
{
    begin_combat_page(false);
    auto &global = config::combat_settings.global;
    if (m_weapon_group < 0)
    {
        const auto aim_uses_key = global.aimbot_activation_mode != config::combat_profile::activation::always;
        const auto targeting_rows = global.aimbot_enabled ? (aim_uses_key ? 9 : 8) : 1;
        card_in_column("targeting", "TARGETING", targeting_rows, 0, [&] {
            master_row(global.aimbot_enabled, global.aimbot_activation_mode);
            if (global.aimbot_enabled)
            {
                if (aim_uses_key)
                    keybind_row("Key", global.aimbot_key);
                checks_row(global.aimbot_checks);
                aim_parts_row(global.aimbot_hitbox_parts);
                fov_row(global.aimbot_fov, global.aimbot_fov_config, global.aimbot_draw_fov,
                        global.aimbot_fov_color);
                humanizer_row(global.aimbot_humanize, global.aimbot_smoothing, global.aimbot_humanizer);
                multipoint_row(global.aimbot_multipoint, global.aimbot_multipoint_config);
                prediction_row(global.aimbot_prediction);
                toggle_row("Lethal Only", global.aimbot_lethal_only);
            }
        });
        card_in_column("recoil", "RECOIL CONTROL", 1, 1, [&] { rcs_row(global.aimbot_rcs); });
        if (global.aimbot_enabled)
            card_in_column("penetration", "PENETRATION", 3, 1, [&] {
                visibility_row(global.aimbot_checks);
                slider_row("Min Damage", global.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f);
                damage_override_row(global.aimbot_min_damage_override_enabled,
                                    global.aimbot_min_damage_override, global.aimbot_min_damage_override_mode,
                                    global.aimbot_min_damage_override_key);
            });
    }
    else
    {
        auto &group = config::combat_settings.overrides[m_weapon_group];
        card("override", "OVERRIDE SETTINGS", 1,
             [&] { toggle_row("Inherit Global Settings", group.use_global); });
        if (!group.use_global && global.aimbot_enabled)
        {
            card_in_column("targeting", "TARGETING", 7, 0, [&] {
                checks_row(group.aimbot_checks);
                aim_parts_row(group.aimbot_hitbox_parts);
                fov_row(group.aimbot_fov, group.aimbot_fov_config, global.aimbot_draw_fov,
                        global.aimbot_fov_color);
                humanizer_row(group.aimbot_humanize, group.aimbot_smoothing, group.aimbot_humanizer);
                multipoint_row(group.aimbot_multipoint, group.aimbot_multipoint_config);
                prediction_row(group.aimbot_prediction);
                toggle_row("Lethal Only", group.aimbot_lethal_only);
            });
            card_in_column("recoil", "RECOIL CONTROL", 1, 1, [&] { rcs_row(group.aimbot_rcs); });
            card("penetration", "PENETRATION", 2, [&] {
                visibility_row(group.aimbot_checks);
                slider_row("Min Damage", group.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f);
            });
        }
    }
    end_combat_page();
}
