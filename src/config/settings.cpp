#include <stdafx.hpp>

#include <config/default_profile.hpp>
#include <config/settings.hpp>
#include <render/menu/localization.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <shlwapi.h>

using json = nlohmann::json;

namespace config {

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
	j = json{
		{"aimbot_enabled", g.aimbot_enabled},
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
		{"penetration_color_no", g.penetration_color_no}
	};
}

static void from_json(const json& j, combat_profile::global_settings& g)
{
	if (j.contains("aimbot_enabled")) j.at("aimbot_enabled").get_to(g.aimbot_enabled);
	if (j.contains("aimbot_key")) j.at("aimbot_key").get_to(g.aimbot_key);
	if (j.contains("aimbot_activation_mode")) j.at("aimbot_activation_mode").get_to(g.aimbot_activation_mode);
	g.aimbot_activation_mode = std::clamp(g.aimbot_activation_mode,
		static_cast<int>(combat_profile::activation::hold),
		static_cast<int>(combat_profile::activation::toggle));
	if (j.contains("aimbot_fov")) j.at("aimbot_fov").get_to(g.aimbot_fov);
	if (j.contains("aimbot_smoothing")) j.at("aimbot_smoothing").get_to(g.aimbot_smoothing);
	if (j.contains("aimbot_humanize")) j.at("aimbot_humanize").get_to(g.aimbot_humanize);
	if (j.contains("aimbot_autowall")) j.at("aimbot_autowall").get_to(g.aimbot_autowall);
	if (j.contains("aimbot_min_damage")) j.at("aimbot_min_damage").get_to(g.aimbot_min_damage);
	if (j.contains("aimbot_min_damage_override_enabled")) j.at("aimbot_min_damage_override_enabled").get_to(g.aimbot_min_damage_override_enabled);
	if (j.contains("aimbot_min_damage_override")) j.at("aimbot_min_damage_override").get_to(g.aimbot_min_damage_override);
	if (j.contains("aimbot_min_damage_override_mode")) j.at("aimbot_min_damage_override_mode").get_to(g.aimbot_min_damage_override_mode);
	if (j.contains("aimbot_min_damage_override_key")) j.at("aimbot_min_damage_override_key").get_to(g.aimbot_min_damage_override_key);
	g.aimbot_min_damage_override = std::clamp(g.aimbot_min_damage_override, 1.0f, 100.0f);
	g.aimbot_min_damage_override_mode = std::clamp(g.aimbot_min_damage_override_mode,
		static_cast<int>(combat_profile::activation::hold),
		static_cast<int>(combat_profile::activation::toggle));
	g.aimbot_min_damage_override_key = std::clamp(g.aimbot_min_damage_override_key, 0, 255);
	if (j.contains("aimbot_lethal_only")) j.at("aimbot_lethal_only").get_to(g.aimbot_lethal_only);
	if (j.contains("aimbot_hitbox_parts")) j.at("aimbot_hitbox_parts").get_to(g.aimbot_hitbox_parts);
	if (j.contains("aimbot_multipoint")) j.at("aimbot_multipoint").get_to(g.aimbot_multipoint);
	if (j.contains("aimbot_visible_only")) j.at("aimbot_visible_only").get_to(g.aimbot_visible_only);
	if (j.contains("aimbot_draw_fov")) j.at("aimbot_draw_fov").get_to(g.aimbot_draw_fov);
	if (j.contains("aimbot_fov_color")) j.at("aimbot_fov_color").get_to(g.aimbot_fov_color);
	if (j.contains("aimbot_predictive")) j.at("aimbot_predictive").get_to(g.aimbot_predictive);
	if (j.contains("aimbot_recoil_sync")) j.at("aimbot_recoil_sync").get_to(g.aimbot_recoil_sync);
	if (j.contains("aimbot_checks")) j.at("aimbot_checks").get_to(g.aimbot_checks);
	else if (g.aimbot_visible_only)
		g.aimbot_checks.walls = g.aimbot_autowall
			? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
	else g.aimbot_checks.walls = combat_profile::wall_policy::penetration;
	if (j.contains("aimbot_humanizer")) j.at("aimbot_humanizer").get_to(g.aimbot_humanizer);
	if (j.contains("aimbot_multipoint_config")) j.at("aimbot_multipoint_config").get_to(g.aimbot_multipoint_config);
	if (j.contains("aimbot_prediction")) j.at("aimbot_prediction").get_to(g.aimbot_prediction);
	else g.aimbot_prediction.enabled = g.aimbot_predictive;
	if (j.contains("aimbot_rcs")) j.at("aimbot_rcs").get_to(g.aimbot_rcs);
	else g.aimbot_rcs.enabled = g.aimbot_recoil_sync;
	if (j.contains("aimbot_fov_config")) j.at("aimbot_fov_config").get_to(g.aimbot_fov_config);
	else g.aimbot_fov_config.far_fov = static_cast<float>(g.aimbot_fov);
	if (j.contains("grenade_aim_enabled")) j.at("grenade_aim_enabled").get_to(g.grenade_aim.enabled);
	if (j.contains("grenade_aim_key")) j.at("grenade_aim_key").get_to(g.grenade_aim.key);
	if (j.contains("grenade_aim_fov")) j.at("grenade_aim_fov").get_to(g.grenade_aim.fov);
	if (j.contains("grenade_aim_smoothing")) j.at("grenade_aim_smoothing").get_to(g.grenade_aim.smoothing);
	if (j.contains("triggerbot_enabled")) j.at("triggerbot_enabled").get_to(g.triggerbot_enabled);
	if (j.contains("triggerbot_key")) j.at("triggerbot_key").get_to(g.triggerbot_key);
	if (j.contains("triggerbot_activation_mode")) j.at("triggerbot_activation_mode").get_to(g.triggerbot_activation_mode);
	g.triggerbot_activation_mode = std::clamp(g.triggerbot_activation_mode,
		static_cast<int>(combat_profile::activation::hold),
		static_cast<int>(combat_profile::activation::toggle));

	const auto legacy_simple_mode = j.value("triggerbot_mode", 0) == 1;
	if (j.contains("triggerbot_seed_type"))
		j.at("triggerbot_seed_type").get_to(g.triggerbot_seed_type);
	else if (j.contains("triggerbot_seed"))
		g.triggerbot_seed_type = j.at("triggerbot_seed").get<bool>() ? combat_profile::seed_mode::unrestricted : combat_profile::seed_mode::none;
	g.triggerbot_seed_type = std::clamp(g.triggerbot_seed_type,
		static_cast<int>(combat_profile::seed_mode::none), static_cast<int>(combat_profile::seed_mode::unrestricted));
	if (j.contains("triggerbot_hitbox_parts")) j.at("triggerbot_hitbox_parts").get_to(g.triggerbot_hitbox_parts);
	g.triggerbot_hitbox_parts &= combat_profile::aim_part::all;
	if (j.contains("triggerbot_hitchance")) j.at("triggerbot_hitchance").get_to(g.triggerbot_hitchance);
	if (j.contains("triggerbot_delay")) j.at("triggerbot_delay").get_to(g.triggerbot_delay);
	if (j.contains("triggerbot_randomize_ms")) j.at("triggerbot_randomize_ms").get_to(g.triggerbot_randomize_ms);
	if (j.contains("triggerbot_outlier_chance")) j.at("triggerbot_outlier_chance").get_to(g.triggerbot_outlier_chance);
	if (j.contains("triggerbot_outlier_delay_ms")) j.at("triggerbot_outlier_delay_ms").get_to(g.triggerbot_outlier_delay_ms);
	if (j.contains("triggerbot_delay_after_ms")) j.at("triggerbot_delay_after_ms").get_to(g.triggerbot_delay_after_ms);
	if (j.contains("triggerbot_lethal_only")) j.at("triggerbot_lethal_only").get_to(g.triggerbot_lethal_only);
	g.triggerbot_randomize_ms = std::clamp(g.triggerbot_randomize_ms, 0, 100);
	g.triggerbot_outlier_chance = std::clamp(g.triggerbot_outlier_chance, 0.0f, 100.0f);
	g.triggerbot_outlier_delay_ms = std::clamp(g.triggerbot_outlier_delay_ms, 0, 500);
	g.triggerbot_delay_after_ms = std::clamp(g.triggerbot_delay_after_ms, 0, 1000);
	if (j.contains("triggerbot_reaction_time")) j.at("triggerbot_reaction_time").get_to(g.triggerbot_reaction_time);
	if (legacy_simple_mode)
	{
		g.triggerbot_seed_type = combat_profile::seed_mode::none;
		g.triggerbot_hitchance = 0.0f;
		g.triggerbot_delay = std::clamp(
			j.value("triggerbot_simple_delay", g.triggerbot_delay), 0, 500);
	}
	if (j.contains("triggerbot_min_damage")) j.at("triggerbot_min_damage").get_to(g.triggerbot_min_damage);
	if (j.contains("triggerbot_min_damage_override_enabled")) j.at("triggerbot_min_damage_override_enabled").get_to(g.triggerbot_min_damage_override_enabled);
	if (j.contains("triggerbot_min_damage_override")) j.at("triggerbot_min_damage_override").get_to(g.triggerbot_min_damage_override);
	if (j.contains("triggerbot_min_damage_override_mode")) j.at("triggerbot_min_damage_override_mode").get_to(g.triggerbot_min_damage_override_mode);
	if (j.contains("triggerbot_min_damage_override_key")) j.at("triggerbot_min_damage_override_key").get_to(g.triggerbot_min_damage_override_key);
	g.triggerbot_min_damage_override = std::clamp(g.triggerbot_min_damage_override, 1.0f, 100.0f);
	g.triggerbot_min_damage_override_mode = std::clamp(g.triggerbot_min_damage_override_mode,
		static_cast<int>(combat_profile::activation::hold),
		static_cast<int>(combat_profile::activation::toggle));
	g.triggerbot_min_damage_override_key = std::clamp(g.triggerbot_min_damage_override_key, 0, 255);
	if (j.contains("triggerbot_autowall")) j.at("triggerbot_autowall").get_to(g.triggerbot_autowall);
	if (j.contains("triggerbot_autostop")) j.at("triggerbot_autostop").get_to(g.triggerbot_autostop);
	if (j.value("triggerbot_early_autostop", false)) g.triggerbot_autostop = true;
	if (j.contains("triggerbot_predictive")) j.at("triggerbot_predictive").get_to(g.triggerbot_predictive);
	if (j.contains("triggerbot_revolver_pre_cock")) j.at("triggerbot_revolver_pre_cock").get_to(g.triggerbot_revolver_pre_cock);
	if (j.contains("triggerbot_revolver_release_margin_ms")) j.at("triggerbot_revolver_release_margin_ms").get_to(g.triggerbot_revolver_release_margin_ms);
	g.triggerbot_revolver_release_margin_ms = std::clamp(g.triggerbot_revolver_release_margin_ms, 1, 60);
	if (j.contains("triggerbot_checks")) j.at("triggerbot_checks").get_to(g.triggerbot_checks);
	else g.triggerbot_checks.walls = g.triggerbot_autowall
		? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
	if (j.contains("penetration_crosshair")) j.at("penetration_crosshair").get_to(g.penetration_crosshair);
	if (j.contains("penetration_color_yes")) j.at("penetration_color_yes").get_to(g.penetration_color_yes);
	if (j.contains("penetration_color_no")) j.at("penetration_color_no").get_to(g.penetration_color_no);
}

static void to_json(json& j, const combat_profile::group_overrides& o)
{
	j = json{
		{"use_global", o.use_global},
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
		{"triggerbot_checks", o.triggerbot_checks}
	};
}

static void from_json(const json& j, combat_profile::group_overrides& o)
{
	if (j.contains("use_global")) j.at("use_global").get_to(o.use_global);
	if (j.contains("aimbot_fov")) j.at("aimbot_fov").get_to(o.aimbot_fov);
	if (j.contains("aimbot_smoothing")) j.at("aimbot_smoothing").get_to(o.aimbot_smoothing);
	if (j.contains("aimbot_humanize")) j.at("aimbot_humanize").get_to(o.aimbot_humanize);
	if (j.contains("aimbot_autowall")) j.at("aimbot_autowall").get_to(o.aimbot_autowall);
	if (j.contains("aimbot_min_damage")) j.at("aimbot_min_damage").get_to(o.aimbot_min_damage);
	if (j.contains("aimbot_lethal_only")) j.at("aimbot_lethal_only").get_to(o.aimbot_lethal_only);
	if (j.contains("aimbot_hitbox_parts")) j.at("aimbot_hitbox_parts").get_to(o.aimbot_hitbox_parts);
	if (j.contains("aimbot_multipoint")) j.at("aimbot_multipoint").get_to(o.aimbot_multipoint);
	if (j.contains("aimbot_visible_only")) j.at("aimbot_visible_only").get_to(o.aimbot_visible_only);
	if (j.contains("aimbot_predictive")) j.at("aimbot_predictive").get_to(o.aimbot_predictive);
	if (j.contains("aimbot_recoil_sync")) j.at("aimbot_recoil_sync").get_to(o.aimbot_recoil_sync);
	if (j.contains("aimbot_checks")) j.at("aimbot_checks").get_to(o.aimbot_checks);
	else if (o.aimbot_visible_only)
		o.aimbot_checks.walls = o.aimbot_autowall
			? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
	else o.aimbot_checks.walls = combat_profile::wall_policy::penetration;
	if (j.contains("aimbot_humanizer")) j.at("aimbot_humanizer").get_to(o.aimbot_humanizer);
	if (j.contains("aimbot_multipoint_config")) j.at("aimbot_multipoint_config").get_to(o.aimbot_multipoint_config);
	if (j.contains("aimbot_prediction")) j.at("aimbot_prediction").get_to(o.aimbot_prediction);
	else o.aimbot_prediction.enabled = o.aimbot_predictive;
	if (j.contains("aimbot_rcs")) j.at("aimbot_rcs").get_to(o.aimbot_rcs);
	else o.aimbot_rcs.enabled = o.aimbot_recoil_sync;
	if (j.contains("aimbot_fov_config")) j.at("aimbot_fov_config").get_to(o.aimbot_fov_config);
	else o.aimbot_fov_config.far_fov = static_cast<float>(o.aimbot_fov);
	const auto legacy_simple_mode = j.value("triggerbot_mode", 0) == 1;
	if (j.contains("triggerbot_seed_type"))
		j.at("triggerbot_seed_type").get_to(o.triggerbot_seed_type);
	else if (j.contains("triggerbot_seed"))
		o.triggerbot_seed_type = j.at("triggerbot_seed").get<bool>() ? combat_profile::seed_mode::unrestricted : combat_profile::seed_mode::none;
	o.triggerbot_seed_type = std::clamp(o.triggerbot_seed_type,
		static_cast<int>(combat_profile::seed_mode::none), static_cast<int>(combat_profile::seed_mode::unrestricted));
	if (j.contains("triggerbot_hitbox_parts")) j.at("triggerbot_hitbox_parts").get_to(o.triggerbot_hitbox_parts);
	o.triggerbot_hitbox_parts &= combat_profile::aim_part::all;
	if (j.contains("triggerbot_hitchance")) j.at("triggerbot_hitchance").get_to(o.triggerbot_hitchance);
	if (j.contains("triggerbot_delay")) j.at("triggerbot_delay").get_to(o.triggerbot_delay);
	if (j.contains("triggerbot_randomize_ms")) j.at("triggerbot_randomize_ms").get_to(o.triggerbot_randomize_ms);
	if (j.contains("triggerbot_outlier_chance")) j.at("triggerbot_outlier_chance").get_to(o.triggerbot_outlier_chance);
	if (j.contains("triggerbot_outlier_delay_ms")) j.at("triggerbot_outlier_delay_ms").get_to(o.triggerbot_outlier_delay_ms);
	if (j.contains("triggerbot_delay_after_ms")) j.at("triggerbot_delay_after_ms").get_to(o.triggerbot_delay_after_ms);
	if (j.contains("triggerbot_lethal_only")) j.at("triggerbot_lethal_only").get_to(o.triggerbot_lethal_only);
	o.triggerbot_randomize_ms = std::clamp(o.triggerbot_randomize_ms, 0, 100);
	o.triggerbot_outlier_chance = std::clamp(o.triggerbot_outlier_chance, 0.0f, 100.0f);
	o.triggerbot_outlier_delay_ms = std::clamp(o.triggerbot_outlier_delay_ms, 0, 500);
	o.triggerbot_delay_after_ms = std::clamp(o.triggerbot_delay_after_ms, 0, 1000);
	if (j.contains("triggerbot_reaction_time")) j.at("triggerbot_reaction_time").get_to(o.triggerbot_reaction_time);
	if (legacy_simple_mode)
	{
		o.triggerbot_seed_type = combat_profile::seed_mode::none;
		o.triggerbot_hitchance = 0.0f;
		o.triggerbot_delay = std::clamp(
			j.value("triggerbot_simple_delay", o.triggerbot_delay), 0, 500);
	}
	if (j.contains("triggerbot_min_damage")) j.at("triggerbot_min_damage").get_to(o.triggerbot_min_damage);
	if (j.contains("triggerbot_autowall")) j.at("triggerbot_autowall").get_to(o.triggerbot_autowall);
	if (j.contains("triggerbot_autostop")) j.at("triggerbot_autostop").get_to(o.triggerbot_autostop);
	if (j.value("triggerbot_early_autostop", false)) o.triggerbot_autostop = true;
	if (j.contains("triggerbot_predictive")) j.at("triggerbot_predictive").get_to(o.triggerbot_predictive);
	if (j.contains("triggerbot_revolver_pre_cock")) j.at("triggerbot_revolver_pre_cock").get_to(o.triggerbot_revolver_pre_cock);
	if (j.contains("triggerbot_revolver_release_margin_ms")) j.at("triggerbot_revolver_release_margin_ms").get_to(o.triggerbot_revolver_release_margin_ms);
	o.triggerbot_revolver_release_margin_ms = std::clamp(o.triggerbot_revolver_release_margin_ms, 1, 60);
	if (j.contains("triggerbot_checks")) j.at("triggerbot_checks").get_to(o.triggerbot_checks);
	else o.triggerbot_checks.walls = o.triggerbot_autowall
		? combat_profile::wall_policy::penetration : combat_profile::wall_policy::block;
}

static void to_json(json& j, const visual_profile::player::box& b)
{
	j = json{
		{"enabled", b.enabled},
		{"style", static_cast<int>(b.style)},
		{"fill", b.fill},
		{"outline", b.outline},
		{"corner_length", b.corner_length},
		{"visible_color", b.visible_color},
		{"occluded_color", b.occluded_color},
		{"fill_visible_color", b.fill_visible_color},
		{"fill_occluded_color", b.fill_occluded_color}
	};
}

static void from_json(const json& j, visual_profile::player::box& b)
{
	if (j.contains("enabled")) j.at("enabled").get_to(b.enabled);
	if (j.contains("style")) j.at("style").get_to(b.style);
	if (j.contains("fill")) j.at("fill").get_to(b.fill);
	if (j.contains("outline")) j.at("outline").get_to(b.outline);
	if (j.contains("corner_length")) j.at("corner_length").get_to(b.corner_length);
	if (j.contains("visible_color")) j.at("visible_color").get_to(b.visible_color);
	if (j.contains("occluded_color")) j.at("occluded_color").get_to(b.occluded_color);
	if (j.contains("fill_visible_color")) j.at("fill_visible_color").get_to(b.fill_visible_color);
	if (j.contains("fill_occluded_color")) j.at("fill_occluded_color").get_to(b.fill_occluded_color);
}

static void to_json(json& j, const visual_profile::player::skeleton& s)
{
	j = json{
		{"enabled", s.enabled},
		{"thickness", s.thickness},
		{"visible_color", s.visible_color},
		{"occluded_color", s.occluded_color}
	};
}

static void from_json(const json& j, visual_profile::player::skeleton& s)
{
	if (j.contains("enabled")) j.at("enabled").get_to(s.enabled);
	if (j.contains("thickness")) j.at("thickness").get_to(s.thickness);
	if (j.contains("visible_color")) j.at("visible_color").get_to(s.visible_color);
	if (j.contains("occluded_color")) j.at("occluded_color").get_to(s.occluded_color);
}

static void to_json(json& j, const visual_profile::player::hitboxes& h)
{
	j = json{
		{"enabled", h.enabled},
		{"visible_color", h.visible_color},
		{"occluded_color", h.occluded_color},
		{"fill", h.fill},
		{"outline", h.outline}
	};
}

static void from_json(const json& j, visual_profile::player::hitboxes& h)
{
	if (j.contains("enabled")) j.at("enabled").get_to(h.enabled);
	if (j.contains("visible_color")) j.at("visible_color").get_to(h.visible_color);
	if (j.contains("occluded_color")) j.at("occluded_color").get_to(h.occluded_color);
	if (j.contains("fill")) j.at("fill").get_to(h.fill);
	if (j.contains("outline")) j.at("outline").get_to(h.outline);
}

static void to_json(json& j, const visual_profile::player::threat_module& t)
{
	j = json{
		{"enabled", t.enabled},
		{"max_distance", t.max_distance},
		{"head_hitbox", t.head_hitbox},
		{"body_hitbox", t.body_hitbox},
		{"limb_hitbox", t.limb_hitbox},
		{"fill_alpha", t.fill_alpha},
		{"outline_alpha", t.outline_alpha},
		{"outline_thickness", t.outline_thickness},
		{"head_color", t.head_color},
		{"body_color", t.body_color},
		{"limb_color", t.limb_color}
	};
}

static void from_json(const json& j, visual_profile::player::threat_module& t)
{
	if (j.contains("enabled")) j.at("enabled").get_to(t.enabled);
	if (j.contains("max_distance")) j.at("max_distance").get_to(t.max_distance);
	if (j.contains("head_hitbox")) j.at("head_hitbox").get_to(t.head_hitbox);
	if (j.contains("body_hitbox")) j.at("body_hitbox").get_to(t.body_hitbox);
	if (j.contains("limb_hitbox")) j.at("limb_hitbox").get_to(t.limb_hitbox);
	if (j.contains("fill_alpha")) j.at("fill_alpha").get_to(t.fill_alpha);
	if (j.contains("outline_alpha")) j.at("outline_alpha").get_to(t.outline_alpha);
	if (j.contains("outline_thickness")) j.at("outline_thickness").get_to(t.outline_thickness);
	if (j.contains("head_color")) j.at("head_color").get_to(t.head_color);
	if (j.contains("body_color")) j.at("body_color").get_to(t.body_color);
	if (j.contains("limb_color")) j.at("limb_color").get_to(t.limb_color);
}

static void to_json(json& j, const visual_profile::player::health_bar& h)
{
	j = json{
		{"enabled", h.enabled},
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
		{"text_color", h.text_color}
	};
}

static void from_json(const json& j, visual_profile::player::health_bar& h)
{
	if (j.contains("enabled")) j.at("enabled").get_to(h.enabled);
	if (j.contains("position")) j.at("position").get_to(h.position);
	if (j.contains("thickness")) j.at("thickness").get_to(h.thickness);
	if (j.contains("outline")) j.at("outline").get_to(h.outline);
	if (j.contains("outline_thickness")) j.at("outline_thickness").get_to(h.outline_thickness);
	if (j.contains("gradient")) j.at("gradient").get_to(h.gradient);
	if (j.contains("show_value")) j.at("show_value").get_to(h.show_value);
	if (j.contains("segments")) j.at("segments").get_to(h.segments);
	if (j.contains("segment_gap")) j.at("segment_gap").get_to(h.segment_gap);
	if (j.contains("full_color")) j.at("full_color").get_to(h.full_color);
	if (j.contains("low_color")) j.at("low_color").get_to(h.low_color);
	if (j.contains("background_color")) j.at("background_color").get_to(h.background_color);
	if (j.contains("outline_color")) j.at("outline_color").get_to(h.outline_color);
	if (j.contains("text_color")) j.at("text_color").get_to(h.text_color);
	h.thickness = std::clamp(h.thickness, 1.0f, 12.0f);
	h.outline_thickness = std::clamp(h.outline_thickness, 0.5f, 4.0f);
	h.segments = std::clamp(h.segments, 1, 10);
	h.segment_gap = std::clamp(h.segment_gap, 0.0f, 4.0f);
}

static void to_json(json& j, const combat_profile::legit_checks& value)
{
	j = json{{"airborne", value.airborne}, {"flashed", value.flashed},
		{"smoke", value.smoke}, {"flash_threshold", value.flash_threshold},
		{"flash_threshold_percent", value.flash_threshold},
		{"walls", value.walls}};
}

static void from_json(const json& j, combat_profile::legit_checks& value)
{
	if (j.contains("airborne")) j.at("airborne").get_to(value.airborne);
	if (j.contains("flashed")) j.at("flashed").get_to(value.flashed);
	if (j.contains("smoke")) j.at("smoke").get_to(value.smoke);
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
	if (j.contains("walls")) j.at("walls").get_to(value.walls);
	value.flash_threshold = std::clamp(value.flash_threshold, 0.0f, 100.0f);
	value.walls = std::clamp(value.walls,
		static_cast<int>(combat_profile::wall_policy::block),
		static_cast<int>(combat_profile::wall_policy::ignore));
	if (value.walls == combat_profile::wall_policy::ignore)
		value.walls = combat_profile::wall_policy::penetration;
}

static void to_json(json& j, const combat_profile::humanizer_settings& value)
{
	j = json{{"gravity", value.gravity}, {"wind", value.wind},
		{"max_step", value.max_step}, {"damping", value.damping},
		{"reaction_min_ms", value.reaction_min_ms},
		{"reaction_max_ms", value.reaction_max_ms}, {"curve", value.curve},
		{"overshoot_chance", value.overshoot_chance},
		{"overshoot_amount", value.overshoot_amount}, {"jitter", value.jitter},
		{"deadzone", value.deadzone}};
}

static void from_json(const json& j, combat_profile::humanizer_settings& value)
{
	if (j.contains("gravity")) j.at("gravity").get_to(value.gravity);
	if (j.contains("wind")) j.at("wind").get_to(value.wind);
	if (j.contains("max_step")) j.at("max_step").get_to(value.max_step);
	if (j.contains("damping")) j.at("damping").get_to(value.damping);
	if (j.contains("reaction_min_ms")) j.at("reaction_min_ms").get_to(value.reaction_min_ms);
	if (j.contains("reaction_max_ms")) j.at("reaction_max_ms").get_to(value.reaction_max_ms);
	if (j.contains("curve")) j.at("curve").get_to(value.curve);
	if (j.contains("overshoot_chance")) j.at("overshoot_chance").get_to(value.overshoot_chance);
	if (j.contains("overshoot_amount")) j.at("overshoot_amount").get_to(value.overshoot_amount);
	if (j.contains("jitter")) j.at("jitter").get_to(value.jitter);
	if (j.contains("deadzone")) j.at("deadzone").get_to(value.deadzone);
	value.gravity = std::clamp(value.gravity, 0.0f, 20.0f);
	value.wind = std::clamp(value.wind, 0.0f, 20.0f);
	value.max_step = std::clamp(value.max_step, 1.0f, 90.0f);
	value.damping = std::clamp(value.damping, 0.0f, 1.0f);
	value.reaction_min_ms = std::clamp(value.reaction_min_ms, 0, 500);
	value.reaction_max_ms = std::clamp(value.reaction_max_ms,
		value.reaction_min_ms, 750);
	value.curve = std::clamp(value.curve, 0.0f, 1.0f);
	value.overshoot_chance = std::clamp(value.overshoot_chance, 0.0f, 100.0f);
	value.overshoot_amount = std::clamp(value.overshoot_amount, 0.0f, 1.0f);
	value.jitter = std::clamp(value.jitter, 0.0f, 3.0f);
	value.deadzone = std::clamp(value.deadzone, 0.0f, 2.0f);
}

static void to_json(json& j, const combat_profile::multipoint_settings& value)
{
	j = json{{"caps", value.caps}, {"sides", value.sides},
		{"head_scale", value.head_scale}, {"body_scale", value.body_scale},
		{"limb_scale", value.limb_scale}};
}

static void from_json(const json& j, combat_profile::multipoint_settings& value)
{
	if (j.contains("caps")) j.at("caps").get_to(value.caps);
	if (j.contains("sides")) j.at("sides").get_to(value.sides);
	if (j.contains("head_scale")) j.at("head_scale").get_to(value.head_scale);
	if (j.contains("body_scale")) j.at("body_scale").get_to(value.body_scale);
	if (j.contains("limb_scale")) j.at("limb_scale").get_to(value.limb_scale);
	value.head_scale = std::clamp(value.head_scale, 0.0f, 0.95f);
	value.body_scale = std::clamp(value.body_scale, 0.0f, 0.95f);
	value.limb_scale = std::clamp(value.limb_scale, 0.0f, 0.95f);
}

static void to_json(json& j, const combat_profile::prediction_settings& value)
{
	j = json{{"enabled", value.enabled}, {"max_horizon_ms", value.max_horizon_ms},
		{"acceleration", value.acceleration}};
}

static void from_json(const json& j, combat_profile::prediction_settings& value)
{
	if (j.contains("enabled")) j.at("enabled").get_to(value.enabled);
	if (j.contains("max_horizon_ms")) j.at("max_horizon_ms").get_to(value.max_horizon_ms);
	if (j.contains("acceleration")) j.at("acceleration").get_to(value.acceleration);
	value.max_horizon_ms = std::clamp(value.max_horizon_ms, 0.0f, 120.0f);
}

static void to_json(json& j, const combat_profile::rcs_settings& value)
{
	j = json{{"enabled", value.enabled}, {"start_bullet", value.start_bullet},
		{"pitch", value.pitch}, {"yaw", value.yaw},
		{"response_ms", value.response_ms}, {"randomness", value.randomness},
		{"drift", value.drift}};
}

static void from_json(const json& j, combat_profile::rcs_settings& value)
{
	if (j.contains("enabled")) j.at("enabled").get_to(value.enabled);
	if (j.contains("start_bullet")) j.at("start_bullet").get_to(value.start_bullet);
	if (j.contains("pitch")) j.at("pitch").get_to(value.pitch);
	if (j.contains("yaw")) j.at("yaw").get_to(value.yaw);
	if (j.contains("response_ms")) j.at("response_ms").get_to(value.response_ms);
	if (j.contains("randomness")) j.at("randomness").get_to(value.randomness);
	if (j.contains("drift")) j.at("drift").get_to(value.drift);
	value.start_bullet = std::clamp(value.start_bullet, 1, 10);
	value.pitch = std::clamp(value.pitch, 0.0f, 200.0f);
	value.yaw = std::clamp(value.yaw, 0.0f, 200.0f);
	value.response_ms = std::clamp(value.response_ms, 1.0f, 150.0f);
	value.randomness = std::clamp(value.randomness, 0.0f, 30.0f);
	value.drift = std::clamp(value.drift, 0.0f, 30.0f);
}

static void to_json(json& j, const combat_profile::fov_settings& value)
{
	j = json{{"selection", value.selection}, {"visualization", value.visualization},
		{"draw_target_point", value.selection == combat_profile::fov_settings::target_distance},
		{"near_distance_m", value.near_distance_m}, {"near_fov", value.near_fov},
		{"far_distance_m", value.far_distance_m}, {"far_fov", value.far_fov},
		{"distance_curve", value.distance_curve}};
}

static void from_json(const json& j, combat_profile::fov_settings& value)
{
	const auto has_distance_curve = j.contains("distance_curve");
	if (j.contains("selection")) j.at("selection").get_to(value.selection);
	if (j.contains("visualization")) j.at("visualization").get_to(value.visualization);
	bool legacy_target = value.visualization == combat_profile::fov_settings::target;
	if (j.contains("draw_target_point")) j.at("draw_target_point").get_to(legacy_target);
	if (legacy_target) value.selection = combat_profile::fov_settings::target_distance;
	if (j.contains("near_distance_m")) j.at("near_distance_m").get_to(value.near_distance_m);
	if (j.contains("near_fov")) j.at("near_fov").get_to(value.near_fov);
	if (j.contains("far_distance_m")) j.at("far_distance_m").get_to(value.far_distance_m);
	if (j.contains("far_fov")) j.at("far_fov").get_to(value.far_fov);
	if (j.contains("distance_curve")) j.at("distance_curve").get_to(value.distance_curve);

	if (!has_distance_curve && value.selection != combat_profile::fov_settings::fixed
		&& value.near_fov >= 40.0f)
	{
		value.near_distance_m = 1.5f;
		value.near_fov = 18.0f;
		value.far_distance_m = 45.0f;
		value.far_fov = 2.5f;
		value.distance_curve = 1.0f;
	}
	value.selection = std::clamp(value.selection,
		static_cast<int>(combat_profile::fov_settings::fixed),
		static_cast<int>(combat_profile::fov_settings::target_distance));
	value.visualization = std::clamp(value.visualization,
		static_cast<int>(combat_profile::fov_settings::center),
		static_cast<int>(combat_profile::fov_settings::target));
	value.near_distance_m = std::clamp(value.near_distance_m, 0.5f, 100.0f);
	value.far_distance_m = std::clamp(value.far_distance_m,
		value.near_distance_m + 0.5f, 150.0f);
	value.near_fov = std::clamp(value.near_fov, 0.25f, 180.0f);
	value.far_fov = std::clamp(value.far_fov, 0.25f, value.near_fov);
	value.distance_curve = std::clamp(value.distance_curve, 0.25f, 4.0f);
}

static void to_json(json& j, const visual_profile::player::armor_bar& a)
{
	j = json{
		{"enabled", a.enabled}, {"position", static_cast<int>(a.position)},
		{"thickness", a.thickness}, {"outline", a.outline},
		{"outline_thickness", a.outline_thickness}, {"gradient", a.gradient},
		{"show_value", a.show_value}, {"segments", a.segments},
		{"segment_gap", a.segment_gap}, {"full_color", a.full_color},
		{"low_color", a.low_color}, {"background_color", a.background_color},
		{"outline_color", a.outline_color}, {"text_color", a.text_color}
	};
}

static void from_json(const json& j, visual_profile::player::armor_bar& a)
{
	if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
	if (j.contains("position")) j.at("position").get_to(a.position);
	if (j.contains("thickness")) j.at("thickness").get_to(a.thickness);
	if (j.contains("outline")) j.at("outline").get_to(a.outline);
	if (j.contains("outline_thickness")) j.at("outline_thickness").get_to(a.outline_thickness);
	if (j.contains("gradient")) j.at("gradient").get_to(a.gradient);
	if (j.contains("show_value")) j.at("show_value").get_to(a.show_value);
	if (j.contains("segments")) j.at("segments").get_to(a.segments);
	if (j.contains("segment_gap")) j.at("segment_gap").get_to(a.segment_gap);
	if (j.contains("full_color")) j.at("full_color").get_to(a.full_color);
	if (j.contains("low_color")) j.at("low_color").get_to(a.low_color);
	if (j.contains("background_color")) j.at("background_color").get_to(a.background_color);
	if (j.contains("outline_color")) j.at("outline_color").get_to(a.outline_color);
	if (j.contains("text_color")) j.at("text_color").get_to(a.text_color);
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
	if (j.contains("color")) j.at("color").get_to(s.color);
	if (j.contains("scale")) j.at("scale").get_to(s.scale);
	s.scale = std::clamp(s.scale, 0.55f, 2.0f);
}

static void to_json(json& j, const visual_profile::player::info_flags& f)
{
	j = json{
		{"enabled", f.enabled},
		{"flags", f.flags},
		{"money", f.money_style}, {"armor", f.armor_style},
		{"kit", f.kit_style}, {"scoped", f.scoped_style},
		{"defusing", f.defusing_style}, {"flashed", f.flashed_style},
		{"ping", f.ping_style}, {"distance", f.distance_style},
		{"bomb_damage", f.bomb_damage_style}
	};
}

static void from_json(const json& j, visual_profile::player::info_flags& f)
{
	if (j.contains("enabled")) j.at("enabled").get_to(f.enabled);
	if (j.contains("flags")) j.at("flags").get_to(f.flags);
	const auto load_style = [&](const char* name, const char* legacy_color,
		visual_profile::player::info_flags::style& style)
	{
		if (j.contains(name)) j.at(name).get_to(style);
		else if (j.contains(legacy_color)) j.at(legacy_color).get_to(style.color);
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
	if (j.contains("enabled")) j.at("enabled").get_to(n.enabled);
	if (j.contains("color")) j.at("color").get_to(n.color);
}

static void to_json(json& j, const visual_profile::player::weapon& w)
{
	j = json{
		{"enabled", w.enabled},
		{"display", static_cast<int>(w.display)},
		{"text_color", w.text_color},
		{"icon_color", w.icon_color},
		{"ammo", {
			{"enabled", w.ammo.enabled},
			{"show_count", w.ammo.show_count},
			{"empty_color", w.ammo.empty_color}
		}}
	};
}

static void from_json(const json& j, visual_profile::player::weapon& w)
{
	if (j.contains("enabled")) j.at("enabled").get_to(w.enabled);
	if (j.contains("display")) j.at("display").get_to(w.display);
	if (j.contains("text_color")) j.at("text_color").get_to(w.text_color);
	if (j.contains("icon_color")) j.at("icon_color").get_to(w.icon_color);
	if (j.contains("ammo"))
	{
		const auto& ammo = j.at("ammo");
		if (ammo.contains("enabled")) ammo.at("enabled").get_to(w.ammo.enabled);
		if (ammo.contains("show_count")) ammo.at("show_count").get_to(w.ammo.show_count);
		if (ammo.contains("empty_color")) ammo.at("empty_color").get_to(w.ammo.empty_color);
	}
}

static void to_json(json& j, const visual_profile::player::head_circle& h)
{
	j = json{ {"enabled", h.enabled}, {"thickness", h.thickness}, {"color", h.color} };
}

static void from_json(const json& j, visual_profile::player::head_circle& h)
{
	if (j.contains("enabled")) j.at("enabled").get_to(h.enabled);
	if (j.contains("thickness")) j.at("thickness").get_to(h.thickness);
	if (j.contains("color")) j.at("color").get_to(h.color);
}

static void to_json(json& j, const visual_profile::player::view_line& v)
{
	j = json{ {"enabled", v.enabled}, {"length", v.length}, {"thickness", v.thickness}, {"color", v.color} };
}

static void from_json(const json& j, visual_profile::player::view_line& v)
{
	if (j.contains("enabled")) j.at("enabled").get_to(v.enabled);
	if (j.contains("length")) j.at("length").get_to(v.length);
	if (j.contains("thickness")) j.at("thickness").get_to(v.thickness);
	if (j.contains("color")) j.at("color").get_to(v.color);
}

static void to_json(json& j, const visual_profile::player::offscreen_arrows& a)
{
	j = json{ {"enabled", a.enabled}, {"size", a.size}, {"radius", a.radius},
		{"thickness", a.thickness}, {"color", a.color}, {"bloom", a.bloom},
		{"bloom_color", a.bloom_color}, {"bloom_radius", a.bloom_radius},
		{"bloom_speed", a.bloom_speed}, {"bloom_min_alpha", a.bloom_min_alpha},
		{"bloom_max_alpha", a.bloom_max_alpha} };
}

static void from_json(const json& j, visual_profile::player::offscreen_arrows& a)
{
	if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
	if (j.contains("size")) j.at("size").get_to(a.size);
	if (j.contains("radius")) j.at("radius").get_to(a.radius);
	if (j.contains("thickness")) j.at("thickness").get_to(a.thickness);
	if (j.contains("color")) j.at("color").get_to(a.color);
	if (j.contains("bloom")) j.at("bloom").get_to(a.bloom);
	else if (j.contains("pulse")) j.at("pulse").get_to(a.bloom);
	if (j.contains("bloom_color")) j.at("bloom_color").get_to(a.bloom_color);
	if (j.contains("bloom_radius")) j.at("bloom_radius").get_to(a.bloom_radius);
	if (j.contains("bloom_speed")) j.at("bloom_speed").get_to(a.bloom_speed);
	else if (j.contains("pulse_speed"))
	{
		float legacy_speed{};
		j.at("pulse_speed").get_to(legacy_speed);
		a.bloom_speed = legacy_speed * 0.4f;
	}
	if (j.contains("bloom_min_alpha")) j.at("bloom_min_alpha").get_to(a.bloom_min_alpha);
	if (j.contains("bloom_max_alpha")) j.at("bloom_max_alpha").get_to(a.bloom_max_alpha);
	a.bloom_radius = std::clamp(a.bloom_radius, 1.0f, 16.0f);
	a.bloom_speed = std::clamp(a.bloom_speed, 0.05f, 2.0f);
	a.bloom_min_alpha = std::clamp(a.bloom_min_alpha, 0.0f, 1.0f);
	a.bloom_max_alpha = std::clamp(a.bloom_max_alpha,
		a.bloom_min_alpha, 1.0f);
}

static void to_json(json& j, const visual_profile::player::layout_element& e)
{
	j = json{{"x", e.x}, {"y", e.y}, {"scale", e.scale}};
}

static void from_json(const json& j, visual_profile::player::layout_element& e)
{
	if (j.contains("x")) j.at("x").get_to(e.x);
	if (j.contains("y")) j.at("y").get_to(e.y);
	if (j.contains("scale")) j.at("scale").get_to(e.scale);
	e.x = std::clamp(e.x, -0.5f, 1.5f);
	e.y = std::clamp(e.y, -0.5f, 1.5f);
	e.scale = std::clamp(e.scale, 0.55f, 2.0f);
}

static void to_json(json& j, const visual_profile::player::editor_layout& l)
{
	j = json{
		{"name", l.name},
		{"weapon", l.weapon},
		{"health", l.health},
		{"armor", l.armor},
		{"flags", l.flags}
	};
}

static void from_json(const json& j, visual_profile::player::editor_layout& l)
{
	if (j.contains("name")) j.at("name").get_to(l.name);

	if (std::abs(l.name.x - 0.50f) < 0.0001f &&
		std::abs(l.name.y - ( -0.07f )) < 0.0001f &&
		std::abs(l.name.scale - 1.0f) < 0.0001f)
	{
		l.name.y = -0.025f;
	}
	if (j.contains("weapon")) j.at("weapon").get_to(l.weapon);
	if (j.contains("health")) j.at("health").get_to(l.health);

	if (j.contains("ammo")) j.at("ammo").get_to(l.armor);
	else if (j.contains("armor")) j.at("armor").get_to(l.armor);
	if (j.contains("flags")) j.at("flags").get_to(l.flags);
}

static void to_json(json& j, const visual_profile::player& p)
{
	j = json{
		{"enabled", p.enabled},
		{"activation_mode", p.activation_mode},
		{"activation_key", p.activation_key},
		{"spectator_sync", p.spectator_sync},
		{"m_legit_sync", json{
			{"enabled", p.m_legit_sync.enabled},
			{"direct_visible", p.m_legit_sync.direct_visible},
			{"radar", p.m_legit_sync.radar},
			{"sound", p.m_legit_sync.sound},
			{"radar_hold", p.m_legit_sync.radar_hold},
			{"sound_hold", p.m_legit_sync.sound_hold},
			{"sound_distance", p.m_legit_sync.sound_distance}}},
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
		{"m_weapon", p.m_weapon}
	};
}

static void from_json(const json& j, visual_profile::player& p)
{
	if (j.contains("enabled")) j.at("enabled").get_to(p.enabled);
	if (j.contains("activation_mode")) j.at("activation_mode").get_to(p.activation_mode);
	if (j.contains("activation_key")) j.at("activation_key").get_to(p.activation_key);
	p.activation_mode = std::clamp(p.activation_mode,
		static_cast<int>(visual_profile::player::always_on),
		static_cast<int>(visual_profile::player::toggle));
	p.activation_key = std::clamp(p.activation_key, 0, 255);
	if (j.contains("spectator_sync")) j.at("spectator_sync").get_to(p.spectator_sync);
	if (j.contains("m_legit_sync"))
	{
		const auto& l = j.at("m_legit_sync");
		if (l.contains("enabled")) l.at("enabled").get_to(p.m_legit_sync.enabled);
		if (l.contains("direct_visible")) l.at("direct_visible").get_to(p.m_legit_sync.direct_visible);
		if (l.contains("radar")) l.at("radar").get_to(p.m_legit_sync.radar);
		if (l.contains("sound")) l.at("sound").get_to(p.m_legit_sync.sound);

		if (!j.contains("spectator_sync") && l.contains("spectator_sync"))
			l.at("spectator_sync").get_to(p.spectator_sync);
		if (l.contains("radar_hold")) l.at("radar_hold").get_to(p.m_legit_sync.radar_hold);
		if (l.contains("sound_hold")) l.at("sound_hold").get_to(p.m_legit_sync.sound_hold);
		if (l.contains("sound_distance")) l.at("sound_distance").get_to(p.m_legit_sync.sound_distance);
	}
	p.m_legit_sync.radar_hold = std::clamp(p.m_legit_sync.radar_hold, 0.1f, 10.0f);
	p.m_legit_sync.sound_hold = std::clamp(p.m_legit_sync.sound_hold, 0.1f, 10.0f);
	p.m_legit_sync.sound_distance = std::clamp(p.m_legit_sync.sound_distance, 100.0f, 2500.0f);
	if (j.contains("m_layout")) j.at("m_layout").get_to(p.m_layout);
	if (j.contains("m_box")) j.at("m_box").get_to(p.m_box);
	if (j.contains("m_skeleton")) j.at("m_skeleton").get_to(p.m_skeleton);
	if (j.contains("m_head_circle")) j.at("m_head_circle").get_to(p.m_head_circle);
	if (j.contains("m_view_line")) j.at("m_view_line").get_to(p.m_view_line);
	if (j.contains("m_offscreen_arrows")) j.at("m_offscreen_arrows").get_to(p.m_offscreen_arrows);
	if (j.contains("m_hitboxes")) j.at("m_hitboxes").get_to(p.m_hitboxes);
	if (j.contains("m_threat_module")) j.at("m_threat_module").get_to(p.m_threat_module);
	if (j.contains("m_health_bar")) j.at("m_health_bar").get_to(p.m_health_bar);
	if (j.contains("m_ammo_bar")) j.at("m_ammo_bar").get_to(p.m_armor_bar);
	else if (j.contains("m_armor_bar")) j.at("m_armor_bar").get_to(p.m_armor_bar);
	if (j.contains("m_info_flags")) j.at("m_info_flags").get_to(p.m_info_flags);
	if (j.contains("m_name")) j.at("m_name").get_to(p.m_name);
	if (j.contains("m_weapon")) j.at("m_weapon").get_to(p.m_weapon);

	if (j.contains("m_bomb_damage") &&
		(!j.contains("m_info_flags") || !j.at("m_info_flags").contains("bomb_damage")))
	{
		const auto& legacy = j.at("m_bomb_damage");
		bool enabled{ true };
		if (legacy.contains("enabled")) legacy.at("enabled").get_to(enabled);
		if (enabled) p.m_info_flags.flags |= visual_profile::player::info_flags::flag::bomb_damage;
		else p.m_info_flags.flags &= ~visual_profile::player::info_flags::flag::bomb_damage;
		if (legacy.contains("color")) legacy.at("color").get_to(p.m_info_flags.bomb_damage_style.color);
	}
}

static void to_json(json& j, const visual_profile::item::icon& i)
{
	j = json{{"enabled", i.enabled}, {"color", i.color}};
}

static void from_json(const json& j, visual_profile::item::icon& i)
{
	if (j.contains("enabled")) j.at("enabled").get_to(i.enabled);
	if (j.contains("color")) j.at("color").get_to(i.color);
}

static void to_json(json& j, const visual_profile::item::name& n)
{
	j = json{{"enabled", n.enabled}, {"color", n.color}};
}

static void from_json(const json& j, visual_profile::item::name& n)
{
	if (j.contains("enabled")) j.at("enabled").get_to(n.enabled);
	if (j.contains("color")) j.at("color").get_to(n.color);
}

static void to_json(json& j, const visual_profile::item::ammo& a)
{
	j = json{{"enabled", a.enabled}, {"color", a.color}, {"empty_color", a.empty_color}};
}

static void from_json(const json& j, visual_profile::item::ammo& a)
{
	if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
	if (j.contains("color")) j.at("color").get_to(a.color);
	if (j.contains("empty_color")) j.at("empty_color").get_to(a.empty_color);
}

static void to_json(json& j, const visual_profile::item::filters& f)
{
	j = json{
		{"rifles", f.rifles},
		{"smgs", f.smgs},
		{"shotguns", f.shotguns},
		{"snipers", f.snipers},
		{"pistols", f.pistols},
		{"heavy", f.heavy},
		{"grenades", f.grenades},
		{"utility", f.utility}
	};
}

static void from_json(const json& j, visual_profile::item::filters& f)
{
	if (j.contains("rifles")) j.at("rifles").get_to(f.rifles);
	if (j.contains("smgs")) j.at("smgs").get_to(f.smgs);
	if (j.contains("shotguns")) j.at("shotguns").get_to(f.shotguns);
	if (j.contains("snipers")) j.at("snipers").get_to(f.snipers);
	if (j.contains("pistols")) j.at("pistols").get_to(f.pistols);
	if (j.contains("heavy")) j.at("heavy").get_to(f.heavy);
	if (j.contains("grenades")) j.at("grenades").get_to(f.grenades);
	if (j.contains("utility")) j.at("utility").get_to(f.utility);
}

static void to_json(json& j, const visual_profile::item& i)
{
	j = json{
		{"enabled", i.enabled},
		{"max_distance", i.max_distance},
		{"m_icon", i.m_icon},
		{"m_name", i.m_name},
		{"m_ammo", i.m_ammo},
		{"m_filters", i.m_filters}
	};
}

static void from_json(const json& j, visual_profile::item& i)
{
	if (j.contains("enabled")) j.at("enabled").get_to(i.enabled);
	if (j.contains("max_distance")) j.at("max_distance").get_to(i.max_distance);
	if (j.contains("m_icon")) j.at("m_icon").get_to(i.m_icon);
	if (j.contains("m_name")) j.at("m_name").get_to(i.m_name);
	if (j.contains("m_ammo")) j.at("m_ammo").get_to(i.m_ammo);
	if (j.contains("m_filters")) j.at("m_filters").get_to(i.m_filters);
}

static void to_json(json& j, const visual_profile::projectile& p)
{
	j = json{
		{"enabled", p.enabled},
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
		{"inferno_gradient_opacity", p.inferno_gradient_opacity}
	};
}

static void from_json(const json& j, visual_profile::projectile& p)
{
	if (j.contains("enabled")) j.at("enabled").get_to(p.enabled);
	if (j.contains("show_icon")) j.at("show_icon").get_to(p.show_icon);
	if (j.contains("show_timer_ring")) j.at("show_timer_ring").get_to(p.show_timer_ring);
	else if (j.contains("show_timer_bar")) j.at("show_timer_bar").get_to(p.show_timer_ring);
	if (j.contains("show_inferno_bounds")) j.at("show_inferno_bounds").get_to(p.show_inferno_bounds);
	if (j.contains("default_color")) j.at("default_color").get_to(p.default_color);
	if (j.contains("color_he")) j.at("color_he").get_to(p.color_he);
	if (j.contains("color_flash")) j.at("color_flash").get_to(p.color_flash);
	if (j.contains("color_smoke")) j.at("color_smoke").get_to(p.color_smoke);
	if (j.contains("color_molotov")) j.at("color_molotov").get_to(p.color_molotov);
	if (j.contains("color_decoy")) j.at("color_decoy").get_to(p.color_decoy);
	if (j.contains("timer_high_color")) j.at("timer_high_color").get_to(p.timer_high_color);
	if (j.contains("timer_low_color")) j.at("timer_low_color").get_to(p.timer_low_color);
	if (j.contains("indicator_background")) j.at("indicator_background").get_to(p.indicator_background);
	else if (j.contains("bar_background")) j.at("bar_background").get_to(p.indicator_background);
	if (j.contains("inferno_gradient_width")) j.at("inferno_gradient_width").get_to(p.inferno_gradient_width);
	if (j.contains("inferno_gradient_opacity")) j.at("inferno_gradient_opacity").get_to(p.inferno_gradient_opacity);
}

static void to_json(json& j, const visual_profile::bomb& b)
{
	j = json{
		{"enabled", b.enabled},
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
		{"safe_zone_draw_radius", b.safe_zone_draw_radius}
	};
}

static void from_json(const json& j, visual_profile::bomb& b)
{
	if (j.contains("enabled")) j.at("enabled").get_to(b.enabled);
	if (j.contains("show_active_bomb")) j.at("show_active_bomb").get_to(b.show_active_bomb);
	if (j.contains("active_bomb_color")) j.at("active_bomb_color").get_to(b.active_bomb_color);
	if (j.contains("show_planted_bomb")) j.at("show_planted_bomb").get_to(b.show_planted_bomb);
	if (j.contains("bomb_color_t")) j.at("bomb_color_t").get_to(b.bomb_color_t);
	if (j.contains("bomb_color_ct")) j.at("bomb_color_ct").get_to(b.bomb_color_ct);
	if (j.contains("show_timer")) j.at("show_timer").get_to(b.show_timer);
	if (j.contains("timer_text_color")) j.at("timer_text_color").get_to(b.timer_text_color);
	if (j.contains("show_info_panel")) j.at("show_info_panel").get_to(b.show_info_panel);
	if (j.contains("panel_background")) j.at("panel_background").get_to(b.panel_background);
	if (j.contains("show_safe_zone")) j.at("show_safe_zone").get_to(b.show_safe_zone);
	if (j.contains("safe_zone_color")) j.at("safe_zone_color").get_to(b.safe_zone_color);
	if (j.contains("safe_zone_bands")) j.at("safe_zone_bands").get_to(b.safe_zone_bands);
	if (j.contains("safe_zone_band_step")) j.at("safe_zone_band_step").get_to(b.safe_zone_band_step);
	if (j.contains("safe_zone_draw_radius")) j.at("safe_zone_draw_radius").get_to(b.safe_zone_draw_radius);
	b.safe_zone_draw_radius = std::clamp(b.safe_zone_draw_radius, 200.0f, 2500.0f);

	if (b.safe_zone_bands == 4 && std::abs(b.safe_zone_band_step - 14.0f) < 0.01f)
	{
		b.safe_zone_bands = 1;
		b.safe_zone_band_step = 4.0f;
		if (b.safe_zone_color.r == 120 && b.safe_zone_color.g == 230 &&
			b.safe_zone_color.b == 140 && b.safe_zone_color.a == 230)
		{
			b.safe_zone_color = {70, 235, 105, 240};
		}
	}
}

static void to_json(json& j, const visual_profile::radar& r)
{
	j = json{{"enabled", r.enabled}, {"activation_mode", r.activation_mode},
		{"activation_key", r.activation_key}, {"show_names", r.show_names},
		{"show_health", r.show_health}, {"show_armor", r.show_armor},
		{"show_weapon", r.show_weapon}, {"show_projectiles", r.show_projectiles},
		{"show_trajectories", r.show_trajectories},
		{"show_grenade_zones", r.show_grenade_zones},
		{"information_scale", r.information_scale}, {"marker_scale", r.marker_scale},
		{"text_outline", r.text_outline}, {"text_outline_thickness", r.text_outline_thickness},
		{"text_outline_color", r.text_outline_color}, {"enemy_color", r.enemy_color},
		{"direction_color", r.direction_color}, {"name_color", r.name_color},
		{"weapon_color", r.weapon_color}, {"status_color", r.status_color},
		{"health_color", r.health_color}, {"armor_color", r.armor_color},
		{"he_color", r.he_color}, {"flash_color", r.flash_color},
		{"smoke_color", r.smoke_color}, {"molotov_color", r.molotov_color},
		{"decoy_color", r.decoy_color}, {"trajectory_thickness", r.trajectory_thickness},
		{"trajectory_endpoint_size", r.trajectory_endpoint_size},
		{"zone_fill_alpha", r.zone_fill_alpha}, {"zone_outline_alpha", r.zone_outline_alpha},
		{"zone_outline_thickness", r.zone_outline_thickness}};
}

static void from_json(const json& j, visual_profile::radar& r)
{
	if (j.contains("enabled")) j.at("enabled").get_to(r.enabled);
	if (j.contains("activation_mode")) j.at("activation_mode").get_to(r.activation_mode);
	if (j.contains("activation_key")) j.at("activation_key").get_to(r.activation_key);
	r.activation_mode = std::clamp(r.activation_mode,
		static_cast<int>(visual_profile::radar::always_on),
		static_cast<int>(visual_profile::radar::toggle));
	r.activation_key = std::clamp(r.activation_key, 0, 255);
	if (j.contains("show_names")) j.at("show_names").get_to(r.show_names);
	if (j.contains("show_health")) j.at("show_health").get_to(r.show_health);
	if (j.contains("show_armor")) j.at("show_armor").get_to(r.show_armor);
	if (j.contains("show_weapon")) j.at("show_weapon").get_to(r.show_weapon);
	if (j.contains("show_projectiles")) j.at("show_projectiles").get_to(r.show_projectiles);
	if (j.contains("show_trajectories")) j.at("show_trajectories").get_to(r.show_trajectories);
	if (j.contains("show_grenade_zones")) j.at("show_grenade_zones").get_to(r.show_grenade_zones);
	if (j.contains("information_scale")) j.at("information_scale").get_to(r.information_scale);
	if (j.contains("marker_scale")) j.at("marker_scale").get_to(r.marker_scale);
	if (j.contains("text_outline")) j.at("text_outline").get_to(r.text_outline);
	if (j.contains("text_outline_thickness")) j.at("text_outline_thickness").get_to(r.text_outline_thickness);
	if (j.contains("text_outline_color")) j.at("text_outline_color").get_to(r.text_outline_color);
	if (j.contains("enemy_color")) j.at("enemy_color").get_to(r.enemy_color);
	if (j.contains("direction_color")) j.at("direction_color").get_to(r.direction_color);
	if (j.contains("name_color")) j.at("name_color").get_to(r.name_color);
	if (j.contains("weapon_color")) j.at("weapon_color").get_to(r.weapon_color);
	if (j.contains("status_color")) j.at("status_color").get_to(r.status_color);
	if (j.contains("health_color")) j.at("health_color").get_to(r.health_color);
	if (j.contains("armor_color")) j.at("armor_color").get_to(r.armor_color);
	if (j.contains("he_color")) j.at("he_color").get_to(r.he_color);
	if (j.contains("flash_color")) j.at("flash_color").get_to(r.flash_color);
	if (j.contains("smoke_color")) j.at("smoke_color").get_to(r.smoke_color);
	if (j.contains("molotov_color")) j.at("molotov_color").get_to(r.molotov_color);
	if (j.contains("decoy_color")) j.at("decoy_color").get_to(r.decoy_color);
	if (j.contains("trajectory_thickness")) j.at("trajectory_thickness").get_to(r.trajectory_thickness);
	if (j.contains("trajectory_endpoint_size")) j.at("trajectory_endpoint_size").get_to(r.trajectory_endpoint_size);
	if (j.contains("zone_fill_alpha")) j.at("zone_fill_alpha").get_to(r.zone_fill_alpha);
	if (j.contains("zone_outline_alpha")) j.at("zone_outline_alpha").get_to(r.zone_outline_alpha);
	if (j.contains("zone_outline_thickness")) j.at("zone_outline_thickness").get_to(r.zone_outline_thickness);
	r.information_scale = std::isfinite(r.information_scale)
		? std::clamp(r.information_scale, 0.5f, 1.5f) : 0.75f;
	const auto finite = [](const float value, const float low, const float high,
		const float fallback) { return std::isfinite(value)
			? std::clamp(value, low, high) : fallback; };
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
	j = json{
		{"enabled", n.enabled},
		{"max_distance", n.max_distance},
		{"background_color", n.background_color},
		{"wireframe_color", n.wireframe_color}
	};
}

static void from_json(const json& j, visual_profile::no_flash& n)
{
	if (j.contains("enabled")) j.at("enabled").get_to(n.enabled);
	if (j.contains("max_distance")) j.at("max_distance").get_to(n.max_distance);
	if (j.contains("background_color")) j.at("background_color").get_to(n.background_color);
	if (j.contains("wireframe_color")) j.at("wireframe_color").get_to(n.wireframe_color);
}

static void to_json(json& j, const visual_profile::crosshair& c)
{
	j = json{
		{"enabled", c.enabled},
		{"copy_game", c.copy_game},
		{"sync", c.sync},
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
		{"penetration_min_damage", c.penetration_min_damage}
	};
}

static void from_json(const json& j, visual_profile::crosshair& c)
{
	if (j.contains("enabled")) j.at("enabled").get_to(c.enabled);
	if (j.contains("copy_game")) j.at("copy_game").get_to(c.copy_game);
	if (j.contains("sync")) j.at("sync").get_to(c.sync);
	if (j.contains("dot")) j.at("dot").get_to(c.dot);
	if (j.contains("lines")) j.at("lines").get_to(c.lines);
	if (j.contains("t_style")) j.at("t_style").get_to(c.t_style);
	if (j.contains("length")) j.at("length").get_to(c.length);
	if (j.contains("thickness")) j.at("thickness").get_to(c.thickness);
	if (j.contains("gap")) j.at("gap").get_to(c.gap);
	if (j.contains("color")) j.at("color").get_to(c.color);
	if (j.contains("outline_color")) j.at("outline_color").get_to(c.outline_color);
	if (j.contains("outline")) j.at("outline").get_to(c.outline);
	if (j.contains("outline_thickness")) j.at("outline_thickness").get_to(c.outline_thickness);
	c.outline_thickness = std::isfinite(c.outline_thickness)
		? std::clamp(c.outline_thickness, 0.5f, 3.0f) : 1.0f;
	if (j.contains("penetration_enabled")) j.at("penetration_enabled").get_to(c.penetration_enabled);
	if (j.contains("penetration_color_yes")) j.at("penetration_color_yes").get_to(c.penetration_color_yes);
	if (j.contains("penetration_color_no")) j.at("penetration_color_no").get_to(c.penetration_color_no);
	if (j.contains("penetration_min_damage")) j.at("penetration_min_damage").get_to(c.penetration_min_damage);
}

static void to_json(json& j, const visual_profile::chams::material& m)
{
	j = json{
		{"enabled", m.enabled},
		{"type", m.type},
		{"wireframe", m.wireframe},
		{"color", m.color},
		{"roughness", m.roughness},
		{"metalness", m.metalness},
		{"exponent", m.exponent},
		{"falloff", m.falloff},
		{"fresnel_fill", m.fresnel_fill},
		{"strength", m.strength},
		{"speed", m.speed},
		{"tint", m.tint}
	};
}

static void from_json(const json& j, visual_profile::chams::material& m)
{
	if (j.contains("enabled")) j.at("enabled").get_to(m.enabled);
	if (j.contains("type")) j.at("type").get_to(m.type);
	if (j.contains("wireframe")) j.at("wireframe").get_to(m.wireframe);
	if (j.contains("color")) j.at("color").get_to(m.color);
	if (j.contains("roughness")) j.at("roughness").get_to(m.roughness);
	if (j.contains("metalness")) j.at("metalness").get_to(m.metalness);
	if (j.contains("exponent")) j.at("exponent").get_to(m.exponent);
	if (j.contains("falloff")) j.at("falloff").get_to(m.falloff);
	if (j.contains("fresnel_fill")) j.at("fresnel_fill").get_to(m.fresnel_fill);
	if (j.contains("strength")) j.at("strength").get_to(m.strength);
	if (j.contains("speed")) j.at("speed").get_to(m.speed);
	if (j.contains("tint")) j.at("tint").get_to(m.tint);

	m.type = std::clamp(m.type, 0, static_cast<int>(visual_profile::chams::material_type_count) - 1);
}

static void to_json(json& j, const visual_profile::chams& c)
{
	j = json{
		{"enabled", c.enabled},
		{"antialiasing", c.antialiasing},
		{"glow_effect", json{{"enabled", c.glow_effect.enabled}, {"color", c.glow_effect.color},
			{"radius", c.glow_effect.radius}, {"strength", c.glow_effect.strength}, {"layers", c.glow_effect.layers}}},
		{"kill_effect", json{{"enabled", c.kill_effect.enabled}, {"color", c.kill_effect.color},
			{"duration", c.kill_effect.duration}, {"size", c.kill_effect.size}, {"count", c.kill_effect.count}}},
		{"on_shot", json{{"enabled", c.on_shot.enabled}, {"duration", c.on_shot.duration},
			{"appearance", c.on_shot.appearance}}},
		{"occlude_dynamic_doors", c.occlude_dynamic_doors},
		{"occlude_smoke", c.occlude_smoke},
		{"visible", c.visible},
		{"invisible", c.invisible}
	};
}

static void from_json(const json& j, visual_profile::chams& c)
{
	if (j.contains("enabled")) j.at("enabled").get_to(c.enabled);
	if (j.contains("antialiasing")) j.at("antialiasing").get_to(c.antialiasing);
	if (j.contains("glow_effect"))
	{
		const auto& v = j.at("glow_effect");
		if (v.contains("enabled")) v.at("enabled").get_to(c.glow_effect.enabled);
		if (v.contains("color")) v.at("color").get_to(c.glow_effect.color);
		if (v.contains("radius")) v.at("radius").get_to(c.glow_effect.radius);
		if (v.contains("strength")) v.at("strength").get_to(c.glow_effect.strength);
		if (v.contains("layers")) v.at("layers").get_to(c.glow_effect.layers);
	}
	if (j.contains("kill_effect"))
	{
		const auto& v = j.at("kill_effect");
		if (v.contains("enabled")) v.at("enabled").get_to(c.kill_effect.enabled);
		if (v.contains("color")) v.at("color").get_to(c.kill_effect.color);
		if (v.contains("duration")) v.at("duration").get_to(c.kill_effect.duration);
		if (v.contains("size")) v.at("size").get_to(c.kill_effect.size);
		if (v.contains("count")) v.at("count").get_to(c.kill_effect.count);
	}
	if (j.contains("on_shot"))
	{
		const auto& v = j.at("on_shot");
		if (v.contains("enabled")) v.at("enabled").get_to(c.on_shot.enabled);
		if (v.contains("duration")) v.at("duration").get_to(c.on_shot.duration);
		if (v.contains("appearance")) v.at("appearance").get_to(c.on_shot.appearance);
	}
	if (j.contains("occlude_dynamic_doors")) j.at("occlude_dynamic_doors").get_to(c.occlude_dynamic_doors);
	if (j.contains("occlude_smoke")) j.at("occlude_smoke").get_to(c.occlude_smoke);
	if (j.contains("visible")) j.at("visible").get_to(c.visible);
	if (j.contains("invisible")) j.at("invisible").get_to(c.invisible);
	c.glow_effect.radius = std::clamp(c.glow_effect.radius, 0.5f, 16.0f);
	c.glow_effect.strength = std::clamp(c.glow_effect.strength, 0.0f, 1.0f);
	c.glow_effect.layers = std::clamp(c.glow_effect.layers, 1, 6);
	c.kill_effect.duration = std::clamp(c.kill_effect.duration, 0.2f, 3.0f);
	c.kill_effect.size = std::clamp(c.kill_effect.size, 1.0f, 8.0f);
	c.kill_effect.count = std::clamp(c.kill_effect.count, 4, 32);
	c.on_shot.duration = std::clamp(c.on_shot.duration, 0.1f, 3.0f);

	if (!j.contains("visible"))
	{
		if (j.contains("wireframe")) j.at("wireframe").get_to(c.visible.wireframe);
		if (j.contains("color")) j.at("color").get_to(c.visible.color);
	}
}

static void to_json(json& j, const visual_profile::sound_esp& s)
{
	j = json{{"enabled", s.enabled}, {"local_sync", s.local_sync}, {"duration", s.duration},
		{"radius", s.radius}, {"color", s.color}};
}

static void from_json(const json& j, visual_profile::sound_esp& s)
{
	if (j.contains("enabled")) j.at("enabled").get_to(s.enabled);
	if (j.contains("local_sync")) j.at("local_sync").get_to(s.local_sync);
	if (j.contains("duration")) j.at("duration").get_to(s.duration);
	if (j.contains("radius")) j.at("radius").get_to(s.radius);
	if (j.contains("color")) j.at("color").get_to(s.color);
}

static void to_json(json& j, const visual_profile& e)
{
	j = json{
		{"m_player", e.m_player},
		{"m_item", e.m_item},
		{"m_projectile", e.m_projectile},
		{"m_bomb", e.m_bomb},
		{"m_chams", e.m_chams},
		{"m_radar", e.m_radar},
		{"m_sound", e.m_sound},
		{"m_no_flash", e.m_no_flash},
		{"m_crosshair", e.m_crosshair}
	};
}

static void from_json(const json& j, visual_profile& e)
{
	if (j.contains("m_player")) j.at("m_player").get_to(e.m_player);
	if (j.contains("m_item")) j.at("m_item").get_to(e.m_item);
	if (j.contains("m_projectile")) j.at("m_projectile").get_to(e.m_projectile);
	if (j.contains("m_bomb")) j.at("m_bomb").get_to(e.m_bomb);
	if (j.contains("m_chams")) j.at("m_chams").get_to(e.m_chams);
	if (j.contains("m_radar")) j.at("m_radar").get_to(e.m_radar);
	if (j.contains("m_sound")) j.at("m_sound").get_to(e.m_sound);
	if (j.contains("m_no_flash")) j.at("m_no_flash").get_to(e.m_no_flash);
	if (j.contains("m_crosshair")) j.at("m_crosshair").get_to(e.m_crosshair);
}

static void to_json(json& j, const general_profile::bullet_tracers& b)
{
	j = json{{"enabled", b.enabled}, {"color", b.color}, {"duration", b.duration}, {"thickness", b.thickness}, {"bloom", b.bloom}, {"max_count", b.max_count},
		{"draw_cubes", b.draw_cubes}, {"draw_line", b.draw_line}, {"cube_half", b.cube_half},
		{"cube_edge_color", b.cube_edge_color}, {"cube_face_alpha", b.cube_face_alpha},
		{"fade_near", b.fade_near}, {"fade_far", b.fade_far}};
}

static void from_json(const json& j, general_profile::bullet_tracers& b)
{
	if (j.contains("enabled")) j.at("enabled").get_to(b.enabled);
	if (j.contains("color")) j.at("color").get_to(b.color);
	if (j.contains("duration")) j.at("duration").get_to(b.duration);
	if (j.contains("thickness")) j.at("thickness").get_to(b.thickness);
	if (j.contains("bloom")) j.at("bloom").get_to(b.bloom);
	if (j.contains("max_count")) j.at("max_count").get_to(b.max_count);
	if (j.contains("draw_cubes")) j.at("draw_cubes").get_to(b.draw_cubes);
	if (j.contains("draw_line")) j.at("draw_line").get_to(b.draw_line);
	if (j.contains("cube_half")) j.at("cube_half").get_to(b.cube_half);
	if (j.contains("cube_edge_color")) j.at("cube_edge_color").get_to(b.cube_edge_color);
	if (j.contains("cube_face_alpha")) j.at("cube_face_alpha").get_to(b.cube_face_alpha);
	if (j.contains("fade_near")) j.at("fade_near").get_to(b.fade_near);
	if (j.contains("fade_far")) j.at("fade_far").get_to(b.fade_far);
}

static void to_json(json& j, const general_profile::hitmarker& h)
{
	j = json{{"enabled", h.enabled}, {"color", h.color}, {"size", h.size},
		{"gap", h.gap}, {"thickness", h.thickness}, {"duration", h.duration}};
}

static void from_json(const json& j, general_profile::hitmarker& h)
{
	if (j.contains("enabled")) j.at("enabled").get_to(h.enabled);
	if (j.contains("color")) j.at("color").get_to(h.color);
	if (j.contains("size")) j.at("size").get_to(h.size);
	if (j.contains("gap")) j.at("gap").get_to(h.gap);
	if (j.contains("thickness")) j.at("thickness").get_to(h.thickness);
	if (j.contains("duration")) j.at("duration").get_to(h.duration);
}

static void to_json(json& j, const general_profile::hitsound& h)
{
	j = json{{"enabled", h.enabled}, {"style", h.style}, {"volume", h.volume},
		{"show_damage", h.show_damage}, {"damage_color", h.damage_color},
		{"damage_size", h.damage_size}, {"damage_duration", h.damage_duration},
		{"damage_rise", h.damage_rise}};
}

static void from_json(const json& j, general_profile::hitsound& h)
{
	if (j.contains("enabled")) j.at("enabled").get_to(h.enabled);
	if (j.contains("style")) j.at("style").get_to(h.style);
	if (j.contains("volume")) j.at("volume").get_to(h.volume);
	if (j.contains("show_damage")) j.at("show_damage").get_to(h.show_damage);
	if (j.contains("damage_color")) j.at("damage_color").get_to(h.damage_color);
	if (j.contains("damage_size")) j.at("damage_size").get_to(h.damage_size);
	if (j.contains("damage_duration")) j.at("damage_duration").get_to(h.damage_duration);
	if (j.contains("damage_rise")) j.at("damage_rise").get_to(h.damage_rise);
	h.style = std::clamp(h.style, 0, 4);
	h.volume = std::clamp(h.volume, 0.0f, 1.0f);
	h.damage_size = std::clamp(h.damage_size, 8.0f, 28.0f);
	h.damage_duration = std::clamp(h.damage_duration, 0.15f, 2.0f);
	h.damage_rise = std::clamp(h.damage_rise, 0.0f, 100.0f);
}

static void to_json(json& j, const general_profile::screen_layout& l)
{
	j = json{
		{"version", 1},
		{"anchor", static_cast<int>(l.anchor)},
		{"offset_x", l.offset_x},
		{"offset_y", l.offset_y},
		{"scale", l.scale}
	};
}

static void from_json(const json& j, general_profile::screen_layout& l)
{
	int anchor = static_cast<int>(l.anchor);
	if (j.contains("anchor")) j.at("anchor").get_to(anchor);
	anchor = std::clamp(anchor, 0, 3);
	l.anchor = static_cast<general_profile::screen_anchor>(anchor);
	if (j.contains("offset_x")) j.at("offset_x").get_to(l.offset_x);
	if (j.contains("offset_y")) j.at("offset_y").get_to(l.offset_y);
	if (j.contains("scale")) j.at("scale").get_to(l.scale);
	l.offset_x = std::isfinite(l.offset_x) ? std::max(0.0f, l.offset_x) : 40.0f;
	l.offset_y = std::isfinite(l.offset_y) ? std::max(0.0f, l.offset_y) : 40.0f;
	l.scale = std::isfinite(l.scale) ? std::clamp(l.scale, 0.55f, 2.0f) : 1.0f;
	l.version = 1;
}

static void to_json(json& j, const general_profile::watermark& w)
{
	j = json{{"enabled", w.enabled},
		{"vertical", w.vertical}, {"show_ping", w.show_ping}, {"show_loss", w.show_loss},
		{"show_cpu", w.show_cpu}, {"show_fps", w.show_fps}};
	if (w.layout.version >= 1) j["layout"] = w.layout;
	else
	{

		j["position_x"] = w.layout.legacy_position_x;
		j["position_y"] = w.layout.legacy_position_y;
	}
}

static void from_json(const json& j, general_profile::watermark& w)
{
	if (j.contains("enabled")) j.at("enabled").get_to(w.enabled);
	if (j.contains("layout")) j.at("layout").get_to(w.layout);
	else
	{
		w.layout.version = 0;
		if (j.contains("position_x")) j.at("position_x").get_to(w.layout.legacy_position_x);
		if (j.contains("position_y")) j.at("position_y").get_to(w.layout.legacy_position_y);
	}
	if (j.contains("vertical")) j.at("vertical").get_to(w.vertical);
	if (j.contains("show_ping")) j.at("show_ping").get_to(w.show_ping);
	if (j.contains("show_loss")) j.at("show_loss").get_to(w.show_loss);
	if (j.contains("show_cpu")) j.at("show_cpu").get_to(w.show_cpu);
	if (j.contains("show_fps")) j.at("show_fps").get_to(w.show_fps);
}

static void to_json(json& j, const general_profile::spectator_list& s)
{
	j = json{{"enabled", s.enabled}, {"show_avatars", s.show_avatars}};
	if (s.layout.version >= 1) j["layout"] = s.layout;
	else
	{
		j["position_x"] = s.layout.legacy_position_x;
		j["position_y"] = s.layout.legacy_position_y;
	}
}

static void from_json(const json& j, general_profile::spectator_list& s)
{
	if (j.contains("enabled")) j.at("enabled").get_to(s.enabled);
	if (j.contains("show_avatars")) j.at("show_avatars").get_to(s.show_avatars);
	if (j.contains("layout")) j.at("layout").get_to(s.layout);
	else
	{
		s.layout.version = 0;
		if (j.contains("position_x")) j.at("position_x").get_to(s.layout.legacy_position_x);
		if (j.contains("position_y")) j.at("position_y").get_to(s.layout.legacy_position_y);
	}
}

static void to_json(json& j, const general_profile::event_log& e)
{
	j = json{{"enabled", e.enabled}, {"layout", e.layout},
		{"duration", e.duration}, {"max_entries", e.max_entries}};
}

static void from_json(const json& j, general_profile::event_log& e)
{
	if (j.contains("enabled")) j.at("enabled").get_to(e.enabled);
	if (j.contains("layout")) j.at("layout").get_to(e.layout);
	if (j.contains("duration")) j.at("duration").get_to(e.duration);
	if (j.contains("max_entries")) j.at("max_entries").get_to(e.max_entries);
	e.duration = std::isfinite(e.duration) ? std::clamp(e.duration, 0.5f, 20.0f) : 5.0f;
	e.max_entries = std::clamp(e.max_entries, 1, 5);
}

static void to_json(json& j, const general_profile::keybind_list& k)
{
	j = json{{"enabled", k.enabled}, {"layout", k.layout}};
}

static void from_json(const json& j, general_profile::keybind_list& k)
{
	if (j.contains("enabled")) j.at("enabled").get_to(k.enabled);
	if (j.contains("layout")) j.at("layout").get_to(k.layout);
}

static void to_json(json& j, const general_profile::bomb_info& b)
{
	j = json{};
	if (b.layout.version >= 1) j["layout"] = b.layout;
	else
	{
		j["position_x"] = b.layout.legacy_position_x;
		j["position_y"] = b.layout.legacy_position_y;
	}
}

static void from_json(const json& j, general_profile::bomb_info& b)
{
	if (j.contains("layout")) j.at("layout").get_to(b.layout);
	else
	{
		b.layout.version = 0;
		if (j.contains("position_x")) j.at("position_x").get_to(b.layout.legacy_position_x);
		if (j.contains("position_y")) j.at("position_y").get_to(b.layout.legacy_position_y);
	}
}

static void to_json(json& j, const general_profile::grenades& g)
{
	j = json{
		{"enabled", g.enabled}, {"local_only", g.local_only}, {"color", g.color},
		{"thickness", g.thickness}, {"bloom", g.bloom},
		{"bloom_color", g.bloom_color}, {"bloom_radius", g.bloom_radius},
		{"show_bounces", g.show_bounces}, {"bounce_color", g.bounce_color},
		{"bounce_size", g.bounce_size}, {"show_endpoint", g.show_endpoint},
		{"endpoint_color", g.endpoint_color}, {"endpoint_size", g.endpoint_size}
	};
}

static void from_json(const json& j, general_profile::grenades& g)
{
	if (j.contains("enabled")) j.at("enabled").get_to(g.enabled);
	if (j.contains("local_only")) j.at("local_only").get_to(g.local_only);
	if (j.contains("color")) j.at("color").get_to(g.color);
	if (j.contains("thickness")) j.at("thickness").get_to(g.thickness);
	if (j.contains("bloom")) j.at("bloom").get_to(g.bloom);
	if (j.contains("bloom_color")) j.at("bloom_color").get_to(g.bloom_color);
	if (j.contains("bloom_radius")) j.at("bloom_radius").get_to(g.bloom_radius);
	if (j.contains("show_bounces")) j.at("show_bounces").get_to(g.show_bounces);
	if (j.contains("bounce_color")) j.at("bounce_color").get_to(g.bounce_color);
	if (j.contains("bounce_size")) j.at("bounce_size").get_to(g.bounce_size);
	if (j.contains("show_endpoint")) j.at("show_endpoint").get_to(g.show_endpoint);
	if (j.contains("endpoint_color")) j.at("endpoint_color").get_to(g.endpoint_color);
	if (j.contains("endpoint_size")) j.at("endpoint_size").get_to(g.endpoint_size);
	g.thickness = std::clamp(g.thickness, 0.5f, 8.0f);
	g.bloom_radius = std::clamp(g.bloom_radius, 0.5f, 12.0f);
	g.bounce_size = std::clamp(g.bounce_size, 1.0f, 16.0f);
	g.endpoint_size = std::clamp(g.endpoint_size, 2.0f, 24.0f);
}

static void to_json(json& j, const general_profile::nade_helper& n)
{
	j = json{
		{"enabled", n.enabled},
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
		{"lock_time_ms", n.lock_time_ms}
	};
}

static void from_json(const json& j, general_profile::nade_helper& n)
{
	if (j.contains("enabled")) j.at("enabled").get_to(n.enabled);
	if (j.contains("draw_distance")) j.at("draw_distance").get_to(n.draw_distance);
	if (j.contains("stand_distance")) j.at("stand_distance").get_to(n.stand_distance);
	if (j.contains("stand_radius")) j.at("stand_radius").get_to(n.stand_radius);
	if (j.contains("release_radius")) j.at("release_radius").get_to(n.release_radius);
	if (j.contains("height_tolerance")) j.at("height_tolerance").get_to(n.height_tolerance);
	if (j.contains("show_action")) j.at("show_action").get_to(n.show_action);
	if (j.contains("show_distance")) j.at("show_distance").get_to(n.show_distance);
	if (j.contains("plaque_background")) j.at("plaque_background").get_to(n.plaque_background);
	if (j.contains("plaque_text")) j.at("plaque_text").get_to(n.plaque_text);
	if (j.contains("plaque_accent")) j.at("plaque_accent").get_to(n.plaque_accent);
	if (j.contains("stand_marker")) j.at("stand_marker").get_to(n.stand_marker);
	if (j.contains("stand_marker_active")) j.at("stand_marker_active").get_to(n.stand_marker_active);
	if (j.contains("aim_marker")) j.at("aim_marker").get_to(n.aim_marker);
	if (j.contains("aim_assist")) j.at("aim_assist").get_to(n.aim_assist);
	if (j.contains("auto_release")) j.at("auto_release").get_to(n.auto_release);
	if (j.contains("aim_key")) j.at("aim_key").get_to(n.aim_key);
	if (j.contains("aim_smoothing")) j.at("aim_smoothing").get_to(n.aim_smoothing);
	if (j.contains("aim_threshold")) j.at("aim_threshold").get_to(n.aim_threshold);
	if (j.contains("lock_time_ms")) j.at("lock_time_ms").get_to(n.lock_time_ms);
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
	if (j.contains("enabled")) j.at("enabled").get_to(b.enabled);
	if (j.contains("activation_key")) j.at("activation_key").get_to(b.activation_key);
	else if (j.contains("key")) j.at("key").get_to(b.activation_key);
}

static void to_json(json& j, const general_profile::edge_jump& e)
{
	j = json{{"enabled", e.enabled}, {"activation_key", e.activation_key}};
}

static void from_json(const json& j, general_profile::edge_jump& e)
{
	if (j.contains("enabled")) j.at("enabled").get_to(e.enabled);
	if (j.contains("activation_key")) j.at("activation_key").get_to(e.activation_key);
	else if (j.contains("key")) j.at("key").get_to(e.activation_key);
}

static void to_json(json& j, const general_profile::auto_stop& a)
{
	j = json{
		{"enabled", a.enabled},
		{"physical_fire", a.physical_fire},
		{"predictive_trigger", a.predictive_trigger},
		{"stop_speed", a.stop_speed},
		{"required_shoot_speed", a.required_shoot_speed}
	};
}

static void from_json(const json& j, general_profile::auto_stop& a)
{
	if (j.contains("enabled")) j.at("enabled").get_to(a.enabled);
	if (j.contains("physical_fire")) j.at("physical_fire").get_to(a.physical_fire);
	if (j.contains("predictive_trigger")) j.at("predictive_trigger").get_to(a.predictive_trigger);
	if (j.contains("stop_speed")) j.at("stop_speed").get_to(a.stop_speed);
	if (j.contains("required_shoot_speed")) j.at("required_shoot_speed").get_to(a.required_shoot_speed);
	a.stop_speed = std::clamp(a.stop_speed, 0.0f, 150.0f);
	a.required_shoot_speed = std::clamp(a.required_shoot_speed, 0.0f, 60.0f);
}

static void to_json(json& j, const general_profile& m)
{
	j = json{
		{"m_grenades", m.m_grenades},
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
		{"m_auto_stop", m.m_auto_stop},
		{"language", m.language},
		{"menu_scale", m.menu_scale},
		{"palette", json{
			{"background", m.palette.background}, {"panel", m.palette.panel},
			{"card", m.palette.card}, {"popup", m.palette.popup},
			{"accent", m.palette.accent}, {"text", m.palette.text},
			{"muted_text", m.palette.muted_text}, {"border", m.palette.border},
			{"hover", m.palette.hover}}},
		{"auto_accept", m.auto_accept},
		{"obs_bypass", m.obs_bypass},
		{"lua_enabled", m.lua_enabled},
		{"limit_fps", m.limit_fps},
		{"fps_limit", m.fps_limit}
	};
}

static void from_json(const json& j, general_profile& m)
{
	if (j.contains("m_grenades")) j.at("m_grenades").get_to(m.m_grenades);
	if (j.contains("m_nade_helper")) j.at("m_nade_helper").get_to(m.m_nade_helper);
	if (j.contains("m_bullet_tracers")) j.at("m_bullet_tracers").get_to(m.m_bullet_tracers);
	if (j.contains("m_hitmarker")) j.at("m_hitmarker").get_to(m.m_hitmarker);
	if (j.contains("m_hitsound")) j.at("m_hitsound").get_to(m.m_hitsound);
	if (j.contains("m_watermark")) j.at("m_watermark").get_to(m.m_watermark);
	if (j.contains("m_spectator_list")) j.at("m_spectator_list").get_to(m.m_spectator_list);
	if (j.contains("m_event_log")) j.at("m_event_log").get_to(m.m_event_log);
	if (j.contains("m_keybind_list")) j.at("m_keybind_list").get_to(m.m_keybind_list);
	if (j.contains("m_bomb_info")) j.at("m_bomb_info").get_to(m.m_bomb_info);
	if (j.contains("m_bunny_hop")) j.at("m_bunny_hop").get_to(m.m_bunny_hop);
	if (j.contains("m_edge_jump")) j.at("m_edge_jump").get_to(m.m_edge_jump);
	if (j.contains("m_auto_stop")) j.at("m_auto_stop").get_to(m.m_auto_stop);
	if (j.contains("language")) j.at("language").get_to(m.language);
	m.language = std::clamp(m.language, 0, static_cast<int>(render::localization::id::count) - 1);

	render::localization::set(static_cast<render::localization::id>(m.language));
	if (j.contains("menu_scale")) j.at("menu_scale").get_to(m.menu_scale);
	m.menu_scale = std::isfinite(m.menu_scale) ? std::clamp(m.menu_scale, 0.50f, 1.50f) : 1.0f;
	if (j.contains("palette"))
	{
		const auto& p = j.at("palette");
		if (p.contains("background")) p.at("background").get_to(m.palette.background);
		if (p.contains("panel")) p.at("panel").get_to(m.palette.panel);
		if (p.contains("card")) p.at("card").get_to(m.palette.card);
		if (p.contains("popup")) p.at("popup").get_to(m.palette.popup);
		if (p.contains("accent")) p.at("accent").get_to(m.palette.accent);
		if (p.contains("text")) p.at("text").get_to(m.palette.text);
		if (p.contains("muted_text")) p.at("muted_text").get_to(m.palette.muted_text);
		if (p.contains("border")) p.at("border").get_to(m.palette.border);
		if (p.contains("hover")) p.at("hover").get_to(m.palette.hover);
	}
	if (j.contains("auto_accept")) j.at("auto_accept").get_to(m.auto_accept);
	if (j.contains("obs_bypass")) j.at("obs_bypass").get_to(m.obs_bypass);
	if (j.contains("lua_enabled")) j.at("lua_enabled").get_to(m.lua_enabled);
	if (j.contains("limit_fps")) j.at("limit_fps").get_to(m.limit_fps);
	if (j.contains("fps_limit")) j.at("fps_limit").get_to(m.fps_limit);
}

combat_profile::resolved_config combat_profile::get( std::uint32_t weapon_type ) const
{
	const auto idx = weapon_type - game::rules::sidearm;
	const auto group_idx = idx < k_group_count ? idx : 2;
	const auto& ov = this->overrides[ group_idx ];
	const auto& gl = this->global;

	resolved_config cfg{};

	cfg.aimbot.enabled = gl.aimbot_enabled;
	cfg.aimbot.key = gl.aimbot_key;
	cfg.aimbot.activation_mode = gl.aimbot_activation_mode;
	cfg.aimbot.fov = ov.use_global ? gl.aimbot_fov : ov.aimbot_fov;
	cfg.aimbot.smoothing = ov.use_global ? gl.aimbot_smoothing : ov.aimbot_smoothing;
	cfg.aimbot.humanize = ov.use_global ? gl.aimbot_humanize : ov.aimbot_humanize;
	cfg.aimbot.autowall = ov.use_global ? gl.aimbot_autowall : ov.aimbot_autowall;
	cfg.aimbot.min_damage = ov.use_global ? gl.aimbot_min_damage : ov.aimbot_min_damage;
	if (gl.aimbot_min_damage_override_enabled
		&& combat_profile::activation_active(gl.aimbot_min_damage_override_mode,
			gl.aimbot_min_damage_override_key))
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
	cfg.aimbot.multipoint_config = ov.use_global
		? gl.aimbot_multipoint_config : ov.aimbot_multipoint_config;
	cfg.aimbot.prediction = ov.use_global ? gl.aimbot_prediction : ov.aimbot_prediction;
	cfg.aimbot.rcs = ov.use_global ? gl.aimbot_rcs : ov.aimbot_rcs;
	cfg.aimbot.fov_config = ov.use_global ? gl.aimbot_fov_config : ov.aimbot_fov_config;
	cfg.aimbot.predictive = cfg.aimbot.prediction.enabled;

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
	if (gl.triggerbot_min_damage_override_enabled
		&& combat_profile::activation_active(gl.triggerbot_min_damage_override_mode,
			gl.triggerbot_min_damage_override_key))
		cfg.triggerbot.min_damage = gl.triggerbot_min_damage_override;
	cfg.triggerbot.autostop = ov.use_global ? gl.triggerbot_autostop : ov.triggerbot_autostop;
	cfg.triggerbot.predictive = ov.use_global ? gl.triggerbot_predictive : ov.triggerbot_predictive;
	cfg.triggerbot.revolver_pre_cock = ov.use_global ? gl.triggerbot_revolver_pre_cock : ov.triggerbot_revolver_pre_cock;
	cfg.triggerbot.revolver_release_margin_ms = ov.use_global ? gl.triggerbot_revolver_release_margin_ms : ov.triggerbot_revolver_release_margin_ms;
	cfg.triggerbot.checks = ov.use_global ? gl.triggerbot_checks : ov.triggerbot_checks;

	cfg.other.penetration_crosshair = gl.penetration_crosshair;
	cfg.other.penetration_color_yes = gl.penetration_color_yes;
	cfg.other.penetration_color_no = gl.penetration_color_no;

	return cfg;
}

bool combat_profile::seed_trigger_configured( ) const noexcept
{
	if ( !this->global.triggerbot_enabled )
	{
		return false;
	}

	for ( const auto& override : this->overrides )
	{
		const auto seed = override.use_global
			? this->global.triggerbot_seed_type : override.triggerbot_seed_type;
		if ( seed != seed_mode::none )
		{
			return true;
		}
	}
	return false;
}

json build_config_json()
{
	json j;
	j["combat_global"] = combat_settings.global;
	j["combat_overrides"] = combat_settings.overrides;
	j["esp"] = visual_settings;
	j["misc"] = general_settings;
	return j;
}

void apply_config_json(const json& j)
{
	if (j.contains("combat_global")) j.at("combat_global").get_to(combat_settings.global);
	if (j.contains("combat_overrides")) j.at("combat_overrides").get_to(combat_settings.overrides);
	if (j.contains("esp")) j.at("esp").get_to(visual_settings);
	if (j.contains("misc")) j.at("misc").get_to(general_settings);

	if ((!j.contains("misc") || !j.at("misc").contains("m_auto_stop")))
	{
		general_settings.m_auto_stop.enabled =
			combat_settings.global.triggerbot_autostop
			|| std::ranges::any_of(combat_settings.overrides,
				[](const auto& group) { return group.triggerbot_autostop; });
	}
}

bool apply_default_config()
{
	try
	{
		const auto* begin = reinterpret_cast<const char*>( embedded::default_profile_json );
		apply_config_json( json::parse( begin,
			begin + embedded::default_profile_json_size ) );
		return true;
	}
	catch ( ... )
	{
		return false;
	}
}

bool configuration_store::write_to(const std::string& path) const
{
	try
	{
		json j = build_config_json();
		std::ofstream ofs(std::filesystem::u8path(path));
		if (!ofs.is_open())
			return false;
		ofs << j.dump(4);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

void configuration_store::set_active_path(const std::string& full_path)
{
	if (full_path.empty())
		return;

	const auto p = std::filesystem::u8path(full_path);

	const auto stem = p.stem().string();
	std::snprintf(this->name_buffer, sizeof(this->name_buffer), "%s",
		stem.empty() ? "config" : stem.c_str());
}

bool configuration_store::read_from(const std::string& path)
{
	try
	{
		std::ifstream ifs(std::filesystem::u8path(path));
		if (!ifs.is_open())
			return false;

		json j;
		ifs >> j;
		apply_config_json(j);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

std::string configuration_store::cache_path() const
{
	const auto dir = platform::windows::runtime_storage::area("config_cache");
	if (dir.empty())
		return {};
	return path_to_utf8(dir / "current.cfg");
}

bool configuration_store::write_cache() const
{
	try
	{
		const auto target = std::filesystem::u8path(this->cache_path());
		if (target.empty())
			return false;

		std::error_code error{};
		std::filesystem::create_directories(target.parent_path(), error);
		if (error)
			return false;

		auto pending = target;
		pending += L".tmp";
		if (!this->write_to(path_to_utf8(pending)))
			return false;

		if (!::MoveFileExW(pending.c_str(), target.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::filesystem::remove(pending, error);
			return false;
		}
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool configuration_store::read_cache()
{
	const auto path = this->cache_path();
	return !path.empty() && std::filesystem::exists(std::filesystem::u8path(path)) && this->read_from(path);
}

}
