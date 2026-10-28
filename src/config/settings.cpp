#include <stdafx.hpp>
#include <game/state/constants.hpp>
#include <diagnostics/log.hpp>
#include <platform/windows/storage.hpp>

#include <config/default_profile.hpp>
#include <config/settings.hpp>
#include <config/config_file.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <shlwapi.h>

using json = nlohmann::json;

namespace foundation
{
// JSON form of a colour: {"r":..,"g":..,"b":..,"a":..}. In the type's namespace so nlohmann finds it.
void to_json(nlohmann::json& j, const rgba& color)
{
    j = nlohmann::json{{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
}

void from_json(const nlohmann::json& j, rgba& color)
{
    j.at("r").get_to(color.r);
    j.at("g").get_to(color.g);
    j.at("b").get_to(color.b);
    j.at("a").get_to(color.a);
}
} // namespace foundation

namespace config
{

// Reads `key` into `value` when the document has it; a missing key keeps the current (default) value.
template<typename value_t>
void read_field(const json& j, const char* key, value_t& value)
{
    if (const auto it = j.find(key); it != j.end())
        it->get_to(value);
}

static std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

static void to_json(json&, const combat_profile::legit_checks&);
static void from_json(const json&, combat_profile::legit_checks&);
static void to_json(json&, const combat_profile::humanizer_settings&);
static void from_json(const json&, combat_profile::humanizer_settings&);
static void to_json(json&, const combat_profile::multipoint_settings&);
static void from_json(const json&, combat_profile::multipoint_settings&);
static void to_json(json&, const combat_profile::prediction_settings&);
static void from_json(const json&, combat_profile::prediction_settings&);
static void to_json(json&, const combat_profile::rcs_settings&);
static void from_json(const json&, combat_profile::rcs_settings&);
static void to_json(json&, const combat_profile::fov_settings&);
static void from_json(const json&, combat_profile::fov_settings&);

static void to_json(json& j, const combat_profile::global_settings& g)
{
    j = json{{"aimbot_enabled", g.aimbot_enabled},
             {"aimbot_key", g.aimbot_key},
             {"aimbot_activation_mode", g.aimbot_activation_mode},
             {"aimbot_fov", g.aimbot_fov},
             {"aimbot_smoothing", g.aimbot_smoothing},
             {"aimbot_humanize", g.aimbot_humanize},
             {"aimbot_autowall", g.aimbot_autowall},
             {"aimbot_min_damage", g.aimbot_min_damage},
             {"aimbot_min_damage_override_enabled", g.aimbot_min_damage_override_enabled},
             {"aimbot_min_damage_override", g.aimbot_min_damage_override},
             {"aimbot_min_damage_override_mode", g.aimbot_min_damage_override_mode},
             {"aimbot_min_damage_override_key", g.aimbot_min_damage_override_key},
             {"aimbot_lethal_only", g.aimbot_lethal_only},
             {"aimbot_hitbox_parts", g.aimbot_hitbox_parts},
             {"aimbot_multipoint", g.aimbot_multipoint},
             {"aimbot_visible_only", g.aimbot_visible_only},
             {"aimbot_draw_fov", g.aimbot_draw_fov},
             {"aimbot_fov_color", g.aimbot_fov_color},
             {"aimbot_predictive", g.aimbot_predictive},
             {"aimbot_recoil_sync", g.aimbot_recoil_sync},
             {"aimbot_checks", g.aimbot_checks},
             {"aimbot_humanizer", g.aimbot_humanizer},
             {"aimbot_multipoint_config", g.aimbot_multipoint_config},
             {"aimbot_prediction", g.aimbot_prediction},
             {"aimbot_rcs", g.aimbot_rcs},
             {"aimbot_fov_config", g.aimbot_fov_config},
             {"grenade_aim_enabled", g.grenade_aim.enabled},
             {"grenade_aim_key", g.grenade_aim.key},
             {"grenade_aim_fov", g.grenade_aim.fov},
             {"grenade_aim_smoothing", g.grenade_aim.smoothing},
             {"triggerbot_enabled", g.triggerbot_enabled},
             {"triggerbot_key", g.triggerbot_key},
             {"triggerbot_activation_mode", g.triggerbot_activation_mode},
             {"triggerbot_seed_type", g.triggerbot_seed_type},
             {"triggerbot_hitbox_parts", g.triggerbot_hitbox_parts},
             {"triggerbot_hitchance", g.triggerbot_hitchance},
             {"triggerbot_delay", g.triggerbot_delay},
             {"triggerbot_randomize_ms", g.triggerbot_randomize_ms},
             {"triggerbot_outlier_chance", g.triggerbot_outlier_chance},
             {"triggerbot_outlier_delay_ms", g.triggerbot_outlier_delay_ms},
             {"triggerbot_delay_after_ms", g.triggerbot_delay_after_ms},
             {"triggerbot_lethal_only", g.triggerbot_lethal_only},
             {"triggerbot_reaction_time", g.triggerbot_reaction_time},
             {"triggerbot_min_damage", g.triggerbot_min_damage},
             {"triggerbot_min_damage_override_enabled", g.triggerbot_min_damage_override_enabled},
             {"triggerbot_min_damage_override", g.triggerbot_min_damage_override},
             {"triggerbot_min_damage_override_mode", g.triggerbot_min_damage_override_mode},
             {"triggerbot_min_damage_override_key", g.triggerbot_min_damage_override_key},
             {"triggerbot_autowall", g.triggerbot_autowall},
             {"triggerbot_autostop", g.triggerbot_autostop},
             {"triggerbot_predictive", g.triggerbot_predictive},
             {"triggerbot_revolver_pre_cock", g.triggerbot_revolver_pre_cock},
             {"triggerbot_revolver_release_margin_ms", g.triggerbot_revolver_release_margin_ms},
             {"triggerbot_checks", g.triggerbot_checks},
             {"penetration_crosshair", g.penetration_crosshair},
             {"penetration_color_yes", g.penetration_color_yes},
             {"penetration_color_no", g.penetration_color_no}};
}

static void from_json(const json& j, combat_profile::global_settings& g)
{
    read_field(j, "aimbot_enabled", g.aimbot_enabled);
    read_field(j, "aimbot_key", g.aimbot_key);
    read_field(j, "aimbot_activation_mode", g.aimbot_activation_mode);
    g.aimbot_activation_mode = std::clamp(g.aimbot_activation_mode, static_cast<int>(combat_profile::activation::hold),
                                          static_cast<int>(combat_profile::activation::toggle));
    read_field(j, "aimbot_fov", g.aimbot_fov);
    read_field(j, "aimbot_smoothing", g.aimbot_smoothing);
    read_field(j, "aimbot_humanize", g.aimbot_humanize);
    read_field(j, "aimbot_autowall", g.aimbot_autowall);
    read_field(j, "aimbot_min_damage", g.aimbot_min_damage);
    read_field(j, "aimbot_min_damage_override_enabled", g.aimbot_min_damage_override_enabled);
    read_field(j, "aimbot_min_damage_override", g.aimbot_min_damage_override);
    read_field(j, "aimbot_min_damage_override_mode", g.aimbot_min_damage_override_mode);
    read_field(j, "aimbot_min_damage_override_key", g.aimbot_min_damage_override_key);
    g.aimbot_min_damage_override = std::clamp(g.aimbot_min_damage_override, 1.0f, 100.0f);
    g.aimbot_min_damage_override_mode =
        std::clamp(g.aimbot_min_damage_override_mode, static_cast<int>(combat_profile::activation::hold),
                   static_cast<int>(combat_profile::activation::toggle));
    g.aimbot_min_damage_override_key = std::clamp(g.aimbot_min_damage_override_key, 0, 255);
    read_field(j, "aimbot_lethal_only", g.aimbot_lethal_only);
    read_field(j, "aimbot_hitbox_parts", g.aimbot_hitbox_parts);
    read_field(j, "aimbot_multipoint", g.aimbot_multipoint);
    read_field(j, "aimbot_visible_only", g.aimbot_visible_only);
    read_field(j, "aimbot_draw_fov", g.aimbot_draw_fov);
    read_field(j, "aimbot_fov_color", g.aimbot_fov_color);
    read_field(j, "aimbot_predictive", g.aimbot_predictive);
    read_field(j, "aimbot_recoil_sync", g.aimbot_recoil_sync);
    if (j.contains("aimbot_checks"))
        j.at("aimbot_checks").get_to(g.aimbot_checks);
    else if (g.aimbot_visible_only)
        g.aimbot_checks.walls =
            g.aimbot_autowall ? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
    else
        g.aimbot_checks.walls = combat_profile::wall_policy::penetration;
    read_field(j, "aimbot_humanizer", g.aimbot_humanizer);
    read_field(j, "aimbot_multipoint_config", g.aimbot_multipoint_config);
    if (j.contains("aimbot_prediction"))
        j.at("aimbot_prediction").get_to(g.aimbot_prediction);
    else
        g.aimbot_prediction.enabled = g.aimbot_predictive;
    if (j.contains("aimbot_rcs"))
        j.at("aimbot_rcs").get_to(g.aimbot_rcs);
    else
        g.aimbot_rcs.enabled = g.aimbot_recoil_sync;
    if (j.contains("aimbot_fov_config"))
        j.at("aimbot_fov_config").get_to(g.aimbot_fov_config);
    else
        g.aimbot_fov_config.far_fov = static_cast<float>(g.aimbot_fov);
    read_field(j, "grenade_aim_enabled", g.grenade_aim.enabled);
    read_field(j, "grenade_aim_key", g.grenade_aim.key);
    read_field(j, "grenade_aim_fov", g.grenade_aim.fov);
    read_field(j, "grenade_aim_smoothing", g.grenade_aim.smoothing);
    read_field(j, "triggerbot_enabled", g.triggerbot_enabled);
    read_field(j, "triggerbot_key", g.triggerbot_key);
    read_field(j, "triggerbot_activation_mode", g.triggerbot_activation_mode);
    g.triggerbot_activation_mode =
        std::clamp(g.triggerbot_activation_mode, static_cast<int>(combat_profile::activation::hold),
                   static_cast<int>(combat_profile::activation::toggle));
    // Legacy Simple was equivalent to unseeded Advanced with zero hitchance and
    // only a pre-shot delay. Migrate it once while no longer serializing the mode.
    const auto legacy_simple_mode = j.value("triggerbot_mode", 0) == 1;
    if (j.contains("triggerbot_seed_type"))
        j.at("triggerbot_seed_type").get_to(g.triggerbot_seed_type);
    else if (j.contains("triggerbot_seed"))
        g.triggerbot_seed_type = j.at("triggerbot_seed").get<bool>() ? combat_profile::seed_mode::unrestricted
                                                                     : combat_profile::seed_mode::none;
    g.triggerbot_seed_type = std::clamp(g.triggerbot_seed_type, static_cast<int>(combat_profile::seed_mode::none),
                                        static_cast<int>(combat_profile::seed_mode::unrestricted));
    read_field(j, "triggerbot_hitbox_parts", g.triggerbot_hitbox_parts);
    g.triggerbot_hitbox_parts &= combat_profile::aim_part::all;
    read_field(j, "triggerbot_hitchance", g.triggerbot_hitchance);
    read_field(j, "triggerbot_delay", g.triggerbot_delay);
    read_field(j, "triggerbot_randomize_ms", g.triggerbot_randomize_ms);
    read_field(j, "triggerbot_outlier_chance", g.triggerbot_outlier_chance);
    read_field(j, "triggerbot_outlier_delay_ms", g.triggerbot_outlier_delay_ms);
    read_field(j, "triggerbot_delay_after_ms", g.triggerbot_delay_after_ms);
    read_field(j, "triggerbot_lethal_only", g.triggerbot_lethal_only);
    g.triggerbot_randomize_ms = std::clamp(g.triggerbot_randomize_ms, 0, 100);
    g.triggerbot_outlier_chance = std::clamp(g.triggerbot_outlier_chance, 0.0f, 100.0f);
    g.triggerbot_outlier_delay_ms = std::clamp(g.triggerbot_outlier_delay_ms, 0, 500);
    g.triggerbot_delay_after_ms = std::clamp(g.triggerbot_delay_after_ms, 0, 1000);
    read_field(j, "triggerbot_reaction_time", g.triggerbot_reaction_time);
    if (legacy_simple_mode)
    {
        g.triggerbot_seed_type = combat_profile::seed_mode::none;
        g.triggerbot_hitchance = 0.0f;
        g.triggerbot_delay = std::clamp(j.value("triggerbot_simple_delay", g.triggerbot_delay), 0, 500);
    }
    read_field(j, "triggerbot_min_damage", g.triggerbot_min_damage);
    read_field(j, "triggerbot_min_damage_override_enabled", g.triggerbot_min_damage_override_enabled);
    read_field(j, "triggerbot_min_damage_override", g.triggerbot_min_damage_override);
    read_field(j, "triggerbot_min_damage_override_mode", g.triggerbot_min_damage_override_mode);
    read_field(j, "triggerbot_min_damage_override_key", g.triggerbot_min_damage_override_key);
    g.triggerbot_min_damage_override = std::clamp(g.triggerbot_min_damage_override, 1.0f, 100.0f);
    g.triggerbot_min_damage_override_mode =
        std::clamp(g.triggerbot_min_damage_override_mode, static_cast<int>(combat_profile::activation::hold),
                   static_cast<int>(combat_profile::activation::toggle));
    g.triggerbot_min_damage_override_key = std::clamp(g.triggerbot_min_damage_override_key, 0, 255);
    read_field(j, "triggerbot_autowall", g.triggerbot_autowall);
    read_field(j, "triggerbot_autostop", g.triggerbot_autostop);
    if (j.value("triggerbot_early_autostop", false))
        g.triggerbot_autostop = true;
    read_field(j, "triggerbot_predictive", g.triggerbot_predictive);
    read_field(j, "triggerbot_revolver_pre_cock", g.triggerbot_revolver_pre_cock);
    read_field(j, "triggerbot_revolver_release_margin_ms", g.triggerbot_revolver_release_margin_ms);
    g.triggerbot_revolver_release_margin_ms = std::clamp(g.triggerbot_revolver_release_margin_ms, 1, 60);
    if (j.contains("triggerbot_checks"))
        j.at("triggerbot_checks").get_to(g.triggerbot_checks);
    else
        g.triggerbot_checks.walls =
            g.triggerbot_autowall ? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
    read_field(j, "penetration_crosshair", g.penetration_crosshair);
    read_field(j, "penetration_color_yes", g.penetration_color_yes);
    read_field(j, "penetration_color_no", g.penetration_color_no);
}

static void to_json(json& j, const combat_profile::group_overrides& o)
{
    j = json{{"use_global", o.use_global},
             {"aimbot_fov", o.aimbot_fov},
             {"aimbot_smoothing", o.aimbot_smoothing},
             {"aimbot_humanize", o.aimbot_humanize},
             {"aimbot_autowall", o.aimbot_autowall},
             {"aimbot_min_damage", o.aimbot_min_damage},
             {"aimbot_lethal_only", o.aimbot_lethal_only},
             {"aimbot_hitbox_parts", o.aimbot_hitbox_parts},
             {"aimbot_multipoint", o.aimbot_multipoint},
             {"aimbot_visible_only", o.aimbot_visible_only},
             {"aimbot_predictive", o.aimbot_predictive},
             {"aimbot_recoil_sync", o.aimbot_recoil_sync},
             {"aimbot_checks", o.aimbot_checks},
             {"aimbot_humanizer", o.aimbot_humanizer},
             {"aimbot_multipoint_config", o.aimbot_multipoint_config},
             {"aimbot_prediction", o.aimbot_prediction},
             {"aimbot_rcs", o.aimbot_rcs},
             {"aimbot_fov_config", o.aimbot_fov_config},
             {"triggerbot_seed_type", o.triggerbot_seed_type},
             {"triggerbot_hitbox_parts", o.triggerbot_hitbox_parts},
             {"triggerbot_hitchance", o.triggerbot_hitchance},
             {"triggerbot_delay", o.triggerbot_delay},
             {"triggerbot_randomize_ms", o.triggerbot_randomize_ms},
             {"triggerbot_outlier_chance", o.triggerbot_outlier_chance},
             {"triggerbot_outlier_delay_ms", o.triggerbot_outlier_delay_ms},
             {"triggerbot_delay_after_ms", o.triggerbot_delay_after_ms},
             {"triggerbot_lethal_only", o.triggerbot_lethal_only},
             {"triggerbot_reaction_time", o.triggerbot_reaction_time},
             {"triggerbot_min_damage", o.triggerbot_min_damage},
             {"triggerbot_autowall", o.triggerbot_autowall},
             {"triggerbot_autostop", o.triggerbot_autostop},
             {"triggerbot_predictive", o.triggerbot_predictive},
             {"triggerbot_revolver_pre_cock", o.triggerbot_revolver_pre_cock},
             {"triggerbot_revolver_release_margin_ms", o.triggerbot_revolver_release_margin_ms},
             {"triggerbot_checks", o.triggerbot_checks}};
}

static void from_json(const json& j, combat_profile::group_overrides& o)
{
    read_field(j, "use_global", o.use_global);
    read_field(j, "aimbot_fov", o.aimbot_fov);
    read_field(j, "aimbot_smoothing", o.aimbot_smoothing);
    read_field(j, "aimbot_humanize", o.aimbot_humanize);
    read_field(j, "aimbot_autowall", o.aimbot_autowall);
    read_field(j, "aimbot_min_damage", o.aimbot_min_damage);
    read_field(j, "aimbot_lethal_only", o.aimbot_lethal_only);
    read_field(j, "aimbot_hitbox_parts", o.aimbot_hitbox_parts);
    read_field(j, "aimbot_multipoint", o.aimbot_multipoint);
    read_field(j, "aimbot_visible_only", o.aimbot_visible_only);
    read_field(j, "aimbot_predictive", o.aimbot_predictive);
    read_field(j, "aimbot_recoil_sync", o.aimbot_recoil_sync);
    if (j.contains("aimbot_checks"))
        j.at("aimbot_checks").get_to(o.aimbot_checks);
    else if (o.aimbot_visible_only)
        o.aimbot_checks.walls =
            o.aimbot_autowall ? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
    else
        o.aimbot_checks.walls = combat_profile::wall_policy::penetration;
    read_field(j, "aimbot_humanizer", o.aimbot_humanizer);
    read_field(j, "aimbot_multipoint_config", o.aimbot_multipoint_config);
    if (j.contains("aimbot_prediction"))
        j.at("aimbot_prediction").get_to(o.aimbot_prediction);
    else
        o.aimbot_prediction.enabled = o.aimbot_predictive;
    if (j.contains("aimbot_rcs"))
        j.at("aimbot_rcs").get_to(o.aimbot_rcs);
    else
        o.aimbot_rcs.enabled = o.aimbot_recoil_sync;
    if (j.contains("aimbot_fov_config"))
        j.at("aimbot_fov_config").get_to(o.aimbot_fov_config);
    else
        o.aimbot_fov_config.far_fov = static_cast<float>(o.aimbot_fov);
    const auto legacy_simple_mode = j.value("triggerbot_mode", 0) == 1;
    if (j.contains("triggerbot_seed_type"))
        j.at("triggerbot_seed_type").get_to(o.triggerbot_seed_type);
    else if (j.contains("triggerbot_seed"))
        o.triggerbot_seed_type = j.at("triggerbot_seed").get<bool>() ? combat_profile::seed_mode::unrestricted
                                                                     : combat_profile::seed_mode::none;
    o.triggerbot_seed_type = std::clamp(o.triggerbot_seed_type, static_cast<int>(combat_profile::seed_mode::none),
                                        static_cast<int>(combat_profile::seed_mode::unrestricted));
    read_field(j, "triggerbot_hitbox_parts", o.triggerbot_hitbox_parts);
    o.triggerbot_hitbox_parts &= combat_profile::aim_part::all;
    read_field(j, "triggerbot_hitchance", o.triggerbot_hitchance);
    read_field(j, "triggerbot_delay", o.triggerbot_delay);
    read_field(j, "triggerbot_randomize_ms", o.triggerbot_randomize_ms);
    read_field(j, "triggerbot_outlier_chance", o.triggerbot_outlier_chance);
    read_field(j, "triggerbot_outlier_delay_ms", o.triggerbot_outlier_delay_ms);
    read_field(j, "triggerbot_delay_after_ms", o.triggerbot_delay_after_ms);
    read_field(j, "triggerbot_lethal_only", o.triggerbot_lethal_only);
    o.triggerbot_randomize_ms = std::clamp(o.triggerbot_randomize_ms, 0, 100);
    o.triggerbot_outlier_chance = std::clamp(o.triggerbot_outlier_chance, 0.0f, 100.0f);
    o.triggerbot_outlier_delay_ms = std::clamp(o.triggerbot_outlier_delay_ms, 0, 500);
    o.triggerbot_delay_after_ms = std::clamp(o.triggerbot_delay_after_ms, 0, 1000);
    read_field(j, "triggerbot_reaction_time", o.triggerbot_reaction_time);
    if (legacy_simple_mode)
    {
        o.triggerbot_seed_type = combat_profile::seed_mode::none;
        o.triggerbot_hitchance = 0.0f;
        o.triggerbot_delay = std::clamp(j.value("triggerbot_simple_delay", o.triggerbot_delay), 0, 500);
    }
    read_field(j, "triggerbot_min_damage", o.triggerbot_min_damage);
    read_field(j, "triggerbot_autowall", o.triggerbot_autowall);
    read_field(j, "triggerbot_autostop", o.triggerbot_autostop);
    if (j.value("triggerbot_early_autostop", false))
        o.triggerbot_autostop = true;
    read_field(j, "triggerbot_predictive", o.triggerbot_predictive);
    read_field(j, "triggerbot_revolver_pre_cock", o.triggerbot_revolver_pre_cock);
    read_field(j, "triggerbot_revolver_release_margin_ms", o.triggerbot_revolver_release_margin_ms);
    o.triggerbot_revolver_release_margin_ms = std::clamp(o.triggerbot_revolver_release_margin_ms, 1, 60);
    if (j.contains("triggerbot_checks"))
        j.at("triggerbot_checks").get_to(o.triggerbot_checks);
    else
        o.triggerbot_checks.walls =
            o.triggerbot_autowall ? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
}

static void to_json(json& j, const visual_profile::player::box& b)
{
    j = json{{"enabled", b.enabled},
             {"style", static_cast<int>(b.style)},
             {"fill", b.fill},
             {"outline", b.outline},
             {"corner_length", b.corner_length},
             {"visible_color", b.visible_color},
             {"occluded_color", b.occluded_color},
             {"fill_visible_color", b.fill_visible_color},
             {"fill_occluded_color", b.fill_occluded_color}};
}

static void from_json(const json& j, visual_profile::player::box& b)
{
    read_field(j, "enabled", b.enabled);
    read_field(j, "style", b.style);
    read_field(j, "fill", b.fill);
    read_field(j, "outline", b.outline);
    read_field(j, "corner_length", b.corner_length);
    read_field(j, "visible_color", b.visible_color);
    read_field(j, "occluded_color", b.occluded_color);
    read_field(j, "fill_visible_color", b.fill_visible_color);
    read_field(j, "fill_occluded_color", b.fill_occluded_color);
}

static void to_json(json& j, const visual_profile::player::skeleton& s)
{
    j = json{{"enabled", s.enabled},
             {"thickness", s.thickness},
             {"visible_color", s.visible_color},
             {"occluded_color", s.occluded_color}};
}

static void from_json(const json& j, visual_profile::player::skeleton& s)
{
    read_field(j, "enabled", s.enabled);
    read_field(j, "thickness", s.thickness);
    read_field(j, "visible_color", s.visible_color);
    read_field(j, "occluded_color", s.occluded_color);
}

static void to_json(json& j, const visual_profile::player::hitboxes& h)
{
    j = json{{"enabled", h.enabled},
             {"visible_color", h.visible_color},
             {"occluded_color", h.occluded_color},
             {"fill", h.fill},
             {"outline", h.outline}};
}

static void from_json(const json& j, visual_profile::player::hitboxes& h)
{
    read_field(j, "enabled", h.enabled);
    read_field(j, "visible_color", h.visible_color);
    read_field(j, "occluded_color", h.occluded_color);
    read_field(j, "fill", h.fill);
    read_field(j, "outline", h.outline);
}

static void to_json(json& j, const visual_profile::player::threat_module& t)
{
    j = json{{"enabled", t.enabled},
             {"max_distance", t.max_distance},
             {"head_hitbox", t.head_hitbox},
             {"body_hitbox", t.body_hitbox},
             {"limb_hitbox", t.limb_hitbox},
             {"fill_alpha", t.fill_alpha},
             {"outline_alpha", t.outline_alpha},
             {"outline_thickness", t.outline_thickness},
             {"head_color", t.head_color},
             {"body_color", t.body_color},
             {"limb_color", t.limb_color}};
}

static void from_json(const json& j, visual_profile::player::threat_module& t)
{
    read_field(j, "enabled", t.enabled);
    read_field(j, "max_distance", t.max_distance);
    read_field(j, "head_hitbox", t.head_hitbox);
    read_field(j, "body_hitbox", t.body_hitbox);
    read_field(j, "limb_hitbox", t.limb_hitbox);
    read_field(j, "fill_alpha", t.fill_alpha);
    read_field(j, "outline_alpha", t.outline_alpha);
    read_field(j, "outline_thickness", t.outline_thickness);
    read_field(j, "head_color", t.head_color);
    read_field(j, "body_color", t.body_color);
    read_field(j, "limb_color", t.limb_color);
}

static void to_json(json& j, const visual_profile::player::health_bar& h)
{
    j = json{{"enabled", h.enabled},
             {"position", static_cast<int>(h.position)},
             {"thickness", h.thickness},
             {"outline", h.outline},
             {"outline_thickness", h.outline_thickness},
             {"gradient", h.gradient},
             {"show_value", h.show_value},
             {"segments", h.segments},
             {"segment_gap", h.segment_gap},
             {"full_color", h.full_color},
             {"low_color", h.low_color},
             {"background_color", h.background_color},
             {"outline_color", h.outline_color},
             {"text_color", h.text_color}};
}

static void from_json(const json& j, visual_profile::player::health_bar& h)
{
    read_field(j, "enabled", h.enabled);
    read_field(j, "position", h.position);
    read_field(j, "thickness", h.thickness);
    read_field(j, "outline", h.outline);
    read_field(j, "outline_thickness", h.outline_thickness);
    read_field(j, "gradient", h.gradient);
    read_field(j, "show_value", h.show_value);
    read_field(j, "segments", h.segments);
    read_field(j, "segment_gap", h.segment_gap);
    read_field(j, "full_color", h.full_color);
    read_field(j, "low_color", h.low_color);
    read_field(j, "background_color", h.background_color);
    read_field(j, "outline_color", h.outline_color);
    read_field(j, "text_color", h.text_color);
    h.thickness = std::clamp(h.thickness, 1.0f, 12.0f);
    h.outline_thickness = std::clamp(h.outline_thickness, 0.5f, 4.0f);
    h.segments = std::clamp(h.segments, 1, 10);
    h.segment_gap = std::clamp(h.segment_gap, 0.0f, 4.0f);
}

static void to_json(json& j, const combat_profile::legit_checks& value)
{
    j = json{{"airborne", value.airborne},
             {"flashed", value.flashed},
             {"smoke", value.smoke},
             {"flash_threshold", value.flash_threshold},
             {"flash_threshold_percent", value.flash_threshold},
             {"walls", value.walls}};
}

static void from_json(const json& j, combat_profile::legit_checks& value)
{
    read_field(j, "airborne", value.airborne);
    read_field(j, "flashed", value.flashed);
    read_field(j, "smoke", value.smoke);
    if (j.contains("flash_threshold_percent"))
        j.at("flash_threshold_percent").get_to(value.flash_threshold);
    else if (j.contains("flash_threshold"))
    {
        j.at("flash_threshold").get_to(value.flash_threshold);
        if (std::abs(value.flash_threshold - 20.0f) < 0.01f)
            value.flash_threshold = 85.0f;
        else
            value.flash_threshold *= 100.0f / 255.0f;
    }
    read_field(j, "walls", value.walls);
    value.flash_threshold = std::clamp(value.flash_threshold, 0.0f, 100.0f);
    value.walls = std::clamp(value.walls, static_cast<int>(combat_profile::wall_policy::block),
                             static_cast<int>(combat_profile::wall_policy::ignore));
    if (value.walls == combat_profile::wall_policy::ignore)
        value.walls = combat_profile::wall_policy::penetration;
}

static void to_json(json& j, const combat_profile::humanizer_settings& value)
{
    j = json{{"gravity", value.gravity},
             {"wind", value.wind},
             {"max_step", value.max_step},
             {"damping", value.damping},
             {"reaction_min_ms", value.reaction_min_ms},
             {"reaction_max_ms", value.reaction_max_ms},
             {"curve", value.curve},
             {"overshoot_chance", value.overshoot_chance},
             {"overshoot_amount", value.overshoot_amount},
             {"jitter", value.jitter},
             {"deadzone", value.deadzone}};
}

static void from_json(const json& j, combat_profile::humanizer_settings& value)
{
    read_field(j, "gravity", value.gravity);
    read_field(j, "wind", value.wind);
    read_field(j, "max_step", value.max_step);
    read_field(j, "damping", value.damping);
    read_field(j, "reaction_min_ms", value.reaction_min_ms);
    read_field(j, "reaction_max_ms", value.reaction_max_ms);
    read_field(j, "curve", value.curve);
    read_field(j, "overshoot_chance", value.overshoot_chance);
    read_field(j, "overshoot_amount", value.overshoot_amount);
    read_field(j, "jitter", value.jitter);
    read_field(j, "deadzone", value.deadzone);
    value.gravity = std::clamp(value.gravity, 0.0f, 20.0f);
    value.wind = std::clamp(value.wind, 0.0f, 20.0f);
    value.max_step = std::clamp(value.max_step, 1.0f, 90.0f);
    value.damping = std::clamp(value.damping, 0.0f, 1.0f);
    value.reaction_min_ms = std::clamp(value.reaction_min_ms, 0, 500);
    value.reaction_max_ms = std::clamp(value.reaction_max_ms, value.reaction_min_ms, 750);
    value.curve = std::clamp(value.curve, 0.0f, 1.0f);
    value.overshoot_chance = std::clamp(value.overshoot_chance, 0.0f, 100.0f);
    value.overshoot_amount = std::clamp(value.overshoot_amount, 0.0f, 1.0f);
    value.jitter = std::clamp(value.jitter, 0.0f, 3.0f);
    value.deadzone = std::clamp(value.deadzone, 0.0f, 2.0f);
}

static void to_json(json& j, const combat_profile::multipoint_settings& value)
{
    j = json{{"caps", value.caps},
             {"sides", value.sides},
             {"head_scale", value.head_scale},
             {"body_scale", value.body_scale},
             {"limb_scale", value.limb_scale}};
}

static void from_json(const json& j, combat_profile::multipoint_settings& value)
{
    read_field(j, "caps", value.caps);
    read_field(j, "sides", value.sides);
    read_field(j, "head_scale", value.head_scale);
    read_field(j, "body_scale", value.body_scale);
    read_field(j, "limb_scale", value.limb_scale);
    value.head_scale = std::clamp(value.head_scale, 0.0f, 0.95f);
    value.body_scale = std::clamp(value.body_scale, 0.0f, 0.95f);
    value.limb_scale = std::clamp(value.limb_scale, 0.0f, 0.95f);
}

static void to_json(json& j, const combat_profile::prediction_settings& value)
{
    j = json{
        {"enabled", value.enabled}, {"max_horizon_ms", value.max_horizon_ms}, {"acceleration", value.acceleration}};
}

static void from_json(const json& j, combat_profile::prediction_settings& value)
{
    read_field(j, "enabled", value.enabled);
    read_field(j, "max_horizon_ms", value.max_horizon_ms);
    read_field(j, "acceleration", value.acceleration);
    value.max_horizon_ms = std::clamp(value.max_horizon_ms, 0.0f, 120.0f);
}

static void to_json(json& j, const combat_profile::rcs_settings& value)
{
    j = json{{"enabled", value.enabled},
             {"start_bullet", value.start_bullet},
             {"pitch", value.pitch},
             {"yaw", value.yaw},
             {"response_ms", value.response_ms},
             {"smoothness", value.smoothness},
             {"randomness", value.randomness},
             {"drift", value.drift}};
}

static void from_json(const json& j, combat_profile::rcs_settings& value)
{
    read_field(j, "enabled", value.enabled);
    read_field(j, "start_bullet", value.start_bullet);
    read_field(j, "pitch", value.pitch);
    read_field(j, "yaw", value.yaw);
    read_field(j, "response_ms", value.response_ms);
    read_field(j, "smoothness", value.smoothness);
    read_field(j, "randomness", value.randomness);
    read_field(j, "drift", value.drift);
    value.start_bullet = std::clamp(value.start_bullet, 1, 10);
    value.pitch = std::clamp(value.pitch, 0.0f, 200.0f);
    value.yaw = std::clamp(value.yaw, 0.0f, 200.0f);
    value.response_ms = std::clamp(value.response_ms, 1.0f, 150.0f);
    value.smoothness = std::clamp(value.smoothness, 0.0f, 100.0f);
    value.randomness = std::clamp(value.randomness, 0.0f, 30.0f);
    value.drift = std::clamp(value.drift, 0.0f, 30.0f);
}

static void to_json(json& j, const combat_profile::fov_settings& value)
{
    j = json{{"selection", value.selection},
             {"visualization", value.visualization},
             {"draw_target_point", value.selection == combat_profile::fov_settings::target_distance},
             {"near_distance_m", value.near_distance_m},
             {"near_fov", value.near_fov},
             {"far_distance_m", value.far_distance_m},
             {"far_fov", value.far_fov},
             {"distance_curve", value.distance_curve},
             {"target_near_distance_m", value.target_near_distance_m},
             {"target_near_fov", value.target_near_fov},
             {"target_far_distance_m", value.target_far_distance_m},
             {"target_far_fov", value.target_far_fov},
             {"target_distance_curve", value.target_distance_curve}};
}

static void from_json(const json& j, combat_profile::fov_settings& value)
{
    const auto has_distance_curve = j.contains("distance_curve");
    const auto has_target_profile = j.contains("target_near_fov");
    read_field(j, "selection", value.selection);
    read_field(j, "visualization", value.visualization);
    bool legacy_target = value.visualization == combat_profile::fov_settings::target;
    read_field(j, "draw_target_point", legacy_target);
    if (legacy_target)
        value.selection = combat_profile::fov_settings::target_distance;
    read_field(j, "near_distance_m", value.near_distance_m);
    read_field(j, "near_fov", value.near_fov);
    read_field(j, "far_distance_m", value.far_distance_m);
    read_field(j, "far_fov", value.far_fov);
    read_field(j, "distance_curve", value.distance_curve);
    read_field(j, "target_near_distance_m", value.target_near_distance_m);
    read_field(j, "target_near_fov", value.target_near_fov);
    read_field(j, "target_far_distance_m", value.target_far_distance_m);
    read_field(j, "target_far_fov", value.target_far_fov);
    read_field(j, "target_distance_curve", value.target_distance_curve);
    if (!has_target_profile && value.selection == combat_profile::fov_settings::target_distance)
    {
        value.target_near_distance_m = value.near_distance_m;
        value.target_near_fov = value.near_fov;
        value.target_far_distance_m = value.far_distance_m;
        value.target_far_fov = value.far_fov;
        value.target_distance_curve = value.distance_curve;
        value.near_distance_m = 2.5f;
        value.near_fov = 2.0f;
        value.far_distance_m = 10.0f;
        value.far_fov = 2.0f;
        value.distance_curve = 1.0f;
    }

    if (!has_distance_curve && value.selection != combat_profile::fov_settings::fixed && value.near_fov >= 40.0f)
    {
        if (value.selection == combat_profile::fov_settings::target_distance)
        {
            value.target_near_distance_m = 2.5f;
            value.target_near_fov = 20.0f;
            value.target_far_distance_m = 10.0f;
            value.target_far_fov = 2.0f;
            value.target_distance_curve = 1.0f;
        }
        else
        {
            value.near_distance_m = 2.5f;
            value.near_fov = 2.0f;
            value.far_distance_m = 10.0f;
            value.far_fov = 2.0f;
            value.distance_curve = 1.0f;
        }
    }
    value.selection = std::clamp(value.selection, static_cast<int>(combat_profile::fov_settings::fixed),
                                 static_cast<int>(combat_profile::fov_settings::target_distance));
    value.visualization = std::clamp(value.visualization, static_cast<int>(combat_profile::fov_settings::center),
                                     static_cast<int>(combat_profile::fov_settings::target));
    value.near_distance_m = std::clamp(value.near_distance_m, 0.5f, 100.0f);
    value.far_distance_m = std::clamp(value.far_distance_m, value.near_distance_m + 0.5f, 150.0f);
    value.near_fov = std::clamp(value.near_fov, 0.25f, 180.0f);
    value.far_fov = std::clamp(value.far_fov, 0.25f, value.near_fov);
    value.distance_curve = std::clamp(value.distance_curve, 0.25f, 4.0f);
    value.target_near_distance_m = std::clamp(value.target_near_distance_m, 0.5f, 100.0f);
    value.target_far_distance_m = std::clamp(value.target_far_distance_m, value.target_near_distance_m + 0.5f, 150.0f);
    value.target_near_fov = std::clamp(value.target_near_fov, 0.25f, 180.0f);
    value.target_far_fov = std::clamp(value.target_far_fov, 0.25f, value.target_near_fov);
    value.target_distance_curve = std::clamp(value.target_distance_curve, 0.25f, 4.0f);
}

static void to_json(json& j, const visual_profile::player::armor_bar& a)
{
    j = json{{"enabled", a.enabled},
             {"position", static_cast<int>(a.position)},
             {"thickness", a.thickness},
             {"outline", a.outline},
             {"outline_thickness", a.outline_thickness},
             {"gradient", a.gradient},
             {"show_value", a.show_value},
             {"segments", a.segments},
             {"segment_gap", a.segment_gap},
             {"full_color", a.full_color},
             {"low_color", a.low_color},
             {"background_color", a.background_color},
             {"outline_color", a.outline_color},
             {"text_color", a.text_color}};
}

static void from_json(const json& j, visual_profile::player::armor_bar& a)
{
    read_field(j, "enabled", a.enabled);
    read_field(j, "position", a.position);
    read_field(j, "thickness", a.thickness);
    read_field(j, "outline", a.outline);
    read_field(j, "outline_thickness", a.outline_thickness);
    read_field(j, "gradient", a.gradient);
    read_field(j, "show_value", a.show_value);
    read_field(j, "segments", a.segments);
    read_field(j, "segment_gap", a.segment_gap);
    read_field(j, "full_color", a.full_color);
    read_field(j, "low_color", a.low_color);
    read_field(j, "background_color", a.background_color);
    read_field(j, "outline_color", a.outline_color);
    read_field(j, "text_color", a.text_color);
    a.thickness = std::clamp(a.thickness, 1.0f, 12.0f);
    a.outline_thickness = std::clamp(a.outline_thickness, 0.5f, 4.0f);
    a.segments = std::clamp(a.segments, 1, 10);
    a.segment_gap = std::clamp(a.segment_gap, 0.0f, 4.0f);
}

static void to_json(json& j, const visual_profile::player::info_flags::style& s)
{
    j = json{{"color", s.color}, {"scale", s.scale}};
}

static void from_json(const json& j, visual_profile::player::info_flags::style& s)
{
    read_field(j, "color", s.color);
    read_field(j, "scale", s.scale);
    s.scale = std::clamp(s.scale, 0.55f, 2.0f);
}

static void to_json(json& j, const visual_profile::player::info_flags& f)
{
    j = json{{"enabled", f.enabled},
             {"flags", f.flags},
             {"money", f.money_style},
             {"armor", f.armor_style},
             {"kit", f.kit_style},
             {"scoped", f.scoped_style},
             {"defusing", f.defusing_style},
             {"flashed", f.flashed_style},
             {"ping", f.ping_style},
             {"distance", f.distance_style},
             {"bomb_damage", f.bomb_damage_style}};
}

static void from_json(const json& j, visual_profile::player::info_flags& f)
{
    read_field(j, "enabled", f.enabled);
    read_field(j, "flags", f.flags);
    const auto load_style =
        [&](const char* name, const char* legacy_color, visual_profile::player::info_flags::style& style)
    {
        if (j.contains(name))
            j.at(name).get_to(style);
        else if (j.contains(legacy_color))
            j.at(legacy_color).get_to(style.color);
    };
    load_style("money", "money_color", f.money_style);
    load_style("armor", "armor_color", f.armor_style);
    load_style("kit", "kit_color", f.kit_style);
    load_style("scoped", "scoped_color", f.scoped_style);
    load_style("defusing", "defusing_color", f.defusing_style);
    load_style("flashed", "flashed_color", f.flashed_style);
    load_style("ping", "ping_color", f.ping_style);
    load_style("distance", "distance_color", f.distance_style);
    load_style("bomb_damage", "bomb_damage_color", f.bomb_damage_style);
}

static void to_json(json& j, const visual_profile::player::name& n)
{
    j = json{{"enabled", n.enabled}, {"color", n.color}};
}

static void from_json(const json& j, visual_profile::player::name& n)
{
    read_field(j, "enabled", n.enabled);
    read_field(j, "color", n.color);
}

static void to_json(json& j, const visual_profile::player::weapon& w)
{
    j = json{{"enabled", w.enabled},
             {"display", static_cast<int>(w.display)},
             {"text_color", w.text_color},
             {"icon_color", w.icon_color},
             {"ammo",
              {{"enabled", w.ammo.enabled}, {"show_count", w.ammo.show_count}, {"empty_color", w.ammo.empty_color}}}};
}

static void from_json(const json& j, visual_profile::player::weapon& w)
{
    read_field(j, "enabled", w.enabled);
    read_field(j, "display", w.display);
    read_field(j, "text_color", w.text_color);
    read_field(j, "icon_color", w.icon_color);
    if (j.contains("ammo"))
    {
        const auto& ammo = j.at("ammo");
        read_field(ammo, "enabled", w.ammo.enabled);
        read_field(ammo, "show_count", w.ammo.show_count);
        read_field(ammo, "empty_color", w.ammo.empty_color);
    }
}

static void to_json(json& j, const visual_profile::player::head_circle& h)
{
    j = json{{"enabled", h.enabled}, {"thickness", h.thickness}, {"color", h.color}};
}

static void from_json(const json& j, visual_profile::player::head_circle& h)
{
    read_field(j, "enabled", h.enabled);
    read_field(j, "thickness", h.thickness);
    read_field(j, "color", h.color);
}

static void to_json(json& j, const visual_profile::player::view_line& v)
{
    j = json{{"enabled", v.enabled}, {"length", v.length}, {"thickness", v.thickness}, {"color", v.color}};
}

static void from_json(const json& j, visual_profile::player::view_line& v)
{
    read_field(j, "enabled", v.enabled);
    read_field(j, "length", v.length);
    read_field(j, "thickness", v.thickness);
    read_field(j, "color", v.color);
}

static void to_json(json& j, const visual_profile::player::offscreen_arrows& a)
{
    j = json{{"enabled", a.enabled},
             {"size", a.size},
             {"radius", a.radius},
             {"thickness", a.thickness},
             {"color", a.color},
             {"bloom", a.bloom},
             {"bloom_color", a.bloom_color},
             {"bloom_radius", a.bloom_radius},
             {"bloom_speed", a.bloom_speed},
             {"bloom_min_alpha", a.bloom_min_alpha},
             {"bloom_max_alpha", a.bloom_max_alpha}};
}

static void from_json(const json& j, visual_profile::player::offscreen_arrows& a)
{
    read_field(j, "enabled", a.enabled);
    read_field(j, "size", a.size);
    read_field(j, "radius", a.radius);
    read_field(j, "thickness", a.thickness);
    read_field(j, "color", a.color);
    if (j.contains("bloom"))
        j.at("bloom").get_to(a.bloom);
    else if (j.contains("pulse"))
        j.at("pulse").get_to(a.bloom);
    read_field(j, "bloom_color", a.bloom_color);
    read_field(j, "bloom_radius", a.bloom_radius);
    if (j.contains("bloom_speed"))
        j.at("bloom_speed").get_to(a.bloom_speed);
    else if (j.contains("pulse_speed"))
    {
        float legacy_speed{};
        j.at("pulse_speed").get_to(legacy_speed);
        a.bloom_speed = legacy_speed * 0.4f;
    }
    read_field(j, "bloom_min_alpha", a.bloom_min_alpha);
    read_field(j, "bloom_max_alpha", a.bloom_max_alpha);
    a.bloom_radius = std::clamp(a.bloom_radius, 1.0f, 16.0f);
    a.bloom_speed = std::clamp(a.bloom_speed, 0.05f, 2.0f);
    a.bloom_min_alpha = std::clamp(a.bloom_min_alpha, 0.0f, 1.0f);
    a.bloom_max_alpha = std::clamp(a.bloom_max_alpha, a.bloom_min_alpha, 1.0f);
}

static void to_json(json& j, const visual_profile::player::layout_element& e)
{
    j = json{{"x", e.x}, {"y", e.y}, {"scale", e.scale}};
}

static void from_json(const json& j, visual_profile::player::layout_element& e)
{
    read_field(j, "x", e.x);
    read_field(j, "y", e.y);
    read_field(j, "scale", e.scale);
    e.x = std::clamp(e.x, -0.5f, 1.5f);
    e.y = std::clamp(e.y, -0.5f, 1.5f);
    e.scale = std::clamp(e.scale, 0.55f, 2.0f);
}

static void to_json(json& j, const visual_profile::player::editor_layout& l)
{
    j = json{{"name", l.name}, {"weapon", l.weapon}, {"health", l.health}, {"armor", l.armor}, {"flags", l.flags}};
}

static void from_json(const json& j, visual_profile::player::editor_layout& l)
{
    read_field(j, "name", l.name);

    if (std::abs(l.name.x - 0.50f) < 0.0001f && std::abs(l.name.y - (-0.07f)) < 0.0001f &&
        std::abs(l.name.scale - 1.0f) < 0.0001f)
    {
        l.name.y = -0.025f;
    }
    read_field(j, "weapon", l.weapon);
    read_field(j, "health", l.health);

    if (j.contains("ammo"))
        j.at("ammo").get_to(l.armor);
    else if (j.contains("armor"))
        j.at("armor").get_to(l.armor);
    read_field(j, "flags", l.flags);
}

static void to_json(json& j, const visual_profile::player& p)
{
    j = json{{"enabled", p.enabled},
             {"activation_mode", p.activation_mode},
             {"activation_key", p.activation_key},
             {"spectator_sync", p.spectator_sync},
             {"m_legit_sync", json{{"enabled", p.m_legit_sync.enabled},
                                   {"direct_visible", p.m_legit_sync.direct_visible},
                                   {"radar", p.m_legit_sync.radar},
                                   {"sound", p.m_legit_sync.sound},
                                   {"radar_hold", p.m_legit_sync.radar_hold},
                                   {"sound_hold", p.m_legit_sync.sound_hold},
                                   {"sound_distance", p.m_legit_sync.sound_distance},
                                   {"pulse_min_opacity", p.m_legit_sync.pulse_min_opacity},
                                   {"pulse_max_opacity", p.m_legit_sync.pulse_max_opacity},
                                   {"pulse_period", p.m_legit_sync.pulse_period}}},
             {"m_layout", p.m_layout},
             {"m_box", p.m_box},
             {"m_skeleton", p.m_skeleton},
             {"m_head_circle", p.m_head_circle},
             {"m_view_line", p.m_view_line},
             {"m_offscreen_arrows", p.m_offscreen_arrows},
             {"m_hitboxes", p.m_hitboxes},
             {"m_threat_module", p.m_threat_module},
             {"m_health_bar", p.m_health_bar},
             {"m_armor_bar", p.m_armor_bar},
             {"m_info_flags", p.m_info_flags},
             {"m_name", p.m_name},
             {"m_weapon", p.m_weapon}};
}

static void from_json(const json& j, visual_profile::player& p)
{
    read_field(j, "enabled", p.enabled);
    read_field(j, "activation_mode", p.activation_mode);
    read_field(j, "activation_key", p.activation_key);
    p.activation_mode = std::clamp(p.activation_mode, static_cast<int>(visual_profile::player::always_on),
                                   static_cast<int>(visual_profile::player::toggle));
    p.activation_key = std::clamp(p.activation_key, 0, 255);
    read_field(j, "spectator_sync", p.spectator_sync);
    if (j.contains("m_legit_sync"))
    {
        const auto& l = j.at("m_legit_sync");
        read_field(l, "enabled", p.m_legit_sync.enabled);
        read_field(l, "direct_visible", p.m_legit_sync.direct_visible);
        read_field(l, "radar", p.m_legit_sync.radar);
        read_field(l, "sound", p.m_legit_sync.sound);
        // Backward compatibility for configs written while Spectator Sync lived
        // inside Legit Sync.
        if (!j.contains("spectator_sync") && l.contains("spectator_sync"))
            l.at("spectator_sync").get_to(p.spectator_sync);
        read_field(l, "radar_hold", p.m_legit_sync.radar_hold);
        read_field(l, "sound_hold", p.m_legit_sync.sound_hold);
        read_field(l, "sound_distance", p.m_legit_sync.sound_distance);
        read_field(l, "pulse_min_opacity", p.m_legit_sync.pulse_min_opacity);
        read_field(l, "pulse_max_opacity", p.m_legit_sync.pulse_max_opacity);
        read_field(l, "pulse_period", p.m_legit_sync.pulse_period);
    }
    p.m_legit_sync.radar_hold = std::clamp(p.m_legit_sync.radar_hold, 0.1f, 10.0f);
    p.m_legit_sync.sound_hold = std::clamp(p.m_legit_sync.sound_hold, 0.1f, 10.0f);
    p.m_legit_sync.sound_distance = std::clamp(p.m_legit_sync.sound_distance, 100.0f, 2500.0f);
    p.m_legit_sync.pulse_min_opacity = std::clamp(p.m_legit_sync.pulse_min_opacity, 0.0f, 1.0f);
    p.m_legit_sync.pulse_max_opacity =
        std::clamp(p.m_legit_sync.pulse_max_opacity, p.m_legit_sync.pulse_min_opacity, 1.0f);
    p.m_legit_sync.pulse_period = std::clamp(p.m_legit_sync.pulse_period, 0.4f, 5.0f);
    read_field(j, "m_layout", p.m_layout);
    read_field(j, "m_box", p.m_box);
    read_field(j, "m_skeleton", p.m_skeleton);
    read_field(j, "m_head_circle", p.m_head_circle);
    read_field(j, "m_view_line", p.m_view_line);
    read_field(j, "m_offscreen_arrows", p.m_offscreen_arrows);
    read_field(j, "m_hitboxes", p.m_hitboxes);
    read_field(j, "m_threat_module", p.m_threat_module);
    read_field(j, "m_health_bar", p.m_health_bar);
    if (j.contains("m_ammo_bar"))
        j.at("m_ammo_bar").get_to(p.m_armor_bar);
    else if (j.contains("m_armor_bar"))
        j.at("m_armor_bar").get_to(p.m_armor_bar);
    read_field(j, "m_info_flags", p.m_info_flags);
    read_field(j, "m_name", p.m_name);
    read_field(j, "m_weapon", p.m_weapon);

    if (j.contains("m_bomb_damage") && (!j.contains("m_info_flags") || !j.at("m_info_flags").contains("bomb_damage")))
    {
        const auto& legacy = j.at("m_bomb_damage");
        bool enabled{true};
        read_field(legacy, "enabled", enabled);
        if (enabled)
            p.m_info_flags.flags |= visual_profile::player::info_flags::flag::bomb_damage;
        else
            p.m_info_flags.flags &= ~visual_profile::player::info_flags::flag::bomb_damage;
        read_field(legacy, "color", p.m_info_flags.bomb_damage_style.color);
    }
}

static void to_json(json& j, const visual_profile::item::icon& i)
{
    j = json{{"enabled", i.enabled}, {"color", i.color}};
}

static void from_json(const json& j, visual_profile::item::icon& i)
{
    read_field(j, "enabled", i.enabled);
    read_field(j, "color", i.color);
}

static void to_json(json& j, const visual_profile::item::name& n)
{
    j = json{{"enabled", n.enabled}, {"color", n.color}};
}

static void from_json(const json& j, visual_profile::item::name& n)
{
    read_field(j, "enabled", n.enabled);
    read_field(j, "color", n.color);
}

static void to_json(json& j, const visual_profile::item::ammo& a)
{
    j = json{{"enabled", a.enabled}, {"color", a.color}, {"empty_color", a.empty_color}};
}

static void from_json(const json& j, visual_profile::item::ammo& a)
{
    read_field(j, "enabled", a.enabled);
    read_field(j, "color", a.color);
    read_field(j, "empty_color", a.empty_color);
}

static void to_json(json& j, const visual_profile::item::filters& f)
{
    j = json{{"rifles", f.rifles},   {"smgs", f.smgs},   {"shotguns", f.shotguns}, {"snipers", f.snipers},
             {"pistols", f.pistols}, {"heavy", f.heavy}, {"grenades", f.grenades}, {"utility", f.utility}};
}

static void from_json(const json& j, visual_profile::item::filters& f)
{
    read_field(j, "rifles", f.rifles);
    read_field(j, "smgs", f.smgs);
    read_field(j, "shotguns", f.shotguns);
    read_field(j, "snipers", f.snipers);
    read_field(j, "pistols", f.pistols);
    read_field(j, "heavy", f.heavy);
    read_field(j, "grenades", f.grenades);
    read_field(j, "utility", f.utility);
}

static void to_json(json& j, const visual_profile::item& i)
{
    j = json{{"enabled", i.enabled}, {"max_distance", i.max_distance}, {"m_icon", i.m_icon}, {"m_name", i.m_name},
             {"m_ammo", i.m_ammo},   {"m_filters", i.m_filters}};
}

static void from_json(const json& j, visual_profile::item& i)
{
    read_field(j, "enabled", i.enabled);
    read_field(j, "max_distance", i.max_distance);
    read_field(j, "m_icon", i.m_icon);
    read_field(j, "m_name", i.m_name);
    read_field(j, "m_ammo", i.m_ammo);
    read_field(j, "m_filters", i.m_filters);
}

static void to_json(json& j, const visual_profile::projectile& p)
{
    j = json{{"enabled", p.enabled},
             {"display_mode", p.display_mode},
             {"show_icon", p.show_icon},
             {"show_timer_ring", p.show_timer_ring},
             {"show_inferno_bounds", p.show_inferno_bounds},
             {"default_color", p.default_color},
             {"color_he", p.color_he},
             {"color_flash", p.color_flash},
             {"color_smoke", p.color_smoke},
             {"color_molotov", p.color_molotov},
             {"color_decoy", p.color_decoy},
             {"timer_high_color", p.timer_high_color},
             {"timer_low_color", p.timer_low_color},
             {"indicator_background", p.indicator_background},
             {"inferno_gradient_width", p.inferno_gradient_width},
             {"inferno_gradient_opacity", p.inferno_gradient_opacity}};
}

static void from_json(const json& j, visual_profile::projectile& p)
{
    read_field(j, "enabled", p.enabled);
    read_field(j, "display_mode", p.display_mode);
    p.display_mode =
        std::clamp(p.display_mode, visual_profile::projectile::indicator, visual_profile::projectile::text_only);
    read_field(j, "show_icon", p.show_icon);
    if (j.contains("show_timer_ring"))
        j.at("show_timer_ring").get_to(p.show_timer_ring);
    else if (j.contains("show_timer_bar"))
        j.at("show_timer_bar").get_to(p.show_timer_ring);
    read_field(j, "show_inferno_bounds", p.show_inferno_bounds);
    read_field(j, "default_color", p.default_color);
    read_field(j, "color_he", p.color_he);
    read_field(j, "color_flash", p.color_flash);
    read_field(j, "color_smoke", p.color_smoke);
    read_field(j, "color_molotov", p.color_molotov);
    read_field(j, "color_decoy", p.color_decoy);
    read_field(j, "timer_high_color", p.timer_high_color);
    read_field(j, "timer_low_color", p.timer_low_color);
    if (j.contains("indicator_background"))
        j.at("indicator_background").get_to(p.indicator_background);
    else if (j.contains("bar_background"))
        j.at("bar_background").get_to(p.indicator_background);
    read_field(j, "inferno_gradient_width", p.inferno_gradient_width);
    read_field(j, "inferno_gradient_opacity", p.inferno_gradient_opacity);
}

static void to_json(json& j, const visual_profile::bomb& b)
{
    j = json{{"enabled", b.enabled},
             {"display_mode", b.display_mode},
             {"show_active_bomb", b.show_active_bomb},
             {"active_bomb_color", b.active_bomb_color},
             {"show_planted_bomb", b.show_planted_bomb},
             {"bomb_color_t", b.bomb_color_t},
             {"bomb_color_ct", b.bomb_color_ct},
             {"show_timer", b.show_timer},
             {"timer_text_color", b.timer_text_color},
             {"show_info_panel", b.show_info_panel},
             {"panel_background", b.panel_background},
             {"show_safe_zone", b.show_safe_zone},
             {"safe_zone_color", b.safe_zone_color},
             {"safe_zone_bands", b.safe_zone_bands},
             {"safe_zone_band_step", b.safe_zone_band_step},
             {"safe_zone_draw_radius", b.safe_zone_draw_radius}};
}

static void from_json(const json& j, visual_profile::bomb& b)
{
    read_field(j, "enabled", b.enabled);
    read_field(j, "display_mode", b.display_mode);
    b.display_mode = std::clamp(b.display_mode, visual_profile::bomb::indicator, visual_profile::bomb::text_only);
    read_field(j, "show_active_bomb", b.show_active_bomb);
    read_field(j, "active_bomb_color", b.active_bomb_color);
    read_field(j, "show_planted_bomb", b.show_planted_bomb);
    read_field(j, "bomb_color_t", b.bomb_color_t);
    read_field(j, "bomb_color_ct", b.bomb_color_ct);
    read_field(j, "show_timer", b.show_timer);
    read_field(j, "timer_text_color", b.timer_text_color);
    read_field(j, "show_info_panel", b.show_info_panel);
    read_field(j, "panel_background", b.panel_background);
    read_field(j, "show_safe_zone", b.show_safe_zone);
    read_field(j, "safe_zone_color", b.safe_zone_color);
    read_field(j, "safe_zone_bands", b.safe_zone_bands);
    read_field(j, "safe_zone_band_step", b.safe_zone_band_step);
    read_field(j, "safe_zone_draw_radius", b.safe_zone_draw_radius);
    b.safe_zone_draw_radius = std::clamp(b.safe_zone_draw_radius, 200.0f, 2500.0f);
    // Migrate the former default that filled the floor with four widely spaced
    // 14-HP contours. Explicit custom combinations remain untouched.
    if (b.safe_zone_bands == 4 && std::abs(b.safe_zone_band_step - 14.0f) < 0.01f)
    {
        b.safe_zone_bands = 1;
        b.safe_zone_band_step = 4.0f;
        if (b.safe_zone_color.r == 120 && b.safe_zone_color.g == 230 && b.safe_zone_color.b == 140 &&
            b.safe_zone_color.a == 230)
        {
            b.safe_zone_color = {70, 235, 105, 240};
        }
    }
}

static void to_json(json& j, const visual_profile::radar& r)
{
    j = json{{"enabled", r.enabled},
             {"activation_mode", r.activation_mode},
             {"activation_key", r.activation_key},
             {"show_names", r.show_names},
             {"show_health", r.show_health},
             {"show_armor", r.show_armor},
             {"show_weapon", r.show_weapon},
             {"show_projectiles", r.show_projectiles},
             {"show_trajectories", r.show_trajectories},
             {"show_grenade_zones", r.show_grenade_zones},
             {"information_scale", r.information_scale},
             {"marker_scale", r.marker_scale},
             {"text_outline", r.text_outline},
             {"text_outline_thickness", r.text_outline_thickness},
             {"text_outline_color", r.text_outline_color},
             {"enemy_color", r.enemy_color},
             {"direction_color", r.direction_color},
             {"name_color", r.name_color},
             {"weapon_color", r.weapon_color},
             {"status_color", r.status_color},
             {"health_color", r.health_color},
             {"armor_color", r.armor_color},
             {"he_color", r.he_color},
             {"flash_color", r.flash_color},
             {"smoke_color", r.smoke_color},
             {"molotov_color", r.molotov_color},
             {"decoy_color", r.decoy_color},
             {"trajectory_thickness", r.trajectory_thickness},
             {"trajectory_endpoint_size", r.trajectory_endpoint_size},
             {"zone_fill_alpha", r.zone_fill_alpha},
             {"zone_outline_alpha", r.zone_outline_alpha},
             {"zone_outline_thickness", r.zone_outline_thickness}};
}

static void from_json(const json& j, visual_profile::radar& r)
{
    read_field(j, "enabled", r.enabled);
    read_field(j, "activation_mode", r.activation_mode);
    read_field(j, "activation_key", r.activation_key);
    r.activation_mode = std::clamp(r.activation_mode, static_cast<int>(visual_profile::radar::always_on),
                                   static_cast<int>(visual_profile::radar::toggle));
    r.activation_key = std::clamp(r.activation_key, 0, 255);
    read_field(j, "show_names", r.show_names);
    read_field(j, "show_health", r.show_health);
    read_field(j, "show_armor", r.show_armor);
    read_field(j, "show_weapon", r.show_weapon);
    read_field(j, "show_projectiles", r.show_projectiles);
    read_field(j, "show_trajectories", r.show_trajectories);
    read_field(j, "show_grenade_zones", r.show_grenade_zones);
    read_field(j, "information_scale", r.information_scale);
    read_field(j, "marker_scale", r.marker_scale);
    read_field(j, "text_outline", r.text_outline);
    read_field(j, "text_outline_thickness", r.text_outline_thickness);
    read_field(j, "text_outline_color", r.text_outline_color);
    read_field(j, "enemy_color", r.enemy_color);
    read_field(j, "direction_color", r.direction_color);
    read_field(j, "name_color", r.name_color);
    read_field(j, "weapon_color", r.weapon_color);
    read_field(j, "status_color", r.status_color);
    read_field(j, "health_color", r.health_color);
    read_field(j, "armor_color", r.armor_color);
    read_field(j, "he_color", r.he_color);
    read_field(j, "flash_color", r.flash_color);
    read_field(j, "smoke_color", r.smoke_color);
    read_field(j, "molotov_color", r.molotov_color);
    read_field(j, "decoy_color", r.decoy_color);
    read_field(j, "trajectory_thickness", r.trajectory_thickness);
    read_field(j, "trajectory_endpoint_size", r.trajectory_endpoint_size);
    read_field(j, "zone_fill_alpha", r.zone_fill_alpha);
    read_field(j, "zone_outline_alpha", r.zone_outline_alpha);
    read_field(j, "zone_outline_thickness", r.zone_outline_thickness);
    r.information_scale = std::isfinite(r.information_scale) ? std::clamp(r.information_scale, 0.5f, 1.5f) : 0.75f;
    const auto finite = [](const float value, const float low, const float high, const float fallback)
    { return std::isfinite(value) ? std::clamp(value, low, high) : fallback; };
    r.marker_scale = finite(r.marker_scale, 0.5f, 2.0f, 1.0f);
    r.text_outline_thickness = finite(r.text_outline_thickness, 0.5f, 3.0f, 1.0f);
    r.trajectory_thickness = finite(r.trajectory_thickness, 0.5f, 6.0f, 1.5f);
    r.trajectory_endpoint_size = finite(r.trajectory_endpoint_size, 1.0f, 10.0f, 3.0f);
    r.zone_fill_alpha = finite(r.zone_fill_alpha, 0.0f, 100.0f, 15.0f);
    r.zone_outline_alpha = finite(r.zone_outline_alpha, 0.0f, 100.0f, 59.0f);
    r.zone_outline_thickness = finite(r.zone_outline_thickness, 0.5f, 5.0f, 1.0f);
}

static void to_json(json& j, const visual_profile::no_flash& n)
{
    j = json{{"enabled", n.enabled},
             {"max_distance", n.max_distance},
             {"background_color", n.background_color},
             {"wireframe_color", n.wireframe_color}};
}

static void from_json(const json& j, visual_profile::no_flash& n)
{
    read_field(j, "enabled", n.enabled);
    read_field(j, "max_distance", n.max_distance);
    read_field(j, "background_color", n.background_color);
    read_field(j, "wireframe_color", n.wireframe_color);
}

static void to_json(json& j, const visual_profile::no_smoke& n)
{
    j = json{{"enabled", n.enabled}, {"wireframe_color", n.wireframe_color}};
}

static void from_json(const json& j, visual_profile::no_smoke& n)
{
    read_field(j, "enabled", n.enabled);
    read_field(j, "wireframe_color", n.wireframe_color);
}

static void to_json(json& j, const visual_profile::crosshair& c)
{
    j = json{{"enabled", c.enabled},
             {"dot", c.dot},
             {"lines", c.lines},
             {"t_style", c.t_style},
             {"length", c.length},
             {"thickness", c.thickness},
             {"gap", c.gap},
             {"color", c.color},
             {"outline_color", c.outline_color},
             {"outline", c.outline},
             {"outline_thickness", c.outline_thickness},
             {"penetration_enabled", c.penetration_enabled},
             {"penetration_color_yes", c.penetration_color_yes},
             {"penetration_color_no", c.penetration_color_no},
             {"penetration_min_damage", c.penetration_min_damage}};
}

static void from_json(const json& j, visual_profile::crosshair& c)
{
    read_field(j, "enabled", c.enabled);
    read_field(j, "dot", c.dot);
    read_field(j, "lines", c.lines);
    read_field(j, "t_style", c.t_style);
    read_field(j, "length", c.length);
    read_field(j, "thickness", c.thickness);
    read_field(j, "gap", c.gap);
    read_field(j, "color", c.color);
    read_field(j, "outline_color", c.outline_color);
    read_field(j, "outline", c.outline);
    read_field(j, "outline_thickness", c.outline_thickness);
    c.outline_thickness = std::isfinite(c.outline_thickness) ? std::clamp(c.outline_thickness, 0.5f, 3.0f) : 1.0f;
    read_field(j, "penetration_enabled", c.penetration_enabled);
    read_field(j, "penetration_color_yes", c.penetration_color_yes);
    read_field(j, "penetration_color_no", c.penetration_color_no);
    read_field(j, "penetration_min_damage", c.penetration_min_damage);
}

static void to_json(json& j, const visual_profile::chams::material& m)
{
    j = json{{"enabled", m.enabled},   {"type", m.type},           {"wireframe", m.wireframe},
             {"color", m.color},       {"roughness", m.roughness}, {"metalness", m.metalness},
             {"exponent", m.exponent}, {"falloff", m.falloff},     {"fresnel_fill", m.fresnel_fill},
             {"strength", m.strength}, {"speed", m.speed},         {"tint", m.tint}};
}

static void from_json(const json& j, visual_profile::chams::material& m)
{
    read_field(j, "enabled", m.enabled);
    read_field(j, "type", m.type);
    read_field(j, "wireframe", m.wireframe);
    read_field(j, "color", m.color);
    read_field(j, "roughness", m.roughness);
    read_field(j, "metalness", m.metalness);
    read_field(j, "exponent", m.exponent);
    read_field(j, "falloff", m.falloff);
    read_field(j, "fresnel_fill", m.fresnel_fill);
    read_field(j, "strength", m.strength);
    read_field(j, "speed", m.speed);
    read_field(j, "tint", m.tint);

    m.type = std::clamp(m.type, 0, static_cast<int>(visual_profile::chams::material_type_count) - 1);
}

static void to_json(json& j, const visual_profile::chams& c)
{
    j = json{
        {"enabled", c.enabled},
        {"antialiasing", c.antialiasing},
        {"glow_effect", json{{"enabled", c.glow_effect.enabled},
                             {"color", c.glow_effect.color},
                             {"radius", c.glow_effect.radius},
                             {"strength", c.glow_effect.strength},
                             {"layers", c.glow_effect.layers}}},
        {"kill_effect", json{{"enabled", c.kill_effect.enabled},
                             {"color", c.kill_effect.color},
                             {"duration", c.kill_effect.duration},
                             {"size", c.kill_effect.size},
                             {"count", c.kill_effect.count}}},
        {"on_shot",
         json{{"enabled", c.on_shot.enabled}, {"duration", c.on_shot.duration}, {"appearance", c.on_shot.appearance}}},
        {"occlude_dynamic_doors", c.occlude_dynamic_doors},
        {"occlude_smoke", c.occlude_smoke},
        {"visible", c.visible},
        {"invisible", c.invisible}};
}

static void from_json(const json& j, visual_profile::chams& c)
{
    read_field(j, "enabled", c.enabled);
    read_field(j, "antialiasing", c.antialiasing);
    if (j.contains("glow_effect"))
    {
        const auto& v = j.at("glow_effect");
        read_field(v, "enabled", c.glow_effect.enabled);
        read_field(v, "color", c.glow_effect.color);
        read_field(v, "radius", c.glow_effect.radius);
        read_field(v, "strength", c.glow_effect.strength);
        read_field(v, "layers", c.glow_effect.layers);
    }
    if (j.contains("kill_effect"))
    {
        const auto& v = j.at("kill_effect");
        read_field(v, "enabled", c.kill_effect.enabled);
        read_field(v, "color", c.kill_effect.color);
        read_field(v, "duration", c.kill_effect.duration);
        read_field(v, "size", c.kill_effect.size);
        read_field(v, "count", c.kill_effect.count);
    }
    if (j.contains("on_shot"))
    {
        const auto& v = j.at("on_shot");
        read_field(v, "enabled", c.on_shot.enabled);
        read_field(v, "duration", c.on_shot.duration);
        read_field(v, "appearance", c.on_shot.appearance);
    }
    read_field(j, "occlude_dynamic_doors", c.occlude_dynamic_doors);
    read_field(j, "occlude_smoke", c.occlude_smoke);
    read_field(j, "visible", c.visible);
    read_field(j, "invisible", c.invisible);
    c.glow_effect.radius = std::clamp(c.glow_effect.radius, 0.5f, 16.0f);
    c.glow_effect.strength = std::clamp(c.glow_effect.strength, 0.0f, 1.0f);
    c.glow_effect.layers = std::clamp(c.glow_effect.layers, 1, 6);
    c.kill_effect.duration = std::clamp(c.kill_effect.duration, 0.2f, 3.0f);
    c.kill_effect.size = std::clamp(c.kill_effect.size, 1.0f, 8.0f);
    c.kill_effect.count = std::clamp(c.kill_effect.count, 4, 32);
    c.on_shot.duration = std::clamp(c.on_shot.duration, 0.1f, 3.0f);

    if (!j.contains("visible"))
    {
        read_field(j, "wireframe", c.visible.wireframe);
        read_field(j, "color", c.visible.color);
    }
}

static void to_json(json& j, const visual_profile::sound_esp& s)
{
    j = json{{"enabled", s.enabled},
             {"local_sync", s.local_sync},
             {"duration", s.duration},
             {"radius", s.radius},
             {"color", s.color}};
}

static void from_json(const json& j, visual_profile::sound_esp& s)
{
    read_field(j, "enabled", s.enabled);
    read_field(j, "local_sync", s.local_sync);
    read_field(j, "duration", s.duration);
    read_field(j, "radius", s.radius);
    read_field(j, "color", s.color);
}

static void to_json(json& j, const visual_profile& e)
{
    j = json{{"m_player", e.m_player},      {"m_item", e.m_item},         {"m_projectile", e.m_projectile},
             {"m_bomb", e.m_bomb},          {"m_chams", e.m_chams},       {"m_radar", e.m_radar},
             {"m_sound", e.m_sound},        {"m_no_flash", e.m_no_flash}, {"m_no_smoke", e.m_no_smoke},
             {"m_crosshair", e.m_crosshair}};
}

static void from_json(const json& j, visual_profile& e)
{
    read_field(j, "m_player", e.m_player);
    read_field(j, "m_item", e.m_item);
    read_field(j, "m_projectile", e.m_projectile);
    read_field(j, "m_bomb", e.m_bomb);
    read_field(j, "m_chams", e.m_chams);
    read_field(j, "m_radar", e.m_radar);
    read_field(j, "m_sound", e.m_sound);
    read_field(j, "m_no_flash", e.m_no_flash);
    read_field(j, "m_no_smoke", e.m_no_smoke);
    read_field(j, "m_crosshair", e.m_crosshair);
}

static void to_json(json& j, const general_profile::bullet_tracers& b)
{
    j = json{{"enabled", b.enabled},
             {"color", b.color},
             {"duration", b.duration},
             {"thickness", b.thickness},
             {"bloom", b.bloom},
             {"max_count", b.max_count},
             {"draw_cubes", b.draw_cubes},
             {"draw_line", b.draw_line},
             {"cube_half", b.cube_half},
             {"cube_edge_color", b.cube_edge_color},
             {"cube_face_alpha", b.cube_face_alpha},
             {"fade_near", b.fade_near},
             {"fade_far", b.fade_far}};
}

static void from_json(const json& j, general_profile::bullet_tracers& b)
{
    read_field(j, "enabled", b.enabled);
    read_field(j, "color", b.color);
    read_field(j, "duration", b.duration);
    read_field(j, "thickness", b.thickness);
    read_field(j, "bloom", b.bloom);
    read_field(j, "max_count", b.max_count);
    read_field(j, "draw_cubes", b.draw_cubes);
    read_field(j, "draw_line", b.draw_line);
    read_field(j, "cube_half", b.cube_half);
    read_field(j, "cube_edge_color", b.cube_edge_color);
    read_field(j, "cube_face_alpha", b.cube_face_alpha);
    read_field(j, "fade_near", b.fade_near);
    read_field(j, "fade_far", b.fade_far);
}

static void to_json(json& j, const general_profile::hitmarker& h)
{
    j = json{{"enabled", h.enabled}, {"color", h.color},         {"size", h.size},
             {"gap", h.gap},         {"thickness", h.thickness}, {"duration", h.duration}};
}

static void from_json(const json& j, general_profile::hitmarker& h)
{
    read_field(j, "enabled", h.enabled);
    read_field(j, "color", h.color);
    read_field(j, "size", h.size);
    read_field(j, "gap", h.gap);
    read_field(j, "thickness", h.thickness);
    read_field(j, "duration", h.duration);
}

static void to_json(json& j, const general_profile::hitsound& h)
{
    j = json{{"enabled", h.enabled},
             {"style", h.style},
             {"volume", h.volume},
             {"show_damage", h.show_damage},
             {"damage_color", h.damage_color},
             {"damage_size", h.damage_size},
             {"damage_duration", h.damage_duration},
             {"damage_rise", h.damage_rise}};
}

static void from_json(const json& j, general_profile::hitsound& h)
{
    read_field(j, "enabled", h.enabled);
    read_field(j, "style", h.style);
    read_field(j, "volume", h.volume);
    read_field(j, "show_damage", h.show_damage);
    read_field(j, "damage_color", h.damage_color);
    read_field(j, "damage_size", h.damage_size);
    read_field(j, "damage_duration", h.damage_duration);
    read_field(j, "damage_rise", h.damage_rise);
    h.style = std::clamp(h.style, 0, 4);
    h.volume = std::clamp(h.volume, 0.0f, 1.0f);
    h.damage_size = std::clamp(h.damage_size, 8.0f, 28.0f);
    h.damage_duration = std::clamp(h.damage_duration, 0.15f, 2.0f);
    h.damage_rise = std::clamp(h.damage_rise, 0.0f, 100.0f);
}

static void to_json(json& j, const general_profile::screen_layout& l)
{
    j = json{{"version", 1},
             {"anchor", static_cast<int>(l.anchor)},
             {"offset_x", l.offset_x},
             {"offset_y", l.offset_y},
             {"scale", l.scale}};
}

static void from_json(const json& j, general_profile::screen_layout& l)
{
    int anchor = static_cast<int>(l.anchor);
    read_field(j, "anchor", anchor);
    anchor = std::clamp(anchor, 0, 3);
    l.anchor = static_cast<general_profile::screen_anchor>(anchor);
    read_field(j, "offset_x", l.offset_x);
    read_field(j, "offset_y", l.offset_y);
    read_field(j, "scale", l.scale);
    l.offset_x = std::isfinite(l.offset_x) ? std::max(0.0f, l.offset_x) : 40.0f;
    l.offset_y = std::isfinite(l.offset_y) ? std::max(0.0f, l.offset_y) : 40.0f;
    l.scale = std::isfinite(l.scale) ? std::clamp(l.scale, 0.55f, 2.0f) : 1.0f;
    l.version = 1;
}

static void to_json(json& j, const general_profile::watermark& w)
{
    j = json{{"enabled", w.enabled},     {"vertical", w.vertical}, {"show_ping", w.show_ping},
             {"show_loss", w.show_loss}, {"show_cpu", w.show_cpu}, {"show_fps", w.show_fps}};
    if (w.layout.version >= 1)
        j["layout"] = w.layout;
    else
    {
        j["position_x"] = w.layout.legacy_position_x;
        j["position_y"] = w.layout.legacy_position_y;
    }
}

static void from_json(const json& j, general_profile::watermark& w)
{
    read_field(j, "enabled", w.enabled);
    if (j.contains("layout"))
        j.at("layout").get_to(w.layout);
    else
    {
        w.layout.version = 0;
        read_field(j, "position_x", w.layout.legacy_position_x);
        read_field(j, "position_y", w.layout.legacy_position_y);
    }
    read_field(j, "vertical", w.vertical);
    read_field(j, "show_ping", w.show_ping);
    read_field(j, "show_loss", w.show_loss);
    read_field(j, "show_cpu", w.show_cpu);
    read_field(j, "show_fps", w.show_fps);
}

static void to_json(json& j, const general_profile::spectator_list& s)
{
    j = json{{"enabled", s.enabled}, {"show_avatars", s.show_avatars}};
    if (s.layout.version >= 1)
        j["layout"] = s.layout;
    else
    {
        j["position_x"] = s.layout.legacy_position_x;
        j["position_y"] = s.layout.legacy_position_y;
    }
}

static void from_json(const json& j, general_profile::spectator_list& s)
{
    read_field(j, "enabled", s.enabled);
    read_field(j, "show_avatars", s.show_avatars);
    if (j.contains("layout"))
        j.at("layout").get_to(s.layout);
    else
    {
        s.layout.version = 0;
        read_field(j, "position_x", s.layout.legacy_position_x);
        read_field(j, "position_y", s.layout.legacy_position_y);
    }
}

static void to_json(json& j, const general_profile::event_log& e)
{
    j = json{{"enabled", e.enabled},         {"layout", e.layout},           {"duration", e.duration},
             {"max_entries", e.max_entries}, {"show_shots", e.show_shots},   {"show_hits", e.show_hits},
             {"show_kills", e.show_kills},   {"show_misses", e.show_misses}, {"show_blocked", e.show_blocked},
             {"show_info", e.show_info}};
}

static void from_json(const json& j, general_profile::event_log& e)
{
    read_field(j, "enabled", e.enabled);
    read_field(j, "layout", e.layout);
    read_field(j, "duration", e.duration);
    read_field(j, "max_entries", e.max_entries);
    read_field(j, "show_shots", e.show_shots);
    read_field(j, "show_hits", e.show_hits);
    read_field(j, "show_kills", e.show_kills);
    read_field(j, "show_misses", e.show_misses);
    read_field(j, "show_blocked", e.show_blocked);
    read_field(j, "show_info", e.show_info);
    e.duration = std::isfinite(e.duration) ? std::clamp(e.duration, 0.5f, 20.0f) : 5.0f;
    e.max_entries = std::clamp(e.max_entries, 1, 5);
}

static void to_json(json& j, const general_profile::keybind_list& k)
{
    j = json{{"enabled", k.enabled},
             {"layout", k.layout},
             {"show_always", k.show_always},
             {"show_hold", k.show_hold},
             {"show_toggle", k.show_toggle}};
}

static void from_json(const json& j, general_profile::keybind_list& k)
{
    read_field(j, "enabled", k.enabled);
    read_field(j, "layout", k.layout);
    read_field(j, "show_always", k.show_always);
    read_field(j, "show_hold", k.show_hold);
    read_field(j, "show_toggle", k.show_toggle);
}

static void to_json(json& j, const general_profile::bomb_info& b)
{
    j = json{};
    if (b.layout.version >= 1)
        j["layout"] = b.layout;
    else
    {
        j["position_x"] = b.layout.legacy_position_x;
        j["position_y"] = b.layout.legacy_position_y;
    }
}

static void from_json(const json& j, general_profile::bomb_info& b)
{
    if (j.contains("layout"))
        j.at("layout").get_to(b.layout);
    else
    {
        b.layout.version = 0;
        read_field(j, "position_x", b.layout.legacy_position_x);
        read_field(j, "position_y", b.layout.legacy_position_y);
    }
}

static void to_json(json& j, const general_profile::grenades& g)
{
    j = json{{"enabled", g.enabled},
             {"local_only", g.local_only},
             {"color", g.color},
             {"thickness", g.thickness},
             {"bloom", g.bloom},
             {"bloom_color", g.bloom_color},
             {"bloom_radius", g.bloom_radius},
             {"show_bounces", g.show_bounces},
             {"bounce_color", g.bounce_color},
             {"bounce_size", g.bounce_size},
             {"show_endpoint", g.show_endpoint},
             {"endpoint_color", g.endpoint_color},
             {"endpoint_size", g.endpoint_size}};
}

static void from_json(const json& j, general_profile::grenades& g)
{
    read_field(j, "enabled", g.enabled);
    read_field(j, "local_only", g.local_only);
    read_field(j, "color", g.color);
    read_field(j, "thickness", g.thickness);
    read_field(j, "bloom", g.bloom);
    read_field(j, "bloom_color", g.bloom_color);
    read_field(j, "bloom_radius", g.bloom_radius);
    read_field(j, "show_bounces", g.show_bounces);
    read_field(j, "bounce_color", g.bounce_color);
    read_field(j, "bounce_size", g.bounce_size);
    read_field(j, "show_endpoint", g.show_endpoint);
    read_field(j, "endpoint_color", g.endpoint_color);
    read_field(j, "endpoint_size", g.endpoint_size);
    g.thickness = std::clamp(g.thickness, 0.5f, 8.0f);
    g.bloom_radius = std::clamp(g.bloom_radius, 0.5f, 12.0f);
    g.bounce_size = std::clamp(g.bounce_size, 1.0f, 16.0f);
    g.endpoint_size = std::clamp(g.endpoint_size, 2.0f, 24.0f);
}

static void to_json(json& j, const general_profile::nade_helper& n)
{
    j = json{{"enabled", n.enabled},
             {"draw_distance", n.draw_distance},
             {"stand_distance", n.stand_distance},
             {"stand_radius", n.stand_radius},
             {"release_radius", n.release_radius},
             {"height_tolerance", n.height_tolerance},
             {"show_action", n.show_action},
             {"show_distance", n.show_distance},
             {"plaque_background", n.plaque_background},
             {"plaque_text", n.plaque_text},
             {"plaque_accent", n.plaque_accent},
             {"stand_marker", n.stand_marker},
             {"stand_marker_active", n.stand_marker_active},
             {"aim_marker", n.aim_marker},
             {"aim_assist", n.aim_assist},
             {"auto_release", n.auto_release},
             {"aim_key", n.aim_key},
             {"aim_smoothing", n.aim_smoothing},
             {"aim_threshold", n.aim_threshold},
             {"lock_time_ms", n.lock_time_ms}};
}

static void from_json(const json& j, general_profile::nade_helper& n)
{
    read_field(j, "enabled", n.enabled);
    read_field(j, "draw_distance", n.draw_distance);
    read_field(j, "stand_distance", n.stand_distance);
    read_field(j, "stand_radius", n.stand_radius);
    read_field(j, "release_radius", n.release_radius);
    read_field(j, "height_tolerance", n.height_tolerance);
    read_field(j, "show_action", n.show_action);
    read_field(j, "show_distance", n.show_distance);
    read_field(j, "plaque_background", n.plaque_background);
    read_field(j, "plaque_text", n.plaque_text);
    read_field(j, "plaque_accent", n.plaque_accent);
    read_field(j, "stand_marker", n.stand_marker);
    read_field(j, "stand_marker_active", n.stand_marker_active);
    read_field(j, "aim_marker", n.aim_marker);
    read_field(j, "aim_assist", n.aim_assist);
    read_field(j, "auto_release", n.auto_release);
    read_field(j, "aim_key", n.aim_key);
    read_field(j, "aim_smoothing", n.aim_smoothing);
    read_field(j, "aim_threshold", n.aim_threshold);
    read_field(j, "lock_time_ms", n.lock_time_ms);
    n.release_radius = std::clamp(n.release_radius, 1.0f, 16.0f);
    n.height_tolerance = std::clamp(n.height_tolerance, 1.0f, 24.0f);
    n.lock_time_ms = std::clamp(n.lock_time_ms, 0, 250);
}

static void to_json(json& j, const general_profile::bunny_hop& b)
{
    j = json{{"enabled", b.enabled}, {"activation_key", b.activation_key}};
}

static void from_json(const json& j, general_profile::bunny_hop& b)
{
    read_field(j, "enabled", b.enabled);
    if (j.contains("activation_key"))
        j.at("activation_key").get_to(b.activation_key);
    else if (j.contains("key"))
        j.at("key").get_to(b.activation_key);
}

static json buy_presets_json(const general_profile::walk_bot& w)
{
    auto presets = json::array();
    for (const auto& preset : w.buy_presets)
    {
        auto items = json::array();
        for (int i = 0; i < std::clamp(preset.count, 0, general_profile::walk_bot::k_buy_items); ++i)
            if (const auto& item = preset.items[static_cast<std::size_t>(i)]; true)
                items.push_back({{"enabled", item.enabled},
                                 {"code", std::string{item.code.data()}},
                                 {"side", item.side},
                                 {"money_rule", item.money_rule},
                                 {"money", item.money}});
        presets.push_back({{"custom", preset.custom}, {"random_one", preset.random_one}, {"items", items}});
    }
    return presets;
}

static void read_buy_presets(const json& j, general_profile::walk_bot& w)
{
    const auto found = j.find("buy_presets");
    if (found == j.end() || !found->is_array())
        return;
    for (std::size_t p = 0; p < w.buy_presets.size() && p < found->size(); ++p)
    {
        const auto& source = (*found)[p];
        auto& preset = w.buy_presets[p];
        read_field(source, "custom", preset.custom);
        read_field(source, "random_one", preset.random_one);
        const auto items = source.find("items");
        if (items == source.end() || !items->is_array())
            continue;
        preset.count = static_cast<int>(std::min<std::size_t>(items->size(), preset.items.size()));
        for (std::size_t i = 0; i < preset.items.size(); ++i)
        {
            auto& item = preset.items[i];
            if (i >= items->size())
            {
                item = {};
                continue;
            }
            const auto& entry = (*items)[i];
            read_field(entry, "enabled", item.enabled);
            std::string code{item.code.data()};
            read_field(entry, "code", code);
            item.code.fill('\0');
            std::memcpy(item.code.data(), code.data(), std::min(code.size(), item.code.size() - 1));
            read_field(entry, "side", item.side);
            item.side = std::clamp(item.side, 0, 2);
            read_field(entry, "money_rule", item.money_rule);
            item.money_rule = std::clamp(item.money_rule, 0, 2);
            read_field(entry, "money", item.money);
            item.money = std::clamp(item.money, 0, 16000);
        }
    }
}

static void to_json(json& j, const general_profile::walk_bot& w)
{
    j = json{{"enabled", w.enabled},
             {"route", w.mode},
             {"hunt", w.hunt},
             {"show_recordings", w.show_recordings},
             {"aim", w.aim},
             {"aim_speed", w.aim_speed},
             {"autofire", w.autofire},
             {"react_to_hits", w.react_to_hits},
             {"look", w.look},
             {"breakables", w.breakables},
             {"record_key", w.record_key},
             {"auto_buy", w.auto_buy},
             {"buy_edit", w.buy_edit},
             {"buy_presets", buy_presets_json(w)},
             {"auto_reload", w.auto_reload},
             {"auto_rejoin", w.auto_rejoin},
             {"game_mode", w.game_mode},
             {"team", w.team},
             {"auto_disconnect", w.auto_disconnect},
             {"disconnect_score", w.disconnect_score},
             {"disconnect_minutes", w.disconnect_minutes},
             {"disconnect_spread", w.disconnect_spread},
             {"show_path", w.show_path},
             {"path_color", w.path_color},
             {"path_thickness", w.path_thickness},
             {"show_mesh", w.show_mesh},
             {"mesh_color", w.mesh_color},
             {"mesh_distance", w.mesh_distance}};
}

static void from_json(const json& j, general_profile::walk_bot& w)
{
    read_field(j, "enabled", w.enabled);
    if (j.contains("route"))
        read_field(j, "route", w.mode);
    else if (int old_mode{}; j.contains("mode"))
    {
        // Older settings: 0 random, 1 hunt, 2 recorded (hunt is a flag now).
        read_field(j, "mode", old_mode);
        w.hunt = old_mode == 1;
        w.mode = old_mode == 2 ? general_profile::walk_bot::recorded : general_profile::walk_bot::random_points;
    }
    w.mode = std::clamp(w.mode, 0, general_profile::walk_bot::mode_count - 1);
    read_field(j, "hunt", w.hunt);
    read_field(j, "show_recordings", w.show_recordings);
    read_field(j, "aim", w.aim);
    read_field(j, "aim_speed", w.aim_speed);
    w.aim_speed = std::isfinite(w.aim_speed) ? std::clamp(w.aim_speed, 0.4f, 2.5f) : 1.0f;
    read_field(j, "autofire", w.autofire);
    read_field(j, "react_to_hits", w.react_to_hits);
    read_field(j, "breakables", w.breakables);
    read_field(j, "record_key", w.record_key);
    read_field(j, "auto_buy", w.auto_buy);
    read_field(j, "buy_edit", w.buy_edit);
    w.buy_edit = std::clamp(w.buy_edit, 0, static_cast<int>(general_profile::walk_bot::buy_mode_count) - 1);
    read_buy_presets(j, w);
    read_field(j, "auto_reload", w.auto_reload);
    read_field(j, "auto_rejoin", w.auto_rejoin);
    read_field(j, "game_mode", w.game_mode);
    w.game_mode = std::clamp(w.game_mode, 0, 4);
    read_field(j, "team", w.team);
    w.team = std::clamp(w.team, 0, 1);
    read_field(j, "auto_disconnect", w.auto_disconnect);
    read_field(j, "disconnect_score", w.disconnect_score);
    w.disconnect_score = std::clamp(w.disconnect_score, 0, 10000);
    read_field(j, "disconnect_minutes", w.disconnect_minutes);
    w.disconnect_minutes = std::clamp(w.disconnect_minutes, 0, 120);
    read_field(j, "disconnect_spread", w.disconnect_spread);
    w.disconnect_spread = std::clamp(w.disconnect_spread, 0, 100);
    read_field(j, "show_path", w.show_path);
    read_field(j, "path_color", w.path_color);
    read_field(j, "path_thickness", w.path_thickness);
    w.path_thickness = std::isfinite(w.path_thickness) ? std::clamp(w.path_thickness, 0.5f, 6.0f) : 2.0f;
    read_field(j, "look", w.look);
    read_field(j, "show_mesh", w.show_mesh);
    read_field(j, "mesh_color", w.mesh_color);
    read_field(j, "mesh_distance", w.mesh_distance);
    w.mesh_distance = std::isfinite(w.mesh_distance) ? std::clamp(w.mesh_distance, 200.0f, 4000.0f) : 1400.0f;
}

static void to_json(json& j, const general_profile::edge_jump& e)
{
    j = json{{"enabled", e.enabled}, {"activation_key", e.activation_key}};
}

static void from_json(const json& j, general_profile::edge_jump& e)
{
    read_field(j, "enabled", e.enabled);
    if (j.contains("activation_key"))
        j.at("activation_key").get_to(e.activation_key);
    else if (j.contains("key"))
        j.at("key").get_to(e.activation_key);
}

static void to_json(json& j, const game_controls& b)
{
    j = json{{"forward", b.forward}, {"back", b.back}, {"left", b.left},     {"right", b.right},
             {"walk", b.walk},       {"duck", b.duck}, {"jump", b.jump},     {"attack", b.attack},
             {"attack2", b.attack2}, {"use", b.use},   {"reload", b.reload}, {"buy_menu", b.buy_menu}};
}

static void from_json(const json& j, game_controls& b)
{
    read_field(j, "forward", b.forward);
    read_field(j, "back", b.back);
    read_field(j, "left", b.left);
    read_field(j, "right", b.right);
    read_field(j, "walk", b.walk);
    read_field(j, "duck", b.duck);
    read_field(j, "jump", b.jump);
    read_field(j, "attack", b.attack);
    read_field(j, "attack2", b.attack2);
    read_field(j, "use", b.use);
    read_field(j, "reload", b.reload);
    read_field(j, "buy_menu", b.buy_menu);
    for (int* key : {&b.forward, &b.back, &b.left, &b.right, &b.walk, &b.duck, &b.jump, &b.attack, &b.attack2, &b.use,
                     &b.reload, &b.buy_menu})
        *key = std::clamp(*key, 0, 255);
}

static void to_json(json& j, const general_profile::auto_stop& a)
{
    j = json{{"enabled", a.enabled},
             {"physical_fire", a.physical_fire},
             {"predictive_trigger", a.predictive_trigger},
             {"stop_speed", a.stop_speed},
             {"required_shoot_speed", a.required_shoot_speed}};
}

static void from_json(const json& j, general_profile::auto_stop& a)
{
    read_field(j, "enabled", a.enabled);
    read_field(j, "physical_fire", a.physical_fire);
    read_field(j, "predictive_trigger", a.predictive_trigger);
    read_field(j, "stop_speed", a.stop_speed);
    read_field(j, "required_shoot_speed", a.required_shoot_speed);
    a.stop_speed = std::clamp(a.stop_speed, 0.0f, 150.0f);
    a.required_shoot_speed = std::clamp(a.required_shoot_speed, 0.0f, 60.0f);
}

static void to_json(json& j, const general_profile& m)
{
    j = json{{"m_grenades", m.m_grenades},
             {"m_nade_helper", m.m_nade_helper},
             {"m_bullet_tracers", m.m_bullet_tracers},
             {"m_hitmarker", m.m_hitmarker},
             {"m_hitsound", m.m_hitsound},
             {"m_watermark", m.m_watermark},
             {"m_spectator_list", m.m_spectator_list},
             {"m_event_log", m.m_event_log},
             {"m_keybind_list", m.m_keybind_list},
             {"m_bomb_info", m.m_bomb_info},
             {"m_bunny_hop", m.m_bunny_hop},
             {"m_edge_jump", m.m_edge_jump},
             {"m_game_controls", m.m_game_controls},
             {"m_auto_stop", m.m_auto_stop},
             {"m_walk_bot", m.m_walk_bot},
             {"language", m.language},
             {"menu_scale", m.menu_scale},
             {"palette", json{{"background", m.palette.background},
                              {"panel", m.palette.panel},
                              {"card", m.palette.card},
                              {"popup", m.palette.popup},
                              {"accent", m.palette.accent},
                              {"text", m.palette.text},
                              {"muted_text", m.palette.muted_text},
                              {"border", m.palette.border},
                              {"hover", m.palette.hover}}},
             {"auto_accept", m.auto_accept},
             {"obs_bypass", m.obs_bypass},
             {"limit_fps", m.limit_fps},
             {"fps_limit", m.fps_limit},
             {"use_gpu", m.use_gpu},
             {"cpu_priority", m.cpu_priority},
             {"gpu_priority", m.gpu_priority}};
}

static void from_json(const json& j, general_profile& m)
{
    read_field(j, "m_grenades", m.m_grenades);
    read_field(j, "m_nade_helper", m.m_nade_helper);
    read_field(j, "m_bullet_tracers", m.m_bullet_tracers);
    read_field(j, "m_hitmarker", m.m_hitmarker);
    read_field(j, "m_hitsound", m.m_hitsound);
    read_field(j, "m_watermark", m.m_watermark);
    read_field(j, "m_spectator_list", m.m_spectator_list);
    read_field(j, "m_event_log", m.m_event_log);
    read_field(j, "m_keybind_list", m.m_keybind_list);
    read_field(j, "m_bomb_info", m.m_bomb_info);
    read_field(j, "m_bunny_hop", m.m_bunny_hop);
    read_field(j, "m_edge_jump", m.m_edge_jump);
    read_field(j, "m_game_controls", m.m_game_controls);
    read_field(j, "m_auto_stop", m.m_auto_stop);
    read_field(j, "m_walk_bot", m.m_walk_bot);
    read_field(j, "language", m.language);
    m.language = std::clamp(m.language, 0, general_profile::k_language_count - 1);
    // The config is the only persistent home for the language, so applying it
    // here keeps lang and settings from drifting apart on load.
    read_field(j, "menu_scale", m.menu_scale);
    m.menu_scale = std::isfinite(m.menu_scale) ? std::clamp(m.menu_scale, 0.50f, 1.50f) : 1.0f;
    if (j.contains("palette"))
    {
        const auto& p = j.at("palette");
        read_field(p, "background", m.palette.background);
        read_field(p, "panel", m.palette.panel);
        read_field(p, "card", m.palette.card);
        read_field(p, "popup", m.palette.popup);
        read_field(p, "accent", m.palette.accent);
        read_field(p, "text", m.palette.text);
        read_field(p, "muted_text", m.palette.muted_text);
        read_field(p, "border", m.palette.border);
        read_field(p, "hover", m.palette.hover);
    }
    read_field(j, "auto_accept", m.auto_accept);
    read_field(j, "obs_bypass", m.obs_bypass);
    read_field(j, "limit_fps", m.limit_fps);
    read_field(j, "fps_limit", m.fps_limit);
    read_field(j, "use_gpu", m.use_gpu);
    read_field(j, "cpu_priority", m.cpu_priority);
    m.cpu_priority = std::clamp(m.cpu_priority, 0, 2);
    read_field(j, "gpu_priority", m.gpu_priority);
    m.gpu_priority = std::clamp(m.gpu_priority, 0, 2);
}

combat_profile::resolved_config combat_profile::get(std::uint32_t weapon_type) const
{
    const auto idx = weapon_type - game::rules::sidearm;
    const auto group_idx = idx < k_group_count ? idx : 2;
    const auto& ov = overrides[group_idx];
    const auto& gl = global;

    resolved_config cfg{};

    cfg.aimbot.enabled = gl.aimbot_enabled;
    cfg.aimbot.key = gl.aimbot_key;
    cfg.aimbot.activation_mode = gl.aimbot_activation_mode;
    cfg.aimbot.fov = ov.use_global ? gl.aimbot_fov : ov.aimbot_fov;
    cfg.aimbot.smoothing = ov.use_global ? gl.aimbot_smoothing : ov.aimbot_smoothing;
    cfg.aimbot.humanize = ov.use_global ? gl.aimbot_humanize : ov.aimbot_humanize;
    cfg.aimbot.autowall = ov.use_global ? gl.aimbot_autowall : ov.aimbot_autowall;
    cfg.aimbot.min_damage = ov.use_global ? gl.aimbot_min_damage : ov.aimbot_min_damage;
    if (gl.aimbot_min_damage_override_enabled &&
        combat_profile::activation_active(gl.aimbot_min_damage_override_mode, gl.aimbot_min_damage_override_key))
        cfg.aimbot.min_damage = gl.aimbot_min_damage_override;
    cfg.aimbot.lethal_only = ov.use_global ? gl.aimbot_lethal_only : ov.aimbot_lethal_only;
    cfg.aimbot.hitbox_parts = ov.use_global ? gl.aimbot_hitbox_parts : ov.aimbot_hitbox_parts;
    cfg.aimbot.multipoint = ov.use_global ? gl.aimbot_multipoint : ov.aimbot_multipoint;
    cfg.aimbot.visible_only = ov.use_global ? gl.aimbot_visible_only : ov.aimbot_visible_only;
    cfg.aimbot.draw_fov = gl.aimbot_draw_fov;
    cfg.aimbot.fov_color = gl.aimbot_fov_color;
    cfg.aimbot.predictive = ov.use_global ? gl.aimbot_predictive : ov.aimbot_predictive;
    cfg.aimbot.recoil_sync = ov.use_global ? gl.aimbot_recoil_sync : ov.aimbot_recoil_sync;
    cfg.aimbot.checks = ov.use_global ? gl.aimbot_checks : ov.aimbot_checks;
    cfg.aimbot.humanizer = ov.use_global ? gl.aimbot_humanizer : ov.aimbot_humanizer;
    cfg.aimbot.multipoint_config = ov.use_global ? gl.aimbot_multipoint_config : ov.aimbot_multipoint_config;
    cfg.aimbot.prediction = ov.use_global ? gl.aimbot_prediction : ov.aimbot_prediction;
    cfg.aimbot.rcs = ov.use_global ? gl.aimbot_rcs : ov.aimbot_rcs;
    cfg.aimbot.fov_config = ov.use_global ? gl.aimbot_fov_config : ov.aimbot_fov_config;
    cfg.aimbot.predictive = cfg.aimbot.prediction.enabled;
    // The legacy toggle is migration input only. Runtime ownership belongs to the
    // standalone RCS profile so it can work independently from Aimbot activation.
    cfg.aimbot.recoil_sync = cfg.aimbot.rcs.enabled;

    cfg.triggerbot.enabled = gl.triggerbot_enabled;
    cfg.triggerbot.key = gl.triggerbot_key;
    cfg.triggerbot.activation_mode = gl.triggerbot_activation_mode;
    cfg.triggerbot.seed_type = ov.use_global ? gl.triggerbot_seed_type : ov.triggerbot_seed_type;
    cfg.triggerbot.hitbox_parts = ov.use_global ? gl.triggerbot_hitbox_parts : ov.triggerbot_hitbox_parts;
    cfg.triggerbot.hitchance = ov.use_global ? gl.triggerbot_hitchance : ov.triggerbot_hitchance;
    cfg.triggerbot.delay = ov.use_global ? gl.triggerbot_delay : ov.triggerbot_delay;
    cfg.triggerbot.randomize_ms = ov.use_global ? gl.triggerbot_randomize_ms : ov.triggerbot_randomize_ms;
    cfg.triggerbot.outlier_chance = ov.use_global ? gl.triggerbot_outlier_chance : ov.triggerbot_outlier_chance;
    cfg.triggerbot.outlier_delay_ms = ov.use_global ? gl.triggerbot_outlier_delay_ms : ov.triggerbot_outlier_delay_ms;
    cfg.triggerbot.delay_after_ms = ov.use_global ? gl.triggerbot_delay_after_ms : ov.triggerbot_delay_after_ms;
    cfg.triggerbot.lethal_only = ov.use_global ? gl.triggerbot_lethal_only : ov.triggerbot_lethal_only;
    cfg.triggerbot.reaction_time = ov.use_global ? gl.triggerbot_reaction_time : ov.triggerbot_reaction_time;
    cfg.triggerbot.autowall = ov.use_global ? gl.triggerbot_autowall : ov.triggerbot_autowall;
    cfg.triggerbot.min_damage = ov.use_global ? gl.triggerbot_min_damage : ov.triggerbot_min_damage;
    if (gl.triggerbot_min_damage_override_enabled &&
        combat_profile::activation_active(gl.triggerbot_min_damage_override_mode,
                                          gl.triggerbot_min_damage_override_key))
        cfg.triggerbot.min_damage = gl.triggerbot_min_damage_override;
    cfg.triggerbot.autostop = ov.use_global ? gl.triggerbot_autostop : ov.triggerbot_autostop;
    cfg.triggerbot.predictive = ov.use_global ? gl.triggerbot_predictive : ov.triggerbot_predictive;
    cfg.triggerbot.revolver_pre_cock =
        ov.use_global ? gl.triggerbot_revolver_pre_cock : ov.triggerbot_revolver_pre_cock;
    cfg.triggerbot.revolver_release_margin_ms =
        ov.use_global ? gl.triggerbot_revolver_release_margin_ms : ov.triggerbot_revolver_release_margin_ms;
    cfg.triggerbot.checks = ov.use_global ? gl.triggerbot_checks : ov.triggerbot_checks;

    cfg.other.penetration_crosshair = gl.penetration_crosshair;
    cfg.other.penetration_color_yes = gl.penetration_color_yes;
    cfg.other.penetration_color_no = gl.penetration_color_no;

    return cfg;
}

bool combat_profile::seed_trigger_configured() const noexcept
{
    if (!global.triggerbot_enabled)
    {
        return false;
    }

    for (const auto& override : overrides)
    {
        const auto seed = override.use_global ? global.triggerbot_seed_type : override.triggerbot_seed_type;
        if (seed != seed_mode::none)
        {
            return true;
        }
    }
    return false;
}

json build_config_json(const runtime_snapshot& state)
{
    json j;
    j["combat_global"] = state.combat.global;
    j["combat_overrides"] = state.combat.overrides;
    j["esp"] = state.visual;
    j["misc"] = state.general;
    return j;
}

json build_config_json()
{
    return build_config_json(runtime_snapshot{combat_settings, visual_settings, general_settings});
}

void decode_config_json(const json& j, runtime_snapshot& into)
{
    auto& combat = into.combat;
    auto& visual = into.visual;
    auto& general = into.general;
    read_field(j, "combat_global", combat.global);
    read_field(j, "combat_overrides", combat.overrides);
    read_field(j, "esp", visual);
    read_field(j, "misc", general);

    // Auto stop used to be a per-weapon trigger option; older files carry it only there.
    if (!j.contains("misc") || !j.at("misc").contains("m_auto_stop"))
    {
        general.m_auto_stop.enabled =
            combat.global.triggerbot_autostop ||
            std::ranges::any_of(combat.overrides, [](const auto& group) { return group.triggerbot_autostop; });
    }
}

void apply_config_json(const json& j)
{
    // Decode into copies and commit only a fully decoded document, so a malformed file never leaves the settings
    // half-applied.
    runtime_snapshot decoded{combat_settings, visual_settings, general_settings};
    decode_config_json(j, decoded);
    combat_settings = std::move(decoded.combat);
    visual_settings = std::move(decoded.visual);
    general_settings = std::move(decoded.general);
    publish_runtime_snapshot();
}

using diagnostics::process_log;

bool apply_default_config()
{
    try
    {
        const auto* begin = reinterpret_cast<const char*>(embedded::default_profile_json);
        apply_config_json(json::parse(begin, begin + embedded::default_profile_json_size));
        return true;
    }
    catch (...)
    {
        process_log().warning("[config] the built-in default profile could not be applied: {}",
                              diagnostics::current_exception_message());
        return false;
    }
}

std::string configuration_store::cache_path() const
{
    const auto dir = platform::windows::runtime_storage::paths().configs;
    if (dir.empty())
        return {};
    return path_to_utf8(dir / "current.cfg");
}

bool configuration_store::write_cache() const
{
    const std::scoped_lock lock(m_cache_mutex);
    try
    {
        const auto target = cache_path();
        if (target.empty())
        {
            process_log().warning("[config] no configuration folder; the live configuration is not saved.");
            return false;
        }
        const auto contents = file::serialize(
            "Current", build_config_json(*get_runtime_snapshot()),
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        if (contents == m_last_written)
            return true;
        if (const auto error = platform::windows::write_file_atomically(std::filesystem::u8path(target), contents))
        {
            process_log().warning("[config] cannot save {}: {}", target, error.message());
            return false;
        }
        m_last_written = contents;
        return true;
    }
    catch (...)
    {
        process_log().warning("[config] saving the live configuration failed: {}",
                              diagnostics::current_exception_message());
        return false;
    }
}

bool configuration_store::read_cache()
{
    const auto path = cache_path();
    std::error_code error{};
    if (path.empty() || !std::filesystem::exists(std::filesystem::u8path(path), error))
        return false;
    // The live configuration may still be in the format before named configs carried a header.
    const auto document = file::read(std::filesystem::u8path(path), true);
    if (!document)
    {
        process_log().warning("[config] the live configuration was not loaded: {}", file::describe(document.error()));
        return false;
    }
    try
    {
        apply_config_json(document->settings);
        return true;
    }
    catch (...)
    {
        process_log().warning("[config] applying the live configuration failed: {}",
                              diagnostics::current_exception_message());
        return false;
    }
}

} // namespace config
