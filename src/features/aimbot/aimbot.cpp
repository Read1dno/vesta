#include <stdafx.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
void aimbot_t::reset()
{
    if (m_revolver_pre_cock_down)
    {
        platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_revolver_pre_cock_down = false;
    }
    if (m_trigger_held)
    {
        auto released = true;
        if (m_seed_held_secondary)
        {
            platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::secondary_up);
        }
        else if (m_seed_held_proxy)
        {
            platform::windows::bound_input().key(VK_F24, false);
        }
        else
        {
            released = platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        }

        if (released)
            m_trigger_held = false;
    }
    m_trigger_press_shots = -1;
    m_seed_press_active = false;
    m_seed_held_secondary = false;
    m_seed_held_proxy = false;
    m_revolver_committed = false;
    m_revolver_weapon = 0;

    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_cooldown_until = {};
    m_trigger_target_valid = false;
    m_seed_last_shots = -1;
    m_seed_pending_target_tick = -1;
    m_seed_pending_time = {};
    m_seed_phase_receipt = {};
    m_seed_memo_sequence = -1;
    m_seed_memo_pitch = {};
    m_seed_memo_yaw = {};
    m_last_time = 0.0f;
    m_aim_pawn = 0;
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_selection_view = {};
    m_aim_selection_weapon = 0;
    m_aim_selection_at = {};
    m_aim_last_bone = -1;
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_last_camera_angles = {};
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_virtual_angles = {};
    m_aim_virtual_angles_valid = false;
    m_aim_degrees_per_pixel = {};
    m_aim_degrees_candidate = {};
    m_aim_degrees_confirmations = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_previous_relative_velocity = {};
    m_aim_relative_acceleration = {};
    m_aim_velocity_valid = false;
    m_aim_simulation_tick = -1;
    m_aim_simulation_time = 0.0f;
    m_aim_velocity_samples = 0;
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

    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

void aimbot_t::on_render(render::draw::draw_list& draw_list)
{
    refresh_runtime_config();
    const auto eye_pos = game::camera().origin();
    const auto view_angles = game::camera().angles();

    const auto& ctx = ballistics().ctx();
    const auto cfg = runtime_config()->combat.get(ctx.weapon_type);

    if (!ctx.valid)
    {
        return;
    }

    const auto valid_weapon = game::rules::is_firearm(ctx.weapon_type);

    if (valid_weapon && cfg.other.penetration_crosshair)
    {
        const auto global_vars = game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().global_vars);
        const auto current_time = global_vars ? game::bound_memory().process.load<float>(global_vars + 0x30) : 0.0f;
        draw_penetration_crosshair(draw_list, eye_pos, view_angles, cfg, current_time);
    }

    const auto draw_aim_visualization = cfg.aimbot.enabled && cfg.aimbot.draw_fov;
    if (!draw_aim_visualization)
    {
        m_fov_alpha.snap(0.0f);
        return;
    }
    m_fov_alpha.set_target(valid_weapon && draw_aim_visualization ? 1.0f : 0.0f);
    m_fov_alpha.update(render::draw::get_delta_time());

    if (m_fov_alpha.value() <= 0.01f)
    {
        return;
    }

    draw_fov_ring(draw_list, eye_pos, view_angles, cfg.aimbot);
}

// Revolver pre-cock: holds the trigger while the hammer is cocking and lets go shortly before it would fire, so the
// trigger bot's own press fires at once.
void aimbot_t::update_revolver_pre_cock(const ballistics_t::context& ctx, const config::combat_profile::triggerbot& cfg)
{
    constexpr std::uint16_t revolver_id{64};
    auto& input = platform::windows::bound_input();
    const auto revolver_available = ctx.valid && ctx.item_def_idx == revolver_id && cfg.enabled &&
                                    cfg.revolver_pre_cock && !ctx.is_reloading && ctx.clip != 0;
    if (m_revolver_pre_cock_down && (!revolver_available || m_revolver_weapon != ctx.weapon))
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    if (!revolver_available || m_trigger_held)
        return;

    m_revolver_weapon = ctx.weapon;
    const auto ready_tick = ctx.postpone_fire_ready_tick;
    const auto ticks_left = ready_tick > 0 ? ready_tick - ctx.player_tick : std::numeric_limits<int>::max();
    const auto ready_fraction = std::isfinite(ctx.postpone_fire_ready_fraction)
                                    ? std::clamp(ctx.postpone_fire_ready_fraction, 0.0f, 1.0f)
                                    : 0.0f;
    const auto milliseconds_left =
        ticks_left == std::numeric_limits<int>::max()
            ? std::numeric_limits<float>::max()
            : (static_cast<float>(ticks_left) + ready_fraction) * game::rules::simulation_step * 1000.0f;
    const auto release_margin = static_cast<float>(std::clamp(cfg.revolver_release_margin_ms, 1, 60));
    if (m_revolver_pre_cock_down && ready_tick > 0 && milliseconds_left <= release_margin)
    {
        if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
            m_revolver_pre_cock_down = false;
    }
    else if (!m_revolver_pre_cock_down && (ready_tick <= 0 || ready_tick <= ctx.player_tick))
    {
        m_revolver_pre_cock_down = input.pointer(0, 0, platform::windows::pointer_action::primary_down);
    }
}

void aimbot_t::cancel_scheduled_trigger()
{
    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_target_valid = false;
    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
}

void aimbot_t::reset_aim_tracking()
{
    m_aim_error = {};
    m_aim_last_input_sequence = -1;
    m_aim_virtual_angles_valid = false;
    m_aim_sent_since_sample = {};
    m_aim_pending_since = {};
    m_aim_pawn = 0;
    m_aim_selection_world.reset();
    m_aim_selection_target = {};
    m_aim_tracking_lag = 0.0f;
    m_aim_velocity_valid = false;
}

// Ends a trigger press once the game consumed the shot (a newer command tick for a seed press, or a higher shot
// count), once the press timed out, or when a seed press lost its weapon context. A seed press is released
// unconditionally; a normal press only when the release went through.
void aimbot_t::release_finished_press(const ballistics_t::context& ctx, const std::chrono::steady_clock::time_point now)
{
    const auto shots_fired = [](const std::uintptr_t pawn)
    {
        return pawn ? game::bound_memory().process.load<std::int32_t>(pawn +
                                                                      SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id))
                    : -1;
    };
    bool command_consumed = false;
    if (ctx.valid && m_seed_press_active && m_seed_pending_target_tick >= 0)
    {
        const auto pawn = game::local_player().pawn();
        const auto command_tick = read_seed_tick();
        const auto shots = shots_fired(pawn);
        command_consumed =
            command_tick > m_seed_pending_target_tick || (m_seed_last_shots >= 0 && shots > m_seed_last_shots);
    }
    else if (!m_seed_press_active && m_trigger_press_shots >= 0)
    {
        command_consumed = shots_fired(game::local_player().pawn()) > m_trigger_press_shots;
    }

    const auto context_lost = !ctx.valid && m_seed_press_active;
    if (!context_lost && !command_consumed && now < m_trigger_release_time)
        return;
    if (command_consumed)
        features::visuals::event_log().mark_latest_trigger_consumed();
    auto& input = platform::windows::bound_input();
    if (m_seed_press_active)
    {
        input.pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_trigger_held = false;
        m_seed_press_active = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
    else if (input.pointer(0, 0, platform::windows::pointer_action::primary_up))
    {
        m_trigger_held = false;
        m_trigger_press_shots = -1;
        m_revolver_committed = false;
    }
}

// A shot count past the one recorded for the pending seed press means that press already fired.
void aimbot_t::observe_shots_fired(const std::uintptr_t pawn)
{
    if (!pawn)
        return;
    const auto shots_fired =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
    if (m_seed_last_shots >= 0 && shots_fired > m_seed_last_shots && m_seed_pending_target_tick >= 0)
        m_seed_pending_target_tick = -1;
    m_seed_last_shots = shots_fired;
}

// Remembers the target the FOV indicator follows (drawn by on_render).
void aimbot_t::publish_indicator(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                                 const target& picked)
{
    if (!picked.player)
        return;
    auto point = picked.aim_point;
    if (picked.bone >= 0 && picked.bone < 128 && picked.bones.is_valid())
    {
        const auto& bone = picked.bones.bones[picked.bone];
        point = bone.position + bone.rotation.apply(picked.aim_offset);
    }
    std::scoped_lock lock(m_indicator_mutex);
    m_indicator_point = point;
    m_indicator_fov = selection_fov(cfg, eye, point);
    m_indicator_time = std::chrono::steady_clock::now();
    m_indicator_pawn = picked.player->pawn;
    m_indicator_bone_cache = picked.player->bone_cache;
    m_indicator_bone = picked.bone;
    m_indicator_offset = picked.aim_offset;
}

// Chooses the aim target, reusing the last choice for the same world sample within 16 ms, and steers towards it.
// Returns false when the aimbot's local checks block this frame.
bool aimbot_t::select_and_aim(const combat_frame& frame, const config::combat_profile::resolved_config& cfg,
                              const bool visual_scan_requested, const bool visual_scan_due)
{
    if (local_checks_blocked(cfg.aimbot.checks))
    {
        m_aim_pawn = 0;
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
        return false;
    }
    constexpr auto reuse_window = std::chrono::milliseconds(16);
    constexpr auto indicator_period = std::chrono::milliseconds(33);
    const auto selection_now = std::chrono::steady_clock::now();
    const auto view_delta = std::hypot(frame.view_angles.x - m_aim_selection_view.x,
                                       foundation::wrap_yaw(frame.view_angles.y - m_aim_selection_view.y));
    const auto can_reuse = m_aim_selection_world == frame.targets && m_aim_selection_weapon == frame.weapon.weapon &&
                           selection_now - m_aim_selection_at < reuse_window &&
                           (m_aim_selection_target.player || view_delta < 0.5f);
    auto selected_target =
        can_reuse ? m_aim_selection_target : choose_target(frame.eye, frame.view_angles, *frame.targets, cfg);
    if (selected_target.player)
    {
        selected_target.fov = get_fov(frame.view_angles, frame.eye, selected_target.aim_point);
        if (selected_target.fov > selection_fov(cfg.aimbot, frame.eye, selected_target.aim_point))
            selected_target = {};
    }
    if (!can_reuse)
    {
        m_aim_selection_world = frame.targets;
        m_aim_selection_target = selected_target;
        m_aim_selection_view = frame.view_angles;
        m_aim_selection_weapon = frame.weapon.weapon;
        m_aim_selection_at = std::chrono::steady_clock::now();
    }
    if (selected_target.player && visual_scan_requested)
    {
        publish_indicator(cfg.aimbot, frame.eye, selected_target);
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    else if (!selected_target.player && visual_scan_due &&
             cfg.aimbot.fov_config.selection == config::combat_profile::fov_settings::target_distance)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(cfg.aimbot, frame.eye,
                          choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg, false));
        m_next_visual_scan = std::chrono::steady_clock::now() + indicator_period;
    }
    if (selected_target.player)
    {
        note_view_engaged(selected_target.player->pawn);
        aimbot(frame.eye, frame.view_angles, selected_target, cfg.aimbot);
    }
    return true;
}

void aimbot_t::tick()
{
    refresh_runtime_config();
    const auto ctx = ballistics().ctx();
    if (ctx.valid)
    {
        // The game time went backwards: a new match or map.
        if (ctx.current_time < m_last_time - 0.5f)
            reset();
        m_last_time = ctx.current_time;
    }

    if (!m_rng_seeded)
    {
        m_rng.seed(static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff));
        m_rng_seeded = true;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto requested_cfg = runtime_config()->combat.get(game::local_player().weapon_type());
    const auto walk_autofire = g_walk_autofire.load(std::memory_order_acquire);
    const auto trigger_activation_active =
        walk_autofire || (requested_cfg.triggerbot.enabled &&
                          config::combat_profile::activation_active(requested_cfg.triggerbot.activation_mode,
                                                                    requested_cfg.triggerbot.key));

    update_revolver_pre_cock(ctx, requested_cfg.triggerbot);

    // The trigger key was let go during a normal press: release it now.
    if (m_trigger_held && !m_seed_press_active && !trigger_activation_active)
    {
        if (platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up))
        {
            m_trigger_held = false;
            m_trigger_press_shots = -1;
            m_trigger_release_time = {};
            m_revolver_committed = false;
        }
        cancel_scheduled_trigger();
    }
    if (m_trigger_held)
        release_finished_press(ctx, now);

    const auto aimbot_requested =
        requested_cfg.aimbot.enabled &&
        config::combat_profile::activation_active(requested_cfg.aimbot.activation_mode, requested_cfg.aimbot.key);
    if (!aimbot_requested)
    {
        m_aim_selection_world.reset();
        m_aim_selection_target = {};
    }
    // With an independent seed mode the seed thread presses; this trigger bot stays idle.
    const auto independent_seed = requested_cfg.triggerbot.seed_type != config::combat_profile::seed_mode::none;
    const auto trigger_requested =
        !independent_seed &&
        (walk_autofire || (requested_cfg.triggerbot.enabled &&
                           config::combat_profile::activation_active(requested_cfg.triggerbot.activation_mode,
                                                                     requested_cfg.triggerbot.key)));
    const auto dynamic_fov_visualization_requested =
        requested_cfg.aimbot.enabled && requested_cfg.aimbot.draw_fov &&
        requested_cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;

    // Nothing else refreshes the weapon context while only the FOV ring needs it.
    if (dynamic_fov_visualization_requested && now >= m_next_visual_scan && !aimbot_requested && !trigger_requested)
        simulation::ballistics().tick();

    recoil_control(requested_cfg.aimbot, aimbot_requested && ctx.valid);
    if (!aimbot_requested && !trigger_requested && !m_trigger_held && !dynamic_fov_visualization_requested)
    {
        reset_aim_tracking();
        cancel_scheduled_trigger();
        return;
    }

    // A shot scheduled by the trigger bot fires on time even without a fresh target list.
    if (trigger_requested && m_trigger_shot_scheduled)
    {
        static const std::vector<game::player_snapshot> no_targets{};
        triggerbot({}, {}, no_targets, requested_cfg.triggerbot);
    }

    if (!ctx.valid)
        return;

    const auto cfg = runtime_config()->combat.get(ctx.weapon_type);
    const auto pawn = game::local_player().pawn();
    observe_shots_fired(pawn);
#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
    research::impact_probe_poll(pawn, now);
    research::seed_stats_flush(now);
#endif

    if (!game::rules::is_firearm(ctx.weapon_type))
        return;
    const auto visualization_only =
        dynamic_fov_visualization_requested && !aimbot_requested && !trigger_requested && !m_trigger_held;
    if (visualization_only && now < m_next_visual_scan)
        return;

    combat_frame frame{};
    frame.sequence = ++m_combat_sequence;
    frame.timestamp = now;
    frame.camera = game::camera().matrix();
    frame.eye = game::camera().origin();
    frame.view_angles = game::camera().angles();
    static_cast<void>(game::camera().sample(frame.eye, frame.view_angles));
    frame.local_pawn = pawn;
    frame.flash_alpha = game::local_player().flash_alpha();
    frame.weapon = ctx;
    frame.targets = game::world().players();
    if (!frame.targets)
    {
        flush_recoil_input();
        return;
    }

    const auto visual_scan_requested = cfg.aimbot.enabled && cfg.aimbot.draw_fov &&
                                       cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::fixed;
    const auto visual_scan_due = visual_scan_requested && now >= m_next_visual_scan;
    const auto aim_can_select = aimbot_requested && cfg.aimbot.enabled &&
                                config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key) &&
                                !m_trigger_held && !ctx.is_reloading && ctx.clip != 0 &&
                                (!visual_scan_due || !local_checks_blocked(cfg.aimbot.checks));
    // The FOV indicator follows the would-be target even while the aimbot is not selecting.
    if (visual_scan_due && !aim_can_select)
    {
        auto visual_cfg = cfg;
        visual_cfg.aimbot.prediction.enabled = false;
        publish_indicator(
            cfg.aimbot, frame.eye,
            choose_target(frame.eye, frame.view_angles, *frame.targets, visual_cfg,
                          cfg.aimbot.fov_config.selection != config::combat_profile::fov_settings::target_distance));
        m_next_visual_scan = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
    }
    if (visualization_only)
        return;
    if (ctx.is_reloading || ctx.clip == 0)
    {
        cancel_scheduled_trigger();
        flush_recoil_input();
        return;
    }

    if ((cfg.triggerbot.enabled || walk_autofire) &&
        cfg.triggerbot.seed_type == config::combat_profile::seed_mode::none)
        triggerbot(frame.eye, frame.view_angles, *frame.targets, cfg.triggerbot);

    if (m_trigger_held)
        note_view_engaged(m_trigger_target_valid ? m_trigger_target.pawn : 0);
    if (cfg.aimbot.enabled && !m_trigger_held)
    {
        if (!config::combat_profile::activation_active(cfg.aimbot.activation_mode, cfg.aimbot.key))
            reset_aim_tracking();
        else if (!select_and_aim(frame, cfg, visual_scan_requested, visual_scan_due))
        {
            flush_recoil_input();
            return;
        }
    }
    flush_recoil_input();
}


} // namespace features::aimbot
