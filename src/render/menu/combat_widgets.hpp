#pragma once
#include <config/settings.hpp>

namespace render::menu::combat
{
void checks_row(config::combat_profile::legit_checks &checks);
void visibility_row(config::combat_profile::legit_checks &checks);
void trigger_timing_row(int &delay, int &randomize, float &outlier_chance, int &outlier_delay,
                        int &delay_after);
void prediction_row(config::combat_profile::prediction_settings &prediction);
void master_row(bool &enabled, int &mode);
void damage_override_row(bool &enabled, float &value, int &mode, int &key);
void fov_row(int &fixed_fov, config::combat_profile::fov_settings &fov, bool &draw_area, zdraw::rgba &color);
void humanizer_row(int &amount, int &smoothing, config::combat_profile::humanizer_settings &humanizer);
void multipoint_row(bool &enabled, config::combat_profile::multipoint_settings &settings);
void rcs_row(config::combat_profile::rcs_settings &rcs);
} // namespace render::menu::combat
