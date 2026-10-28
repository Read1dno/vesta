#include <stdafx.hpp>
#include <features/aimbot/flick_profile.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
void aimbot_t::recoil_control(const config::combat_profile::aimbot& cfg, bool defer_input)
{
    VESTA_PERF_SCOPE(aim_recoil);
    const auto clear_correction = [this](bool clear_burst)
    {
        m_rcs_raw = {};
        m_rcs_target = {};
        m_rcs_velocity = {};
        m_rcs_applied = {};
        m_rcs_mouse_error = {};
        m_rcs_pending_mouse = {};
        m_rcs_last_input_sequence = -1;
        m_rcs_last_input_time = {};
        m_rcs_input_step = 24;
        m_rcs_active = false;
        if (clear_burst)
        {
            m_rcs_weapon = 0;
            m_rcs_last_clip = -1;
            m_rcs_last_shot_time = -1.0f;
            m_rcs_burst_shots = 0;
        }
    };
    const auto pawn = game::local_player().pawn();
    if (!pawn || !cfg.rcs.enabled)
    {
        clear_correction(true);
        return;
    }

    const auto& weapon_ctx = ballistics().ctx();
    const auto weapon = weapon_ctx.valid ? weapon_ctx.weapon : game::local_player().weapon();
    if (weapon != m_rcs_weapon)
    {
        clear_correction(true);
        m_rcs_weapon = weapon;
    }

    if (!weapon_ctx.valid || weapon_ctx.weapon != weapon || !weapon_ctx.is_full_auto)
    {
        clear_correction(true);
        return;
    }
    const auto reported_shots =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
    if (weapon_ctx.valid && weapon_ctx.weapon == weapon)
    {
        if (m_rcs_last_clip >= 0 && weapon_ctx.clip >= 0 && weapon_ctx.clip < m_rcs_last_clip)
        {
            m_rcs_burst_shots += m_rcs_last_clip - weapon_ctx.clip;
        }
        m_rcs_last_clip = weapon_ctx.clip;
        if (weapon_ctx.last_shot_time > m_rcs_last_shot_time + 0.0001f)
        {
            m_rcs_last_shot_time = weapon_ctx.last_shot_time;
            m_rcs_burst_shots =
                std::max(m_rcs_burst_shots, std::max(1, static_cast<int>(std::ceil(weapon_ctx.recoil_index))));
        }
        m_rcs_burst_shots =
            std::max(m_rcs_burst_shots, std::max(0, static_cast<int>(std::ceil(weapon_ctx.recoil_index))));
    }
    m_rcs_burst_shots = std::max(m_rcs_burst_shots, std::max(reported_shots, 0));

    const auto physical_attack =
        game::physical_action_down(game::input_action::attack, platform::windows::bound_input());
    const auto attack_owned_by_trigger = m_trigger_held && !m_seed_held_secondary && !m_seed_held_proxy;
    if (!physical_attack && !attack_owned_by_trigger)
    {
        clear_correction(false);
        m_rcs_burst_shots = 0;
        return;
    }
    if (m_rcs_burst_shots < std::max(cfg.rcs.start_bullet, 1))
    {
        clear_correction(false);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    auto dt = std::chrono::duration<float>(now - m_rcs_last_call).count();
    if (dt <= 0.0f || dt > 0.1f)
        dt = 0.004f;
    m_rcs_last_call = now;

    const auto sensitivity = game::variables().get<float>(CONVAR("sensitivity"_id));
    const auto fov_adjust =
        game::bound_memory().process.load<float>(pawn + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id));
    const auto degrees_per_pixel = sensitivity * 0.022f * fov_adjust;
    if (!std::isfinite(degrees_per_pixel) || degrees_per_pixel <= 0.0f)
        return;

    const auto punch_sample = simulation::read_cached_punch(game::bound_memory().process, pawn,
                                                            SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id));
    if (!punch_sample)
        return;
    const auto punch = *punch_sample;
    m_rcs_raw = punch;

    if (!m_rcs_active)
    {
        // Humanization varies both the small residual error and the time profile of
        // this burst. It must not turn a high setting into a wildly wrong endpoint.
        const auto spread = std::clamp(cfg.rcs.randomness, 0.0f, 30.0f) * 0.001f;
        m_rcs_gain = m_rng.uniform(1.0f - spread, 1.0f + spread);
        const auto timing_spread = std::clamp(cfg.rcs.randomness, 0.0f, 30.0f) * 0.003f;
        m_rcs_response_scale = m_rng.uniform(1.0f - timing_spread, 1.0f + timing_spread);
        m_rcs_phase = m_rng.uniform(0.0f, 2.0f * std::numbers::pi_v<float>);
        m_rcs_freq = m_rng.uniform(1.5f, 3.5f);
        m_rcs_active = true;
    }
    m_rcs_phase += m_rcs_freq * dt;

    auto desired = punch * m_rcs_gain;
    desired.x *= std::clamp(cfg.rcs.pitch, 0.0f, 200.0f) * 0.01f;
    desired.y *= std::clamp(cfg.rcs.yaw, 0.0f, 200.0f) * 0.01f;
    const auto drift = std::clamp(cfg.rcs.drift, 0.0f, 30.0f) * 0.002f;
    desired.x += punch.length_2d() * drift * std::sin(m_rcs_phase);
    desired.y += punch.length_2d() * drift * 0.8f * std::cos(m_rcs_phase * 1.27f);

    const auto smoothness = std::clamp(cfg.rcs.smoothness, 0.0f, 100.0f) * 0.01f;
    const auto target_follow_time = std::lerp(0.0f, 0.020f, smoothness);
    if (target_follow_time <= 0.0001f)
    {
        m_rcs_target = desired;
    }
    else
    {
        const auto target_blend = 1.0f - std::exp(-dt / std::max(target_follow_time, 0.001f));
        m_rcs_target += (desired - m_rcs_target) * target_blend;
    }

    const auto follow_time =
        std::clamp(cfg.rcs.response_ms * m_rcs_response_scale * std::lerp(1.0f, 1.35f, smoothness), 1.0f, 300.0f) *
        0.001f;
    const auto velocity_time = std::lerp(0.0f, 0.018f, smoothness);
    const auto max_speed = std::lerp(360.0f, 80.0f, smoothness);
    const auto smooth_axis = [dt, follow_time, velocity_time, max_speed](float current, float target, float& velocity)
    {
        const auto error = target - current;
        const auto desired_velocity = std::clamp(error / std::max(follow_time, 0.001f), -max_speed, max_speed);
        const auto velocity_blend = velocity_time <= 0.0001f ? 1.0f : 1.0f - std::exp(-dt / velocity_time);
        velocity += (desired_velocity - velocity) * velocity_blend;
        auto next = current + velocity * dt;
        if ((target - current) * (target - next) <= 0.0f)
        {
            next = target;
            velocity = 0.0f;
        }
        return next;
    };
    const auto previous = m_rcs_applied;
    m_rcs_applied.x = smooth_axis(m_rcs_applied.x, m_rcs_target.x, m_rcs_velocity.x);
    m_rcs_applied.y = smooth_axis(m_rcs_applied.y, m_rcs_target.y, m_rcs_velocity.y);
    m_rcs_applied.z = 0.0f;
    m_rcs_input_step = std::max(1, static_cast<int>(std::lround(std::lerp(24.0f, 5.0f, smoothness))));
    const auto delta = m_rcs_applied - previous;
    m_rcs_mouse_error.x += delta.y / degrees_per_pixel;
    m_rcs_mouse_error.y -= delta.x / degrees_per_pixel;
    const auto dx = static_cast<int>(m_rcs_mouse_error.x);
    const auto dy = static_cast<int>(m_rcs_mouse_error.y);
    m_rcs_mouse_error.x -= static_cast<float>(dx);
    m_rcs_mouse_error.y -= static_cast<float>(dy);
    if (dx || dy)
    {
        m_rcs_pending_mouse.x += static_cast<float>(dx);
        m_rcs_pending_mouse.y += static_cast<float>(dy);
    }
    if (!defer_input)
        flush_recoil_input();
}

void aimbot_t::flush_recoil_input()
{
    VESTA_PERF_SCOPE(aim_flush);
    const auto limit = std::max(m_rcs_input_step, 1);
    const auto dx = std::clamp(static_cast<int>(m_rcs_pending_mouse.x), -limit, limit);
    const auto dy = std::clamp(static_cast<int>(m_rcs_pending_mouse.y), -limit, limit);
    if (!dx && !dy)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (m_rcs_last_input_time.time_since_epoch().count() != 0 &&
        now - m_rcs_last_input_time < std::chrono::milliseconds(2))
        return;
    if ((dx || dy) &&
        platform::windows::bound_input().pointer(dx, dy, platform::windows::pointer_action::relative_move))
    {
        m_rcs_pending_mouse.x -= static_cast<float>(dx);
        m_rcs_pending_mouse.y -= static_cast<float>(dy);
        if (m_aim_virtual_angles_valid && m_aim_degrees_per_pixel > 0.0f)
        {
            if (m_aim_pending_since.time_since_epoch().count() == 0)
                m_aim_pending_since = now;
            m_aim_sent_since_sample.x += static_cast<float>(dy) * m_aim_degrees_per_pixel;
            m_aim_sent_since_sample.y -= static_cast<float>(dx) * m_aim_degrees_per_pixel;
        }
        m_rcs_last_input_sequence = -1;
        m_rcs_last_input_time = now;
    }
}

void aimbot_t::aimbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const target& tgt,
                      const config::combat_profile::aimbot& cfg)
{
    if (!config::combat_profile::activation_active(cfg.activation_mode, cfg.key))
    {
        m_aim_error = {};
        m_aim_last_input_sequence = -1;
        m_aim_virtual_angles_valid = false;
        m_aim_sent_since_sample = {};
        m_aim_pending_since = {};
        m_aim_last_call = {};
        m_aim_pawn = 0; // key released: the next acquisition starts fresh
        m_aim_tracking_lag = 0.0f;
        m_aim_velocity_valid = false;
        return;
    }

    constexpr auto m_yaw{0.022f};
    const auto local_pawn = game::local_player().pawn();
    if (!local_pawn)
    {
        return;
    }
    if (local_checks_blocked(cfg.checks) ||
        live_target_invulnerable(tgt.player ? tgt.player->pawn : 0, tgt.player ? tgt.player->invulnerable : true))
    {
        return;
    }
    const auto sensitivity = game::variables().get<float>(CONVAR("sensitivity"_id));
    float fov_adjust{};
    const auto have_fov_adjust = game::bound_memory().process.copy(
        local_pawn + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id), &fov_adjust, sizeof(fov_adjust));
    const auto sampled_degrees = sensitivity * m_yaw * fov_adjust;
    if (have_fov_adjust && std::isfinite(sampled_degrees) && sampled_degrees >= 0.0001f && sampled_degrees <= 1.0f)
    {
        if (m_aim_degrees_per_pixel <= 0.0f || sampled_degrees >= m_aim_degrees_per_pixel * 0.8f)
        {
            m_aim_degrees_per_pixel = sampled_degrees;
            m_aim_degrees_candidate = {};
            m_aim_degrees_confirmations = {};
        }
        else
        {
            const auto same_candidate =
                m_aim_degrees_candidate > 0.0f &&
                std::abs(sampled_degrees - m_aim_degrees_candidate) <= m_aim_degrees_candidate * 0.05f;
            m_aim_degrees_confirmations = same_candidate ? m_aim_degrees_confirmations + 1 : 1;
            m_aim_degrees_candidate = sampled_degrees;
            if (m_aim_degrees_confirmations >= 2)
            {
                m_aim_degrees_per_pixel = sampled_degrees;
                m_aim_degrees_candidate = {};
                m_aim_degrees_confirmations = {};
            }
        }
    }
    const auto deg_per_pixel = m_aim_degrees_per_pixel;

    if (!std::isfinite(deg_per_pixel) || deg_per_pixel <= 0.0f)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    int input_sequence{-1};
    foundation::vec3 raw_angles{};
    auto command_angles_valid = m_recoil_commands.read_angles(raw_angles, &input_sequence);
    if (command_angles_valid)
    {
        const auto camera_gap =
            std::hypot(raw_angles.x - view_angles.x, foundation::wrap_yaw(raw_angles.y - view_angles.y));
        if (!std::isfinite(camera_gap) || camera_gap > 35.0f)
        {
            // Keep the validated sequence boundary but do not steer from a stale or
            // incorrectly resolved ring slot.
            command_angles_valid = false;
        }
    }
    const auto sequence_changed = input_sequence >= 0 && input_sequence != m_aim_last_input_sequence;

    auto freshest = game::skeletons().get(tgt.player->bone_cache);
    if (!freshest.is_valid())
        freshest = tgt.bones;
    if (!freshest.is_valid())
        return;

    // Reconstruct the picked point from the freshest complete bone transform.
    auto aim_point = tgt.aim_point;
    if (tgt.bone >= 0 && tgt.bone < 128)
    {
        const auto& fresh_bone = freshest.bones[tgt.bone];
        const auto rotation_norm =
            fresh_bone.rotation.x * fresh_bone.rotation.x + fresh_bone.rotation.y * fresh_bone.rotation.y +
            fresh_bone.rotation.z * fresh_bone.rotation.z + fresh_bone.rotation.w * fresh_bone.rotation.w;
        const auto fresh_point = fresh_bone.position + fresh_bone.rotation.apply(tgt.aim_offset);
        const auto valid_point = std::isfinite(fresh_point.x) && std::isfinite(fresh_point.y) &&
                                 std::isfinite(fresh_point.z) && std::isfinite(rotation_norm) &&
                                 rotation_norm >= 0.5f && rotation_norm <= 1.5f &&
                                 fresh_point.distance_sqr(tgt.aim_point) <= 128.0f * 128.0f;
        if (valid_point)
            aim_point = fresh_point;
    }

    // Smoothing 0 is an explicit snap mode. Humanizer path shaping must not turn
    // it back into a delayed move or manufacture an overshoot around the target.
    const auto snap = cfg.smoothing <= 0;
    const auto h = snap ? 0.0f : std::clamp(cfg.humanize, 0, 100) / 100.0f;

    auto dt = std::chrono::duration<float>(now - m_aim_last_call).count();
    const auto stale_control = dt <= 0.0f || dt > 0.1f;
    if (stale_control)
    {
        dt = game::rules::simulation_step; // first command after a pause
    }
    m_aim_last_call = now;

    auto sampled_control_angles = view_angles;
    if (command_angles_valid)
        sampled_control_angles = raw_angles;
    if (m_rcs_active)
    {
        sampled_control_angles += m_rcs_applied;

        sampled_control_angles.x += m_rcs_pending_mouse.y * deg_per_pixel;
        sampled_control_angles.y -= m_rcs_pending_mouse.x * deg_per_pixel;
        sampled_control_angles.y = foundation::wrap_yaw(sampled_control_angles.y);
    }
    if (!m_aim_virtual_angles_valid || stale_control)
    {
        m_aim_virtual_angles = sampled_control_angles;
        m_aim_virtual_angles_valid = true;
        m_aim_last_camera_angles = command_angles_valid ? raw_angles : view_angles;
        m_aim_sent_since_sample = {};
        m_aim_pending_since = {};
    }
    else if (sequence_changed && command_angles_valid)
    {
        const auto pending_expired = m_aim_pending_since.time_since_epoch().count() != 0 &&
                                     now - m_aim_pending_since > std::chrono::milliseconds(80);
        const auto feedback =
            detail::reconcile_control_angles(m_aim_virtual_angles, sampled_control_angles, m_aim_last_camera_angles,
                                             raw_angles, m_aim_sent_since_sample, dt, pending_expired);
        m_aim_virtual_angles = snap ? sampled_control_angles : feedback.control;
        if (feedback.manual_override || pending_expired)
            m_aim_error = {};
        m_aim_last_camera_angles = raw_angles;
        m_aim_sent_since_sample = snap ? foundation::vec3{} : feedback.pending;
        if (snap || feedback.manual_override || pending_expired || m_aim_sent_since_sample.length_sqr() < 0.0001f)
            m_aim_pending_since = {};
    }
    auto control_angles = m_aim_virtual_angles;
    m_aim_last_input_sequence = input_sequence;

    const auto target_angle = [&](const foundation::vec3& point)
    {
        auto angle = foundation::look_at_angles(eye_pos, point);
        angle.y = foundation::wrap_yaw(angle.y);
        return angle;
    };

    auto desired = target_angle(aim_point);
    auto delta_x = desired.x - control_angles.x;
    auto delta_y = foundation::wrap_yaw(desired.y - control_angles.y);
    auto dist = std::sqrt(delta_x * delta_x + delta_y * delta_y);

    const auto reacquire = tgt.player->pawn != m_aim_pawn || now - m_aim_last_seen > std::chrono::milliseconds(400);
    if (reacquire)
    {
        m_aim_pawn = tgt.player->pawn;
        m_aim_tracking_lag = 0.0f;
        m_aim_flick_speed = 0.0f;
        m_flick_active = false;
        m_aim_velocity_valid = false;
        m_aim_relative_acceleration = {};
        m_aim_simulation_tick = -1;
        m_aim_simulation_time = 0.0f;
        m_aim_velocity_samples = 0;
        if (h > 0.0f)
        {
            const auto reaction_min = std::clamp(cfg.humanizer.reaction_min_ms, 0, 500);
            const auto reaction_max = std::clamp(cfg.humanizer.reaction_max_ms, reaction_min, 750);
            m_aim_reaction_until =
                now + std::chrono::milliseconds(static_cast<int>(
                          h * m_rng.uniform(static_cast<float>(reaction_min), static_cast<float>(reaction_max))));
            m_aim_initial_dist = std::max(dist, 0.5f);
            m_aim_curve =
                (m_rng.uniform(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f) * h * std::clamp(cfg.humanizer.curve, 0.0f, 1.0f);
            m_aim_overshoot = m_rng.uniform(0.0f, 100.0f) < std::clamp(cfg.humanizer.overshoot_chance, 0.0f, 100.0f) * h
                                  ? 1.0f + h * std::clamp(cfg.humanizer.overshoot_amount, 0.0f, 1.0f)
                                  : 1.0f;
            m_aim_ramp = 0.0f;
            m_aim_wander_phase = m_rng.uniform(0.0f, 6.28f);
            m_aim_wander_freq = m_rng.uniform(0.35f, 0.85f);
        }
    }
    m_aim_last_seen = now;
    m_aim_last_bone = tgt.bone;

    if (h > 0.0f && now < m_aim_reaction_until)
        return;

    auto factor = cfg.smoothing > 1 ? static_cast<float>(cfg.smoothing) : 1.0f;
    if (h > 0.0f)
        m_aim_ramp = std::min(1.0f, m_aim_ramp + dt / 0.055f);
    const auto response_ramp = h > 0.0f ? 0.75f + 0.25f * m_aim_ramp : 1.0f;
    const auto command_response = std::clamp(response_ramp / factor, 0.001f, 1.0f);
    const auto command_fraction = std::clamp(dt / game::rules::simulation_step, 0.01f, 4.0f);
    const auto response_alpha =
        command_response >= 1.0f ? 1.0f : 1.0f - std::pow(1.0f - command_response, command_fraction);
    const auto prediction_response = std::clamp(response_alpha, 0.001f, 1.0f);

    if (cfg.prediction.enabled)
    {
        m_aim_tracking_lag = std::clamp((1.0f - prediction_response) * (m_aim_tracking_lag + dt), 0.0f, 0.12f);

        auto target_velocity = tgt.player->velocity;
        auto local_velocity = game::bound_memory().process.load<foundation::vec3>(
            local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
        const auto valid_velocity = [](const foundation::vec3& velocity)
        {
            return std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z) &&
                   velocity.length_sqr() < 4'000'000.0f;
        };
        if (!valid_velocity(target_velocity))
            target_velocity = {};
        if (!valid_velocity(local_velocity))
            local_velocity = {};
        const auto target_flags =
            game::bound_memory().process.load<std::uint32_t>(tgt.player->pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id));
        const auto local_flags =
            game::bound_memory().process.load<std::uint32_t>(local_pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id));
        const auto target_airborne = (target_flags & 1u) == 0u;
        const auto local_airborne = (local_flags & 1u) == 0u;
        if (!target_airborne && !local_airborne)
        {
            target_velocity.z = 0.0f;
            local_velocity.z = 0.0f;
        }

        const auto relative_velocity = target_velocity - local_velocity;
        const auto new_simulation_sample =
            tgt.player->simulation_tick >= 0 && tgt.player->simulation_tick != m_aim_simulation_tick;
        if (!m_aim_velocity_valid)
        {
            m_aim_previous_relative_velocity = relative_velocity;
            m_aim_relative_acceleration = {};
            m_aim_velocity_valid = true;
            m_aim_velocity_samples = 1;
            m_aim_simulation_tick = tgt.player->simulation_tick;
            m_aim_simulation_time = tgt.player->simulation_time;
        }
        else if (new_simulation_sample)
        {
            const auto sample_dt =
                std::max(tgt.player->simulation_time - m_aim_simulation_time, game::rules::simulation_step);
            auto observed_acceleration = (relative_velocity - m_aim_previous_relative_velocity) / sample_dt;
            const auto acceleration_length = observed_acceleration.length();
            const auto direction_break =
                relative_velocity.length_2d() > 40.0f && m_aim_previous_relative_velocity.length_2d() > 40.0f &&
                relative_velocity.normalized().dot(m_aim_previous_relative_velocity.normalized()) < -0.25f;
            if (acceleration_length > 3500.0f || direction_break)
            {
                m_aim_relative_acceleration = {};
                m_aim_velocity_samples = 1;
            }
            else
            {
                const auto acceleration_blend = 1.0f - std::exp(-sample_dt / 0.04f);
                m_aim_relative_acceleration = m_aim_relative_acceleration * (1.0f - acceleration_blend) +
                                              observed_acceleration * acceleration_blend;
                ++m_aim_velocity_samples;
            }
            m_aim_previous_relative_velocity = relative_velocity;
            m_aim_simulation_tick = tgt.player->simulation_tick;
            m_aim_simulation_time = tgt.player->simulation_time;
        }

        const auto gravity = game::variables().get<float>(CONVAR("sv_gravity"_id));
        if (std::isfinite(gravity) && gravity > 0.0f && gravity <= 2000.0f)
        {
            m_aim_relative_acceleration.z =
                target_airborne == local_airborne ? 0.0f : (target_airborne ? -gravity : gravity);
        }

        const auto lead =
            std::clamp(dt + m_aim_tracking_lag, 0.0f, std::clamp(cfg.prediction.max_horizon_ms, 0.0f, 120.0f) * 0.001f);
        aim_point += relative_velocity * lead + (cfg.prediction.acceleration && m_aim_velocity_samples >= 3
                                                     ? m_aim_relative_acceleration * (0.5f * lead * lead)
                                                     : foundation::vec3{});

        desired = target_angle(aim_point);
        delta_x = desired.x - control_angles.x;
        delta_y = foundation::wrap_yaw(desired.y - control_angles.y);
        dist = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    }
    else
    {
        m_aim_tracking_lag = 0.0f;
        m_aim_velocity_valid = false;
        m_aim_velocity_samples = 0;
    }
    // Prediction is allowed to refine the selected solution, but it may not pull
    // the input outside the FOV policy that selected this target.
    if (get_fov(view_angles, eye_pos, aim_point) > selection_fov(cfg, eye_pos, aim_point))
    {
        return;
    }
    if (h > 0.0f)
    {
        if (dist < h * std::clamp(cfg.humanizer.deadzone, 0.0f, 2.0f))
            return;

        m_aim_wander_phase += m_aim_wander_freq * dt;
        const auto settle = std::clamp((dist - 0.20f) / 0.90f, 0.0f, 1.0f);
        const auto wander_amp =
            h * std::clamp(cfg.humanizer.wind, 0.0f, 20.0f) * 0.004f * std::min(dist, 2.0f) * settle * settle;
        const auto micro_amp = h * std::clamp(cfg.humanizer.jitter, 0.0f, 3.0f) * 0.0025f * settle * settle;
        delta_x += wander_amp * std::sin(m_aim_wander_phase) + micro_amp * std::sin(m_aim_wander_phase * 1.73f + 0.7f);
        delta_y +=
            wander_amp * 0.7f * std::cos(m_aim_wander_phase * 1.3f) + micro_amp * std::cos(m_aim_wander_phase * 1.47f);
    }

    const auto max_step = std::clamp(cfg.humanizer.max_step, 1.0f, 90.0f);
    // A large turn is one hand movement, timed and shaped like a person's flick: the duration grows with the logarithm
    // of the angle (Fitts: a 180 degree turn takes only about twice as long as a 10 degree one), the speed follows the
    // minimum-jerk profile (smooth start, peak in the middle, smooth stop: no jump to full speed, no shake), and each
    // tick covers its share of what is still left, so a moving target is followed. The hand lands a little short (or
    // now and then past); the fine smoothing below finishes, and a miss of more than the fine zone is a second, shorter
    // flick, the way a person corrects. Max Step scales the speed (24 is the reference).
    constexpr float k_fine_zone = 4.0f;
    const auto flick = h > 0.0f && dist > k_fine_zone;
    if (flick)
    {
        if (!m_flick_active)
        {
            m_flick_duration = flick::duration(dist, std::sqrt(max_step / 24.0f)) * m_rng.uniform(0.9f, 1.12f);
            m_flick_progress = 0.0f;
            // Mostly a little short; past the target as often as the overshoot settings ask (their amount caps it).
            m_flick_bias = m_aim_overshoot > 1.0f ? std::min(m_aim_overshoot - 1.0f, 0.08f) * m_rng.uniform(0.4f, 1.0f)
                                                  : -m_rng.uniform(0.01f, 0.06f);
            m_aim_overshoot = 1.0f; // used up by this flick
            m_flick_active = true;
        }
        const auto from = m_flick_progress;
        const auto to = std::min(1.0f, from + dt / std::max(m_flick_duration, 1e-3f));
        auto share = flick::share(from, to);
        m_flick_progress = to;
        if (to >= 1.0f)
        {
            share *= 1.0f + m_flick_bias;
            m_flick_active = false;
        }
        delta_x *= share;
        delta_y *= share;
        m_aim_flick_speed = dt > 0.0f ? std::sqrt(delta_x * delta_x + delta_y * delta_y) / dt : 0.0f;
    }
    else
    {
        m_flick_active = false;
        delta_x *= response_alpha;
        delta_y *= response_alpha;
        const auto sampled_max_step = max_step * command_fraction;
        const auto step_length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
        if (h > 0.0f && step_length > sampled_max_step)
        {
            delta_x *= sampled_max_step / step_length;
            delta_y *= sampled_max_step / step_length;
        }
        // Hand the speed back to the next flick so it does not restart from rest mid-motion.
        m_aim_flick_speed = dt > 0.0f ? std::sqrt(delta_x * delta_x + delta_y * delta_y) / dt : 0.0f;
    }

    if (h > 0.0f && flick)
    {
        // Arc: a flick does not travel in a straight line; each step is bent sideways, peaking mid-flick and exactly
        // zero at its end, so it never leaves the crosshair above or below the hitbox (the fine phase is straight).
        {
            const auto arc =
                m_aim_curve * std::sin(std::clamp(m_flick_progress, 0.0f, 1.0f) * std::numbers::pi_v<float>);
            const auto base_x = delta_x;
            const auto base_y = delta_y;
            const auto curved_x = base_x - base_y * arc;
            const auto curved_y = base_y + base_x * arc;
            const auto bounded_component = [](const float direct, const float curved)
            { return std::clamp(curved, std::min(direct, 0.0f), std::max(direct, 0.0f)); };
            delta_x = bounded_component(base_x, curved_x);
            delta_y = bounded_component(base_y, curved_y);
        }
    }

    auto move_x = -delta_y / deg_per_pixel;
    auto move_y = delta_x / deg_per_pixel;

    const auto accumulated_x = (snap ? 0.0f : m_aim_error.x) + move_x;
    const auto accumulated_y = (snap ? 0.0f : m_aim_error.y) + move_y;
    const auto dx = snap ? static_cast<int>(std::lround(accumulated_x)) : static_cast<int>(accumulated_x);
    const auto dy = snap ? static_cast<int>(std::lround(accumulated_y)) : static_cast<int>(accumulated_y);
    const foundation::vec2 remainder{snap ? 0.0f : accumulated_x - static_cast<float>(dx),
                                     snap ? 0.0f : accumulated_y - static_cast<float>(dy)};
    if (dx == 0 && dy == 0 && std::abs(m_rcs_pending_mouse.x) < 1.0f && std::abs(m_rcs_pending_mouse.y) < 1.0f)
    {
        m_aim_error = remainder;
        return;
    }

    if (dx != 0 || dy != 0 || std::abs(m_rcs_pending_mouse.x) >= 1.0f || std::abs(m_rcs_pending_mouse.y) >= 1.0f)
    {
        if (!config::combat_profile::activation_active(cfg.activation_mode, cfg.key) ||
            local_checks_blocked(cfg.checks) ||
            live_target_invulnerable(tgt.player ? tgt.player->pawn : 0, tgt.player ? tgt.player->invulnerable : true) ||
            (cfg.checks.smoke && line_through_smoke(eye_pos, aim_point)))
        {
            return;
        }
        const auto live_visible =
            !simulation::collision().valid() || !simulation::collision().trace_ray(eye_pos, aim_point).hit;
        if (!live_visible)
        {
            if (cfg.checks.walls == config::combat_profile::wall_policy::block)
                return;
            if (cfg.checks.walls == config::combat_profile::wall_policy::penetration)
            {
                ballistics_t::penetration::result penetration{};
                if (!simulation::ballistics().pen().run(eye_pos, aim_point, *tgt.player, freshest, penetration) ||
                    penetration.damage < cfg.min_damage)
                    return;
            }
        }
        const auto rcs_limit = std::max(m_rcs_input_step, 1);
        const auto rcs_dx = std::clamp(static_cast<int>(m_rcs_pending_mouse.x), -rcs_limit, rcs_limit);
        const auto rcs_dy = std::clamp(static_cast<int>(m_rcs_pending_mouse.y), -rcs_limit, rcs_limit);
        if (platform::windows::bound_input().pointer(dx + rcs_dx, dy + rcs_dy,
                                                     platform::windows::pointer_action::relative_move))
        {
            m_aim_error = remainder;
            m_aim_virtual_angles.x += static_cast<float>(dy) * deg_per_pixel;
            m_aim_virtual_angles.y =
                foundation::wrap_yaw(m_aim_virtual_angles.y - static_cast<float>(dx) * deg_per_pixel);
            if (dx + rcs_dx || dy + rcs_dy)
            {
                if (m_aim_pending_since.time_since_epoch().count() == 0)
                    m_aim_pending_since = now;
                m_aim_sent_since_sample.x += static_cast<float>(dy + rcs_dy) * deg_per_pixel;
                m_aim_sent_since_sample.y -= static_cast<float>(dx + rcs_dx) * deg_per_pixel;
            }
            m_aim_last_input_sequence = input_sequence;
            m_rcs_last_input_sequence = input_sequence;
            m_rcs_last_input_time = now;
            m_rcs_pending_mouse.x -= static_cast<float>(rcs_dx);
            m_rcs_pending_mouse.y -= static_cast<float>(rcs_dy);
        }
    }
}


} // namespace features::aimbot
