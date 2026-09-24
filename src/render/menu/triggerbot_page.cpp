#include <stdafx.hpp>
#include <render/menu/internal.hpp>
#include <render/menu/combat_widgets.hpp>

using namespace render::menu::detail;
using namespace render::menu::combat;

void menu_t::draw_triggerbot()
{
    begin_combat_page(true);
    auto &global = config::combat_settings.global;
    if (m_weapon_group < 0)
    {
        static constexpr const char *seed_types[]{"None", "Restricted", "Unrestricted"};
        const auto global_seeded = global.triggerbot_seed_type != config::combat_profile::seed_mode::none;
        const auto trigger_uses_key =
            global.triggerbot_activation_mode != config::combat_profile::activation::always;
        const auto trigger_rows =
            global.triggerbot_enabled ? (trigger_uses_key ? 1 : 0) + (global_seeded ? 7 : 8) : 1;
        card_in_column("trigger_core", "TRIGGERBOT CORE", trigger_rows, 0, [&] {
            master_row(global.triggerbot_enabled, global.triggerbot_activation_mode);
            if (global.triggerbot_enabled)
            {
                if (trigger_uses_key)
                    keybind_row("Key", global.triggerbot_key);
                checks_row(global.triggerbot_checks);
                select_row("Seed Type", global.triggerbot_seed_type, seed_types);
                aim_parts_row(global.triggerbot_hitbox_parts);
                if (global_seeded)
                    slider_row("Reaction Time (ms)", global.triggerbot_reaction_time, 0, 400);
                else
                {
                    slider_row("Hitchance", global.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f);
                    trigger_timing_row(global.triggerbot_delay, global.triggerbot_randomize_ms,
                                       global.triggerbot_outlier_chance, global.triggerbot_outlier_delay_ms,
                                       global.triggerbot_delay_after_ms);
                }
                toggle_row("Predictive", global.triggerbot_predictive);
                toggle_row("Lethal Only", global.triggerbot_lethal_only);
            }
        });
        if (global.triggerbot_enabled)
            card_in_column("trigger_penetration", "PENETRATION", 3, 1, [&] {
                visibility_row(global.triggerbot_checks);
                slider_row("Min Damage", global.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f);
                damage_override_row(
                    global.triggerbot_min_damage_override_enabled, global.triggerbot_min_damage_override,
                    global.triggerbot_min_damage_override_mode, global.triggerbot_min_damage_override_key);
            });
    }
    else
    {
        auto &group = config::combat_settings.overrides[m_weapon_group];
        card("override", "OVERRIDE SETTINGS", 1,
             [&] { toggle_row("Inherit Global Settings", group.use_global); });
        if (!group.use_global && global.triggerbot_enabled)
        {
            static constexpr const char *seed_types[]{"None", "Restricted", "Unrestricted"};
            const auto group_seeded = group.triggerbot_seed_type != config::combat_profile::seed_mode::none;
            card_in_column("trigger_core", "TRIGGERBOT CORE", group_seeded ? 6 : 7, 0, [&] {
                checks_row(group.triggerbot_checks);
                select_row("Seed Type", group.triggerbot_seed_type, seed_types);
                aim_parts_row(group.triggerbot_hitbox_parts);
                if (group_seeded)
                {
                    slider_row("Reaction Time (ms)", group.triggerbot_reaction_time, 0, 400);
                }
                else
                {
                    slider_row("Hitchance", group.triggerbot_hitchance, 0.0f, 100.0f, "%", 1.0f);
                    trigger_timing_row(group.triggerbot_delay, group.triggerbot_randomize_ms,
                                       group.triggerbot_outlier_chance, group.triggerbot_outlier_delay_ms,
                                       group.triggerbot_delay_after_ms);
                }
                toggle_row("Predictive", group.triggerbot_predictive);
                toggle_row("Lethal Only", group.triggerbot_lethal_only);
            });
            card_in_column("trigger_penetration", "PENETRATION", 2, 1, [&] {
                visibility_row(group.triggerbot_checks);
                slider_row("Min Damage", group.triggerbot_min_damage, 1.0f, 100.0f, "", 1.0f);
            });
        }
    }
    end_combat_page();
}
