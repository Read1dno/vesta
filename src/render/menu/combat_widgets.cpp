#include <stdafx.hpp>
#include <render/menu/internal.hpp>
#include <render/menu/combat_widgets.hpp>

namespace render::menu::combat
{
using namespace detail;
static constexpr const char *activation_modes[]{"Hold", "Always On", "Toggle"};
static constexpr const char *fov_modes[]{"Fixed", "Distance", "Target"};
void checks_row(config::combat_profile::legit_checks &checks)
{
    int mask = (checks.airborne ? 1 : 0) | (checks.smoke ? 2 : 0) | (checks.flashed ? 4 : 0);
    static constexpr std::pair<const char *, int> options[]{{"Jump", 1}, {"Smoke", 2}, {"Flash", 4}};
    multiselect_row("Checks", mask, options, 7);
    checks.airborne = (mask & 1) != 0;
    checks.smoke = (mask & 2) != 0;
    checks.flashed = (mask & 4) != 0;
}

void visibility_row(config::combat_profile::legit_checks &checks)
{
    auto only_visible = checks.walls == config::combat_profile::wall_policy::block;
    toggle_row("Only Visible", only_visible);
    checks.walls = only_visible ? config::combat_profile::wall_policy::block
                                : config::combat_profile::wall_policy::penetration;
}

void trigger_timing_row(int &delay, int &randomize, float &outlier_chance, int &outlier_delay,
                        int &delay_after)
{
    settings_popup_row("Shot Timing", 5, [&] {
        slider_row("Delay Before", delay, 0, 500, " ms");
        slider_row("Randomize", randomize, 0, 100, " ms");
        slider_row("Outlier Chance", outlier_chance, 0.0f, 25.0f, "%", 0.5f);
        slider_row("Outlier Delay", outlier_delay, 0, 500, " ms");
        slider_row("Delay After", delay_after, 0, 1000, " ms");
    });
}

void prediction_row(config::combat_profile::prediction_settings &prediction)
{
    toggle_popup_row("Prediction", prediction.enabled, 2, [&] {
        slider_row("Max Horizon", prediction.max_horizon_ms, 0.0f, 120.0f, " ms", 1.0f);
        toggle_row("Acceleration", prediction.acceleration);
    });
}

void master_row(bool &enabled, int &mode)
{
    toggle_popup_row("Master Switch", enabled, 1, [&] { select_row("Mode", mode, activation_modes); });
}

void damage_override_row(bool &enabled, float &value, int &mode, int &key)
{
    const auto rows = mode == config::combat_profile::activation::always ? 2 : 3;
    toggle_popup_row("Damage Override", enabled, rows, [&] {
        select_row("Mode", mode, activation_modes);
        if (mode != config::combat_profile::activation::always)
            keybind_row("Key", key);
        slider_row("Override Damage", value, 1.0f, 100.0f, "", 1.0f);
    });
}

void fov_row(int &fixed_fov, config::combat_profile::fov_settings &fov, bool &draw_area, zdraw::rgba &color)
{
    const auto dynamic = fov.selection != config::combat_profile::fov_settings::fixed;
    const auto target_mode = fov.selection == config::combat_profile::fov_settings::target_distance;
    auto &near_distance = target_mode ? fov.target_near_distance_m : fov.near_distance_m;
    auto &near_fov = target_mode ? fov.target_near_fov : fov.near_fov;
    auto &far_distance = target_mode ? fov.target_far_distance_m : fov.far_distance_m;
    auto &far_fov = target_mode ? fov.target_far_fov : fov.far_fov;
    auto &curve = target_mode ? fov.target_distance_curve : fov.distance_curve;
    settings_popup_row("FOV", dynamic ? 4 : 3, [&] {
        select_row("Mode", fov.selection, fov_modes);
        if (!dynamic)
        {
            slider_row("Radius", fixed_fov, 1, 360, "°");
        }
        else
        {
            slider_row("Max Radius", near_fov, 2.0f, 45.0f, "°", 0.5f);
            settings_popup_row("Advanced", 5, [&] {
                slider_row("Full Size At", near_distance, 0.5f, 10.0f, " m", 0.5f);
                slider_row("Max Radius", near_fov, 2.0f, 45.0f, "°", 0.5f);
                slider_row("Min Size At", far_distance, 10.0f, 100.0f, " m", 0.5f);
                slider_row("Min Radius", far_fov, 0.25f, 15.0f, "°", 0.25f);
                slider_row("Falloff", curve, 0.25f, 4.0f, "", 0.05f);
            });
        }
        toggle_color_row("Visualization", draw_area, color);
    });
}

void humanizer_row(int &amount, int &smoothing, config::combat_profile::humanizer_settings &humanizer)
{
    settings_popup_row("Humanizer Profile", 6, [&] {
        humanizer_preview(amount, smoothing, humanizer);
        slider_row("Amount", amount, 0, 100, "%");
        slider_row("Smoothing", smoothing, 0, 50);
        settings_popup_row("Motion", 4, [&] {
            slider_row("Gravity", humanizer.gravity, 0.0f, 20.0f, "", 0.05f);
            slider_row("Wind", humanizer.wind, 0.0f, 20.0f, "", 0.05f);
            slider_row("Max Step", humanizer.max_step, 1.0f, 90.0f, "°", 0.5f);
            slider_row("Damping", humanizer.damping, 0.0f, 1.0f, "", 0.01f);
        });
        settings_popup_row("Behavior", 7, [&] {
            slider_row("Reaction Min", humanizer.reaction_min_ms, 0, 500, " ms");
            slider_row("Reaction Max", humanizer.reaction_max_ms, 0, 750, " ms");
            slider_row("Curve", humanizer.curve, 0.0f, 1.0f, "", 0.01f);
            slider_row("Overshoot Chance", humanizer.overshoot_chance, 0.0f, 100.0f, "%", 1.0f);
            slider_row("Overshoot Amount", humanizer.overshoot_amount, 0.0f, 1.0f, "", 0.01f);
            slider_row("Jitter", humanizer.jitter, 0.0f, 3.0f, "", 0.05f);
            slider_row("Deadzone", humanizer.deadzone, 0.0f, 2.0f, "°", 0.05f);
        });
    });
}

void multipoint_row(bool &enabled, config::combat_profile::multipoint_settings &settings)
{
    toggle_popup_row("Multi-Point", enabled, 5, [&] {
        toggle_row("Cap Points", settings.caps);
        toggle_row("Side Points", settings.sides);
        slider_row("Head Scale", settings.head_scale, 0.05f, 0.95f, "", 0.05f);
        slider_row("Body Scale", settings.body_scale, 0.05f, 0.95f, "", 0.05f);
        slider_row("Limbs Scale", settings.limb_scale, 0.05f, 0.95f, "", 0.05f);
    });
}

void rcs_row(config::combat_profile::rcs_settings &rcs)
{
    toggle_popup_row("RCS", rcs.enabled, 7, [&] {
        slider_row("Start Bullet", rcs.start_bullet, 1, 10);
        slider_row("Pitch Strength", rcs.pitch, 0.0f, 200.0f, "%", 1.0f);
        slider_row("Yaw Strength", rcs.yaw, 0.0f, 200.0f, "%", 1.0f);
        slider_row("Correction Time", rcs.response_ms, 1.0f, 150.0f, " ms", 1.0f);
        slider_row("Smoothness", rcs.smoothness, 0.0f, 100.0f, "%", 1.0f);
        slider_row("Strength Variation", rcs.randomness, 0.0f, 30.0f, "%", 1.0f);
        slider_row("Path Drift", rcs.drift, 0.0f, 30.0f, "%", 1.0f);
    });
}

} // namespace render::menu::combat
