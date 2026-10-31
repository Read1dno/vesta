#include <stdafx.hpp>
#include <render/presentation.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
namespace
{
constexpr auto k_nan = std::numeric_limits<float>::quiet_NaN();

// Shot trace records (Debug builds with tracing enabled).
void trace_snapshot(simulation::shot_trace::snapshot_record& out, const ballistics_t::context& ctx)
{
    out.weapon = ctx.weapon;
    out.tick_base = ctx.player_tick;
    out.weapon_vdata = ctx.weapon_vdata;
    out.fire_mode = ctx.fire_mode;
    out.pattern_seed = ctx.pattern_seed;
    out.num_bullets = ctx.num_bullets;
    out.ground_entity = ctx.ground_entity;
    out.walking = ctx.is_walking;
    out.on_ground = ctx.on_ground;
    out.current_time = k_nan;
    out.wat = k_nan;
    out.last_shot_time = k_nan;
    out.clip = ctx.clip;
    out.item = ctx.item_def_idx;
    out.next_attack = -1;
    out.weapon_ready = ctx.weapon_ready;
    out.reloading = ctx.is_reloading;
    out.inaccuracy = ctx.inaccuracy;
    out.spread = ctx.spread;
    out.recoil = ctx.recoil_index;
    const auto& d = ctx.debug;
    out.accuracy_terms = {d.accuracy_penalty,
                          d.turning_inaccuracy,
                          d.move_inaccuracy,
                          d.air_inaccuracy,
                          d.strafing_inaccuracy,
                          d.move_factor,
                          d.max_speed.first,
                          d.max_speed.second,
                          d.inaccuracy_move.first,
                          d.inaccuracy_move.second,
                          d.inaccuracy_jump_initial,
                          d.inaccuracy_jump_apex,
                          static_cast<float>(ctx.is_walking),
                          static_cast<float>(ctx.on_ground),
                          k_nan,
                          ctx.postpone_fire_ready_fraction,
                          d.eye_angles.x,
                          d.eye_angles.y,
                          ctx.velocity_length,
                          ctx.velocity_length};
}

template<typename plan_t>
[[nodiscard]] simulation::shot_trace::plan_record trace_plan(const plan_t& p)
{
    return simulation::shot_trace::plan_record{p.current.tick,          p.next.tick,        static_cast<int>(p.phase),
                                               p.current.fraction,      p.next.fraction,    p.source_angles,
                                               p.current.hash_angles,   p.next.hash_angles, p.current.direction_angles,
                                               p.next.direction_angles, p.current.punch,    p.next.punch};
}

[[nodiscard]] simulation::shot_trace::target_record trace_target(const std::uint64_t cycle_id,
                                                                 const game::player_snapshot& player, const int stage)
{
    simulation::shot_trace::target_record result{};
    result.cycle_id = cycle_id;
    result.stage = stage;
    result.pawn = player.pawn;
    result.controller = player.controller;
    result.health = player.health;
    result.armor = player.armor;
    result.simulation_tick = player.simulation_tick;
    result.simulation_time = player.simulation_time;
    result.helmet = player.has_helmet;
    result.origin = player.origin;
    result.velocity = player.velocity;
    for (const auto& box : player.hitboxes)
    {
        if (result.count == result.capsules.size())
            break;
        if (box.index < 0 || box.bone < 0 || box.bone >= player.bones.bones.size())
            continue;
        const auto capsule = game::place_hitbox(player.bones.bones[box.bone], box);
        result.capsules[result.count++] = {
            box.index,     box.bone,    game::hitbox_data().hitgroup_from_hitbox(box.index),
            capsule.start, capsule.end, capsule.radius};
    }
    return result;
}
} // namespace

void aimbot_t::reset_seed()
{
    m_seed_reaction.reset();
    if (m_revolver_pre_cock_down)
    {
        platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_revolver_pre_cock_down = false;
    }
    end_seed_press();
    m_trigger_press_shots = -1;
    m_revolver_committed = false;
    m_revolver_weapon = 0;
    m_trigger_release_time = {};
    m_seed_pending_target_tick = -1;
    m_seed_pending_time = {};
    m_seed_phase_receipt = {};
    m_seed_network_window = {};
    m_seed_last_shots = -1;
    m_seed_last_host_mode = -1;
    m_seed_memo_sequence = -1;
    m_seed_memo_pitch = k_nan;
    m_seed_memo_yaw = k_nan;
    m_seed_memo_next_pitch = k_nan;
    m_seed_memo_next_yaw = k_nan;
    m_seed_memo_phase = seed_prediction_phase::current;
    m_seed_angle_history_count = 0;
    m_seed_phase_tick = -1;
    m_seed_phase_tick_at = {};
    m_seed_phase_sampled_at = {};
    m_seed_phase_observation = {};
    m_seed_targets.clear();
    m_seed_player_buffer.clear();
    m_seed_last_controller = 0;
    m_seed_last_pawn = 0;
    m_seed_last_tick = -1;
    m_seed_tick_observed_at = {};
    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::seed_trigger);
}

bool aimbot_t::seed_hot_path_requested() const noexcept
{
    refresh_runtime_config();
    return m_trigger_held ||
           (runtime_config()->combat.seed_trigger_configured() &&
            config::combat_profile::activation_active(runtime_config()->combat.global.triggerbot_activation_mode,
                                                      runtime_config()->combat.global.triggerbot_key));
}

void aimbot_t::end_seed_press()
{
    if (m_trigger_held)
    {
        auto& input = platform::windows::bound_input();
        if (m_seed_held_secondary)
            input.pointer(0, 0, platform::windows::pointer_action::secondary_up);
        else if (m_seed_held_proxy)
            input.key(VK_F24, false);
        else
            input.pointer(0, 0, platform::windows::pointer_action::primary_up);
    }
    m_trigger_held = false;
    m_seed_press_active = false;
    m_seed_held_secondary = false;
    m_seed_held_proxy = false;
}

void aimbot_t::observe_consumed_shot(const std::uintptr_t pawn, const int seed_tick, const int shots_fired,
                                     const std::chrono::steady_clock::time_point now, const bool tracing)
{
    const auto shot_consumed = m_seed_last_shots >= 0 && shots_fired > m_seed_last_shots;
    m_seed_last_shots = shots_fired;
    if (!shot_consumed)
        return;

    if (tracing)
        simulation::shot_trace::consumed(pawn, seed_tick, shots_fired);
    if (m_seed_phase_receipt.weapon && m_seed_phase_receipt.pressed_at <= now)
        m_seed_phase_receipt.shot_seen = true;
    features::visuals::event_log().mark_latest_trigger_consumed();
#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
    if (research::g_press_sequence >= 0)
    {
        foundation::vec3 shot_angles{};
        int shot_sequence{-1};
        (void)m_seed_commands.read_angles(shot_angles, &shot_sequence);
        const auto latency_ms = std::chrono::duration<float, std::milli>(now - research::g_press_at).count();
        VESTA_SEED_COUNT(shots);
        research::seed_file_write(std::format("SHOT research::unix_ms={} press_seq={} shot_seq={} seq_delta={} "
                                              "press_tick={} shot_tick={} tick_delta={} "
                                              "shot_angle=({:.4f},{:.4f}) latency={:.3f}",
                                              research::unix_ms(), research::g_press_sequence, shot_sequence,
                                              shot_sequence >= 0 ? shot_sequence - research::g_press_sequence : -1,
                                              research::g_press_tick, seed_tick, seed_tick - research::g_press_tick,
                                              shot_angles.x, shot_angles.y, latency_ms));
        research::g_press_sequence = -1;
    }
#endif
    end_seed_press();
    m_seed_pending_target_tick = -1;
    features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::seed_trigger);
}

void aimbot_t::update_network_receipt(const std::uintptr_t controller, const std::chrono::steady_clock::time_point now)
{
    if (!m_seed_phase_receipt.weapon)
        return;

    auto& receipt = m_seed_phase_receipt;
    int feedback_player_tick{};
    if (!game::bound_memory().process.copy(controller + SCHEMA("CBasePlayerController", "m_nTickBase"_id),
                                           &feedback_player_tick, sizeof(feedback_player_tick)) ||
        feedback_player_tick < receipt.player_tick)
    {
        receipt.feedback_valid = false;
        m_seed_network_window.early_streak = m_seed_network_window.late_streak = 0;
    }
    receipt.player_tick = std::max(receipt.player_tick, feedback_player_tick);
    if (now - receipt.pressed_at > std::chrono::milliseconds(500))
        receipt = {};
    else if (receipt.shot_seen)
    {
        const auto time_offset = SCHEMA("C_CSWeaponBase", "m_fLastShotTime"_id);
        const auto wat_offset = SCHEMA("C_CSWeaponBase", "m_flWatTickOffset"_id);
        float shot_time{}, wat{}, confirmed_time{}, confirmed_wat{};
        auto& process = game::bound_memory().process;
        if (time_offset > 0 && wat_offset > 0 &&
            process.copy(receipt.weapon + time_offset, &shot_time, sizeof(shot_time)) &&
            process.copy(receipt.weapon + wat_offset, &wat, sizeof(wat)) &&
            process.copy(receipt.weapon + time_offset, &confirmed_time, sizeof(confirmed_time)) &&
            process.copy(receipt.weapon + wat_offset, &confirmed_wat, sizeof(confirmed_wat)) &&
            shot_time == confirmed_time && wat == confirmed_wat && std::isfinite(shot_time) && shot_time > 0.0f &&
            shot_time > receipt.previous_shot_time)
        {
            if (const auto clock = simulation::infer_shot_clock(shot_time, wat);
                receipt.feedback_valid && clock && clock->minimum_tick == clock->maximum_tick)
                m_seed_network_window.observe(receipt.phase_us, clock->minimum_tick - receipt.selected_tick);
            receipt = {};
        }
    }
}

bool aimbot_t::seed_sample_hits(ballistics_t& ballistics, const config::combat_profile::triggerbot& cfg,
                                const std::vector<game::player_snapshot>& candidates, const seed_shooter& shooter,
                                const std::uintptr_t target_pawn, const int required_hitbox,
                                const seed_shot_sample& sample, simulation::shot_trace::ray_sample* const traced_ray)
{
    const auto target = std::ranges::find(candidates, target_pawn, &game::player_snapshot::pawn);
    if (target == candidates.end() || target->invulnerable || target->health <= 0 || !target->bones.is_valid())
        return false;

    const auto& weapon = shooter.weapon;
    auto origin = shooter.eye;
    if (cfg.predictive)
    {
        const auto horizon = std::max(0, sample.tick - shooter.tick);
        origin += shooter.velocity * (static_cast<float>(horizon) / 64.0f);
    }

    foundation::vec3 forward{}, right{}, up{};
    sample.direction_angles.to_directions(&forward, &right, &up);
    const auto spread_seed = ballistics.derive_command_seed(sample.hash_angles, sample.tick) + 1u;
    const auto bullet_count = std::clamp(weapon.num_bullets, 1, 32);
    const auto seed_hitboxes = cfg.hitbox_parts | (cfg.lethal_only ? config::combat_profile::aim_part::head : 0);
    const auto penetrate = cfg.checks.walls == config::combat_profile::wall_policy::penetration;
    for (int bullet = 0; bullet < bullet_count; ++bullet)
    {
        const auto spread = ballistics.sample_predicted_spread(
            static_cast<int>(spread_seed), weapon.inaccuracy, weapon.spread, weapon.recoil_index, weapon.item_def_idx,
            weapon.fire_mode, bullet, bullet_count, weapon.pattern_seed);
        const auto direction = (forward + right * spread.x + up * spread.y).normalized();

        ballistics_t::penetration::result damage{};
        if (!ballistics.pen().run_seed(origin, direction, *target, target->bones, seed_hitboxes, penetrate, 1.0f,
                                       required_hitbox, damage))
            continue;
        if (damage.penetrated && damage.damage + 0.001f < cfg.min_damage)
            continue;
        const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(damage.hitbox);
        if (!cfg.lethal_only || hitgroup == game::rules::hitgroup::head ||
            damage.damage + 0.001f >= static_cast<float>(target->health))
        {
            if (traced_ray)
                *traced_ray = {origin, direction, spread_seed, bullet};
            return true;
        }
    }
    return false;
}

std::pair<bool, bool> aimbot_t::seed_plan_hits(ballistics_t& ballistics, const config::combat_profile::triggerbot& cfg,
                                               const std::vector<game::player_snapshot>& candidates,
                                               const seed_shooter& shooter, const std::uintptr_t target_pawn,
                                               const int required_hitbox, const seed_shot_plan& plan,
                                               simulation::shot_trace::ray_sample* const traced_ray)
{
    return simulation::seed_evaluation::required_pair(
        plan.phase != seed_prediction_phase::next, plan.phase != seed_prediction_phase::current,
        simulation::seed_evaluation::identical_ray(plan.current, plan.next),
        [&](const bool next)
        {
            return seed_sample_hits(ballistics, cfg, candidates, shooter, target_pawn, required_hitbox,
                                    next ? plan.next : plan.current, traced_ray);
        });
}

std::optional<aimbot_t::restricted_target>
aimbot_t::find_restricted_target(ballistics_t& ballistics, const config::combat_profile::triggerbot& cfg,
                                 const std::vector<game::player_snapshot>& players, const foundation::vec3& eye,
                                 const seed_shot_plan& plan, const int local_team, const bool free_for_all)
{
    foundation::vec3 ordinary_direction{};
    plan.current.direction_angles.to_directions(&ordinary_direction, nullptr, nullptr);
    std::optional<restricted_target> nearest_target;
    auto nearest = std::numeric_limits<float>::max();
    for (const auto& player : players)
    {
        const auto enemy = free_for_all || player.team != local_team;
        if (!enemy || player.invulnerable || player.health <= 0)
            continue;
        for (const auto& hitbox : player.hitboxes)
        {
            if (hitbox.index < 0 || hitbox.bone < 0)
                continue;
            const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hitbox.index);
            if ((cfg.hitbox_parts & simulation::aim_part_of(hitbox.index, hitgroup)) == 0)
                continue;
            const auto capsule = game::place_hitbox(player.bones.bones[hitbox.bone], hitbox);
            const auto start = capsule.start;
            const auto end = capsule.end;
            if (!ballistics.ray_hits_capsule(eye, ordinary_direction, start, end, std::max(0.5f, capsule.radius)))
                continue;
            const auto distance = ((start + end) * 0.5f - eye).dot(ordinary_direction);
            if (distance > 0.0f && distance < nearest)
            {
                nearest = distance;
                nearest_target = restricted_target{player.pawn, hitbox.index};
            }
        }
    }
    return nearest_target;
}

void aimbot_t::seed_tick(ballistics_t& seed_shared)
{
    refresh_runtime_config();

    struct stop_request_guard
    {
        bool preserve{};
        ~stop_request_guard()
        {
            if (!preserve)
                features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::seed_trigger);
        }
    } stop_guard{.preserve = m_trigger_held};

    using trace_reason = simulation::shot_trace::decision_reason;
    const bool tracing = simulation::shot_trace::enabled();
    simulation::shot_trace::decision_scope trace{trace_reason::snapshot, tracing};
    struct target_trace_guard
    {
        std::optional<simulation::shot_trace::target_record> initial, final;
        ~target_trace_guard()
        {
            if (initial)
                simulation::shot_trace::submit_target(*initial);
            if (final)
                simulation::shot_trace::submit_target(*final);
        }
    } target_trace;
    const auto now = std::chrono::steady_clock::now();
    if (m_trigger_held && now >= m_trigger_release_time)
    {
        if (tracing)
            simulation::shot_trace::expired(m_seed_last_tick);
        end_seed_press();
    }

    trace.reason = trace_reason::collision;
    if (!simulation::collision().valid())
    {
        reset_seed();
        return;
    }

    trace.reason = trace_reason::inactive;
    if (!runtime_config()->combat.seed_trigger_configured())
    {
        if (m_trigger_held || m_seed_last_controller || m_seed_last_pawn || !m_seed_targets.empty())
        {
            reset_seed();
        }
        return;
    }
    const auto configured_key = runtime_config()->combat.global.triggerbot_key;
    const auto configured_key_down = config::combat_profile::activation_active(
        runtime_config()->combat.global.triggerbot_activation_mode, configured_key);
    m_seed_reaction.observe(configured_key_down, now);
    if (!configured_key_down && !m_trigger_held)
    {
        m_seed_targets.clear();
        m_seed_player_buffer.clear();
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::seed_trigger);
        return;
    }

    trace.reason = trace_reason::snapshot;
    const auto controller =
        game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().local_player_controller);
    const auto host_status = is_host_seed_session(controller);
    if (!host_status)
        return;
    const auto host_session = *host_status;
    if (tracing)
    {
        trace.record.initial.controller = controller;
        trace.record.initial.host_mode = host_session;
    }
    const auto game_type = game::variables().get<std::int32_t>(CONVAR("game_type"_id));
    const auto game_mode = game::variables().get<std::int32_t>(CONVAR("game_mode"_id));
    const auto free_for_all = (game_type == 1 && game_mode == 2) || (game_type == 2 && game_mode == 0);
    const auto local_binding = game::resolve_local_pawn(controller);
    const auto pawn = local_binding.pawn;
    if (!controller || !pawn)
    {
        if (m_seed_last_controller || m_seed_last_pawn || m_trigger_held)
        {
            reset_seed();
        }
        return;
    }

    if (tracing)
        trace.record.initial.pawn = pawn;
    const auto local_health =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_id));
    const auto local_team =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_id));
    if (local_health <= 0 || local_health > 100 || (local_team != 2 && local_team != 3))
    {
        reset_seed();
        return;
    }

    foundation::vec3 eye_pos{};
    foundation::vec3 view_angles{};
    if (!game::camera().sample(eye_pos, view_angles))
    {
        return;
    }
    const auto seed_tick = read_seed_tick(controller, pawn, host_session);
    if (seed_tick <= 0)
    {
        return;
    }

    const auto identity_changed = (m_seed_last_controller && m_seed_last_controller != controller) ||
                                  (m_seed_last_pawn && m_seed_last_pawn != pawn);
    const auto tick_rewound = m_seed_last_tick >= 0 && seed_tick + 32 < m_seed_last_tick;
    const auto source_changed = m_seed_last_host_mode >= 0 && m_seed_last_host_mode != static_cast<int>(host_session);
    if (identity_changed || tick_rewound || source_changed)
    {
        reset_seed();
    }
    m_seed_last_controller = controller;
    m_seed_last_pawn = pawn;
    if (m_seed_last_tick != seed_tick)
    {
        m_seed_tick_observed_at = now;
    }
    m_seed_last_tick = seed_tick;
    m_seed_last_host_mode = static_cast<int>(host_session);
    sync_seed_phase(seed_tick, std::chrono::steady_clock::now());
    if (tracing)
    {
        auto& r = trace.record;
        r.initial.tick = seed_tick;
        r.initial.eye = eye_pos;
        r.initial.view = view_angles;
        r.phase_tick = m_seed_phase_tick;
        r.minimum_us = m_seed_network_window.minimum_us;
        r.maximum_us = m_seed_network_window.maximum_us;
        r.phase_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                           m_seed_phase_tick_at)
                         .count();
    }


    const auto shots_fired =
        game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id));
    observe_consumed_shot(pawn, seed_tick, shots_fired, now, tracing);
    update_network_receipt(controller, now);

    trace.reason = trace_reason::weapon;
    ballistics_t::context ctx{};
    if (!seed_shared.seed_weapon(pawn, controller, ctx) ||
        (!host_session && static_cast<long long>(ctx.seed_simulation_tick) + 1 != seed_tick))
    {
        m_seed_targets.clear();
        return;
    }
    const auto velocity = ctx.velocity;
    if (tracing)
    {
        const auto observed_simulation_tick = ctx.seed_simulation_tick;
        const auto observed_ping =
            game::bound_memory().process.load<int>(controller + SCHEMA("CCSPlayerController", "m_iPing"_id));
        const auto observed_view = ctx.seed_view_angles;
        trace_snapshot(trace.record.initial, ctx);
        trace.record.initial.velocity = velocity;
        trace.record.initial.simulation_tick = observed_simulation_tick;
        trace.record.initial.ping = observed_ping;
        trace.record.snapshot_end_qpc = simulation::shot_trace::qpc_now();
        simulation::shot_trace::weapon_snapshot(seed_tick, observed_simulation_tick, ctx.player_tick, observed_ping,
                                                ctx.inaccuracy, ctx.spread, ctx.recoil_index, observed_view, velocity,
                                                ctx.weapon);
    }
    const auto resolved = runtime_config()->combat.get(ctx.weapon_type);
    const auto cfg = resolved.triggerbot;
    if (tracing)
        trace.record.policy = {cfg.min_damage,
                               static_cast<float>(cfg.hitbox_parts),
                               static_cast<float>(cfg.seed_type),
                               static_cast<float>(cfg.reaction_time),
                               static_cast<float>(cfg.predictive),
                               static_cast<float>(cfg.checks.walls),
                               static_cast<float>(cfg.lethal_only),
                               static_cast<float>(cfg.key)};
    const auto is_enemy = [local_team, free_for_all](int team) { return free_for_all || team != local_team; };

    trace.reason = trace_reason::policy;
    if (!cfg.enabled || cfg.seed_type == config::combat_profile::seed_mode::none)
    {
        reset_seed();
        return;
    }
    if (local_checks_blocked(cfg.checks))
    {
        m_seed_targets.clear();
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::seed_trigger);
        return;
    }

    trace.reason = trace_reason::inactive;
    const bool hotkey_down = config::combat_profile::activation_active(cfg.activation_mode, cfg.key);
    m_seed_reaction.observe(hotkey_down, now);
    if (!hotkey_down)
    {
        m_seed_targets.clear();
        m_seed_player_buffer.clear();
        return;
    }
    trace.reason = trace_reason::reaction;
    if (!m_seed_reaction.ready(now, cfg.reaction_time))
        return;
    trace.reason = trace_reason::cooldown;
    if (!simulation::seed_timing::current_phase(m_seed_phase_tick, seed_tick, m_seed_phase_tick_at) ||
        (!host_session && std::chrono::steady_clock::now() - m_seed_phase_tick_at >
                              std::chrono::microseconds(m_seed_network_window.maximum_us)))
    {
        trace.reason = trace_reason::stale_delivery;
        return;
    }
    constexpr std::uint16_t revolver_id{64};
    if ((!ctx.weapon_ready && ctx.item_def_idx != revolver_id) || ctx.is_reloading)
    {
        m_seed_targets.clear();
        return;
    }


    trace.reason = m_trigger_held ? trace_reason::pending : trace_reason::plan_unavailable;
    const auto plan =
        build_seed_plan(pawn, ctx.seed_view_angles, host_session, seed_tick, now, !m_trigger_held, &ctx.seed_recoil);
    if (tracing)
    {
        trace.record.plan_qpc = simulation::shot_trace::qpc_now();
        if (plan)
            trace.record.initial_plan = trace_plan(*plan);
    }
    if (m_trigger_held || !plan)
    {
        return;
    }
    trace.reason = trace_reason::pending;
    if (m_seed_pending_target_tick >= 0 && seed_tick <= m_seed_pending_target_tick)
    {
        return;
    }

    trace.reason = trace_reason::no_targets;
    game::world().seed_players_into(m_seed_player_buffer, pawn, controller, local_team, free_for_all);
    const auto& players = m_seed_player_buffer;
    if (players.empty())
    {
        return;
    }

    for (const auto& player : players)
    {
        if (const auto found = m_seed_targets.find(player.pawn); found != m_seed_targets.end())
        {
            found->second.last_seen_at = now;
        }
    }
    constexpr auto entity_grace = std::chrono::milliseconds(250);
    std::erase_if(m_seed_targets, [&](const auto& entry) { return now - entry.second.last_seen_at > entity_grace; });

    simulation::shot_trace::ray_sample evaluated_ray{};
    trace.reason = trace_reason::restricted;
    std::optional<restricted_target> restricted;
    if (cfg.seed_type == config::combat_profile::seed_mode::restricted)
    {
        restricted = find_restricted_target(seed_shared, cfg, players, eye_pos, *plan, local_team, free_for_all);
        if (!restricted)
            return;
    }
    const auto restricted_pawn = restricted ? restricted->pawn : std::uintptr_t{};
    const auto restricted_hitbox = restricted ? restricted->hitbox : -1;
    auto* const traced_ray = tracing ? &evaluated_ray : nullptr;

    trace.reason = trace_reason::no_hit;
    std::uintptr_t firing_target{};
    std::uintptr_t firing_controller{};
    int firing_hitbox = -1;
    for (const auto& player : players)
    {
        if (!is_enemy(player.team) || player.invulnerable || player.health <= 0 ||
            (restricted_pawn && player.pawn != restricted_pawn))
        {
            continue;
        }
        const auto matches = seed_plan_hits(seed_shared, cfg, players, {ctx, eye_pos, velocity, seed_tick}, player.pawn,
                                            restricted_hitbox, *plan, traced_ray);
        if (!possible_seed_match(*plan, matches))
        {
            continue;
        }

        if (safe_seed_match(*plan, matches))
        {
            firing_target = player.pawn;
            if (tracing)
                target_trace.initial = trace_target(trace.record.id, player, 0);
            firing_controller = player.controller;
            firing_hitbox = restricted_hitbox;
            break;
        }
    }
    if (tracing)
    {
        trace.record.target_qpc = simulation::shot_trace::qpc_now();
        trace.record.initial.target = firing_target;
    }
    if (!firing_target)
    {
        return;
    }

    const auto planned_tick = plan->phase == seed_prediction_phase::current ? plan->current.tick : plan->next.tick;
    const auto ticks_ahead = std::max(planned_tick - seed_tick, 0);
    const auto planned_at =
        now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<float>(static_cast<float>(ticks_ahead) * game::rules::simulation_step));

    trace.reason = trace_reason::changed_state;
    if (tracing)
        trace.record.final_start_qpc = simulation::shot_trace::qpc_now();
    const auto final_now = std::chrono::steady_clock::now();
    const auto final_controller =
        game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().local_player_controller);
    if (final_controller != controller)
    {
        return;
    }
    const auto final_host_status = is_host_seed_session(final_controller);
    if (!final_host_status)
        return;
    const auto final_host_session = *final_host_status;
    const auto final_binding = game::resolve_local_pawn(final_controller);
    const auto final_pawn = final_binding.pawn;
    if (final_pawn != pawn || final_binding.handle != local_binding.handle)
    {
        return;
    }
    game::world().seed_players_into(m_seed_player_buffer, final_pawn, final_controller, local_team, free_for_all,
                                    firing_target, firing_controller);
    const auto& final_players = m_seed_player_buffer;
    const auto final_health =
        game::bound_memory().process.load<std::int32_t>(final_pawn + SCHEMA("C_BaseEntity", "m_iHealth"_id));
    const auto final_team =
        game::bound_memory().process.load<std::int32_t>(final_pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_id));
    if (final_health <= 0 || final_health > 100 || final_team != local_team)
    {
        return;
    }

    foundation::vec3 final_eye{};
    foundation::vec3 final_view{};
    if (!game::camera().sample(final_eye, final_view))
    {
        return;
    }
    const auto final_tick = read_seed_tick(final_controller, final_pawn, final_host_session);
    if (final_tick != seed_tick || final_host_session != host_session)
    {
        return;
    }

    ballistics_t::context final_ctx{};
    if (!seed_shared.seed_weapon(final_pawn, final_controller, final_ctx) ||
        (!final_ctx.weapon_ready && final_ctx.item_def_idx != revolver_id) || final_ctx.is_reloading ||
        final_ctx.weapon != ctx.weapon || final_ctx.player_tick != ctx.player_tick ||
        final_ctx.seed_simulation_tick != ctx.seed_simulation_tick ||
        (!final_host_session && static_cast<long long>(final_ctx.seed_simulation_tick) + 1 != final_tick))
    {
        return;
    }
    const auto final_velocity = final_ctx.velocity;
    if (tracing)
    {
        auto& out = trace.record.final;
        out.pawn = final_pawn;
        out.controller = final_controller;
        out.target = firing_target;
        out.tick = final_tick;
        out.host_mode = final_host_session;
        out.eye = final_eye;
        out.view = final_view;
        out.velocity = final_velocity;
        trace_snapshot(out, final_ctx);
        out.simulation_tick = final_ctx.seed_simulation_tick;
        trace.record.final_snapshot_qpc = simulation::shot_trace::qpc_now();
    }
    float previous_shot_time{};
    bool previous_shot_time_valid{};
    if (!final_host_session)
    {
        const auto last_shot_offset = SCHEMA("C_CSWeaponBase", "m_fLastShotTime"_id);
        previous_shot_time_valid = last_shot_offset > 0 &&
                                   game::bound_memory().process.copy(final_ctx.weapon + last_shot_offset,
                                                                     &previous_shot_time, sizeof(previous_shot_time)) &&
                                   std::isfinite(previous_shot_time) && previous_shot_time >= 0.0f;
    }
    auto final_plan = build_seed_plan(final_pawn, final_ctx.seed_view_angles, final_host_session, final_tick, final_now,
                                      false, &final_ctx.seed_recoil);
    if (tracing)
    {
        trace.record.final_plan_qpc = simulation::shot_trace::qpc_now();
        if (final_plan)
            trace.record.final_plan = trace_plan(*final_plan);
    }
    if (!final_plan)
    {
        return;
    }


    trace.reason = trace_reason::recheck;
    const auto final_shooter = seed_shooter{final_ctx, final_eye, final_velocity, final_tick};
    const auto final_matches = seed_plan_hits(seed_shared, cfg, final_players, final_shooter, firing_target,
                                              firing_hitbox, *final_plan, traced_ray);
    if (tracing)
    {
        trace.record.final_ray_qpc = simulation::shot_trace::qpc_now();
        const auto target = std::ranges::find(final_players, firing_target, &game::player_snapshot::pawn);
        if (target != final_players.end())
            target_trace.final = trace_target(trace.record.id, *target, 1);
    }
    if (!safe_seed_match(*final_plan, final_matches))
    {
        return;
    }
    const auto seed_max_speed =
        final_ctx.fire_mode ? final_ctx.debug.max_speed.second : final_ctx.debug.max_speed.first;
    const auto required_speed =
        std::max(0.5f, (std::isfinite(seed_max_speed) && seed_max_speed > 0.0f ? seed_max_speed : 250.0f) *
                           runtime_config()->general.m_auto_stop.required_shoot_speed * 0.01f);
    features::misc::auto_stop().request_stop(features::misc::auto_stop_source::seed_trigger, planned_at,
                                             required_speed);
    stop_guard.preserve = true;
    trace.reason = trace_reason::auto_stop;
    if (!features::misc::auto_stop().ready_to_fire(features::misc::auto_stop_source::seed_trigger))
    {
        return;
    }

    trace.reason = trace_reason::changed_state;
    if (tracing)
        trace.record.terminal_start_qpc = simulation::shot_trace::qpc_now();
    const auto terminal_guard_at = std::chrono::steady_clock::now();
    const auto terminal_tick = read_seed_tick(final_controller, final_pawn, final_host_session);
    foundation::vec3 terminal_angles{};
    if (!game::bound_memory().process.copy(final_pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id), &terminal_angles,
                                           sizeof(terminal_angles)))
        return;
    const auto terminal_punch =
        simulation::read_shot_punch(final_pawn, {final_plan->current.tick, final_plan->current.fraction});
    float terminal_recoil_index{};
    int terminal_clip{}, terminal_player_tick{};
    bool terminal_reloading{};
    auto& process = game::bound_memory().process;
    if (!process.copy(final_ctx.weapon + SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_id), &terminal_recoil_index,
                      sizeof(terminal_recoil_index)) ||
        !process.copy(final_ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id), &terminal_clip,
                      sizeof(terminal_clip)) ||
        !process.copy(final_ctx.weapon + SCHEMA("C_CSWeaponBase", "m_bInReload"_id), &terminal_reloading,
                      sizeof(terminal_reloading)) ||
        !process.copy(final_controller + SCHEMA("CBasePlayerController", "m_nTickBase"_id), &terminal_player_tick,
                      sizeof(terminal_player_tick)))
        return;
    const auto terminal_direction =
        terminal_punch ? terminal_angles + *terminal_punch : foundation::vec3{k_nan, k_nan, k_nan};
    const auto prepared_direction = final_plan->source_angles + final_plan->prepared_punch;
    const auto same_hash_bucket =
        std::isfinite(terminal_direction.x) && std::isfinite(terminal_direction.y) &&
        seed_quantize_angle(terminal_direction.x) == seed_quantize_angle(prepared_direction.x) &&
        seed_quantize_angle(terminal_direction.y) == seed_quantize_angle(prepared_direction.y);
    const auto same_punch = terminal_punch && std::abs(terminal_punch->x - final_plan->prepared_punch.x) <= 0.02f &&
                            std::abs(terminal_punch->y - final_plan->prepared_punch.y) <= 0.02f;
    if (tracing)
    {
        auto& r = trace.record;
        r.terminal_end_qpc = simulation::shot_trace::qpc_now();
        r.terminal_tick = terminal_tick;
        r.terminal_angles = terminal_angles;
        r.terminal_punch = terminal_punch.value_or(foundation::vec3{k_nan, k_nan, k_nan});
        r.terminal_recoil = terminal_recoil_index;
        r.terminal_clip = terminal_clip;
        r.terminal_reloading = terminal_reloading;
        r.same_bucket = same_hash_bucket;
        r.same_punch = same_punch;
    }
    if (terminal_tick != final_tick || !same_hash_bucket || !same_punch ||
        terminal_player_tick != final_ctx.player_tick || !std::isfinite(terminal_recoil_index) ||
        std::abs(terminal_recoil_index - final_ctx.recoil_index) > 0.001f || terminal_clip <= 0 ||
        terminal_clip != final_ctx.clip || terminal_reloading)
    {
#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
        research::seed_file_write(std::format("GUARD research::unix_ms={} tick={}->{} hash_ok={} punch_ok={} "
                                              "recoil={:.4f}->{:.4f} clip={} reload={}",
                                              research::unix_ms(), final_tick, terminal_tick, same_hash_bucket,
                                              same_punch, final_ctx.recoil_index, terminal_recoil_index, terminal_clip,
                                              terminal_reloading));
#endif
        return;
    }

    // A stable seed bucket does not imply an unchanged continuous ray direction.
    const auto angle_delta = terminal_angles - final_plan->source_angles;
    if (std::abs(angle_delta.x) > 0.0001f || std::abs(angle_delta.y) > 0.0001f)
    {
        const auto refreshed_plan = build_seed_plan(final_pawn, terminal_angles, final_host_session, final_tick,
                                                    terminal_guard_at, false, &final_ctx.seed_recoil);
        if (!refreshed_plan)
            return;
        const auto refreshed_matches = seed_plan_hits(seed_shared, cfg, final_players, final_shooter, firing_target,
                                                      firing_hitbox, *refreshed_plan, traced_ray);
        if (!safe_seed_match(*refreshed_plan, refreshed_matches))
            return;
        final_plan = refreshed_plan;
        if (tracing)
            trace.record.final_plan = trace_plan(*final_plan);
    }

    trace.reason = trace_reason::stale_delivery;
    const auto delivery_tick = read_seed_tick(final_controller, final_pawn, final_host_session);
    const auto delivery_at = std::chrono::steady_clock::now();
    const auto micros = [](auto duration) { return std::chrono::duration_cast<std::chrono::microseconds>(duration); };
    if (tracing)
    {
        auto& r = trace.record;
        r.delivery_qpc = simulation::shot_trace::qpc_now();
        r.delivery_tick = delivery_tick;
        r.final_age_us = micros(delivery_at - final_now).count();
        r.terminal_age_us = micros(delivery_at - terminal_guard_at).count();
        r.phase_us = micros(delivery_at - m_seed_phase_tick_at).count();
    }
    if (!(final_host_session ? simulation::seed_timing::fresh_decision(
                                   micros(delivery_at - final_now), micros(delivery_at - terminal_guard_at),
                                   micros(final_now - m_seed_phase_tick_at), micros(delivery_at - m_seed_phase_tick_at))
                             : simulation::seed_timing::fresh_network_decision(
                                   micros(delivery_at - final_now), micros(delivery_at - terminal_guard_at),
                                   micros(final_now - m_seed_phase_tick_at), micros(delivery_at - m_seed_phase_tick_at),
                                   m_seed_network_window)) ||
        delivery_tick != final_tick ||
        !simulation::seed_timing::current_phase(m_seed_phase_tick, final_tick, m_seed_phase_tick_at) ||
        !render::combat_input_ready() || render::menu_open())
    {
        m_seed_memo_sequence = -1;
        stop_guard.preserve = false;
        return;
    }

    m_seed_held_secondary = false;
    m_seed_held_proxy = cfg.key == VK_LBUTTON && final_ctx.item_def_idx != revolver_id;
    trace.reason = trace_reason::input_failed;
    if (tracing)
    {
        trace.record.input_proxy = m_seed_held_proxy;
        trace.record.pre_cock = m_revolver_pre_cock_down;
        trace.record.input_start_qpc = simulation::shot_trace::qpc_now();
    }
    const auto submission_started_at = std::chrono::steady_clock::now();
    const bool transferred_press = !m_seed_held_proxy && m_revolver_pre_cock_down;
    auto& api_budget = m_seed_held_proxy ? m_seed_key_budget : m_seed_pointer_budget;
    const auto api_reserve = transferred_press ? std::chrono::microseconds::zero() : api_budget.reserve();
    const auto start_phase = micros(submission_started_at - m_seed_phase_tick_at);
    const auto projected_phase = start_phase + api_reserve;
    const bool has_submission_budget =
        simulation::seed_timing::within_delivery_deadline(submission_started_at - m_seed_phase_tick_at + api_reserve) &&
        (final_host_session
             ? simulation::seed_timing::fresh_decision(micros(submission_started_at - final_now),
                                                       micros(submission_started_at - terminal_guard_at),
                                                       micros(final_now - m_seed_phase_tick_at), projected_phase)
             : simulation::seed_timing::fresh_network_decision(
                   micros(submission_started_at - final_now), micros(submission_started_at - terminal_guard_at),
                   micros(final_now - m_seed_phase_tick_at), start_phase, m_seed_network_window) &&
                   m_seed_network_window.contains(projected_phase));
    if (!has_submission_budget)
    {
        trace.reason = trace_reason::stale_delivery;
        m_seed_held_proxy = false;
        stop_guard.preserve = false;
        return;
    }
    bool press_delivered{};
    if (m_seed_held_proxy)
    {
        press_delivered = platform::windows::bound_input().key(VK_F24, true);
    }
    else
    {
        // If pre-cock already owns LMB this is a transfer of ownership, not a
        // second click. Otherwise start the accurate primary charge now.
        if (m_revolver_pre_cock_down)
        {
            press_delivered = true;
        }
        else
        {
            press_delivered =
                platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_down);
        }
        m_revolver_pre_cock_down = false;
    }
    const auto submission_completed_at = std::chrono::steady_clock::now();
    if (!transferred_press)
        api_budget.observe(submission_completed_at - submission_started_at);
    if (tracing)
    {
        trace.record.input_end_qpc = simulation::shot_trace::qpc_now();
        trace.record.input_ok = press_delivered;
        trace.record.input_backend = !trace.record.input_proxy && trace.record.pre_cock
                                         ? 0
                                         : static_cast<int>(platform::windows::input_gateway::last_injection_backend());
    }
    if (!press_delivered)
    {
        // Do not create synthetic ownership for an input event Windows did not
        // accept.  The next poll can safely retry the same still-valid seed.
        m_seed_held_proxy = false;
        return;
    }

    const auto submitted_at = submission_started_at + (submission_completed_at - submission_started_at) / 2;
    trace.reason = trace_reason::submitted;
    if (tracing)
    {
        const auto& candidate =
            final_plan->phase == seed_prediction_phase::current ? final_plan->current : final_plan->next;
        simulation::shot_trace::input(pawn, final_ctx.weapon, candidate.tick, final_plan->next.tick,
                                      candidate.hash_angles, final_plan->next.hash_angles, final_ctx.inaccuracy,
                                      final_ctx.spread, final_ctx.recoil_index, firing_target, candidate.fraction,
                                      final_plan->source_angles, candidate.punch, final_velocity, final_ctx.player_tick,
                                      final_ctx.clip, final_ctx.item_def_idx, -1, evaluated_ray);
    }
    features::misc::auto_stop().notify_shot(features::misc::auto_stop_source::seed_trigger);
    features::visuals::event_log().begin_trigger_shot("Seed Trigger", true);
    m_trigger_held = true;
    m_seed_press_active = true;
    m_revolver_committed = final_ctx.item_def_idx == revolver_id;
    m_trigger_release_time = submission_completed_at + std::chrono::milliseconds(m_revolver_committed ? 1200 : 40);
    m_seed_pending_target_tick =
        final_plan->phase == seed_prediction_phase::current ? final_plan->current.tick : final_plan->next.tick;
    m_seed_pending_time = submitted_at;
    m_seed_phase_receipt = {};
    if (previous_shot_time_valid)
    {
        // This marker predates input; a post-input read can already contain this shot.
        m_seed_phase_receipt = {
            final_ctx.weapon,      final_tick,   static_cast<int>(micros(submitted_at - m_seed_phase_tick_at).count()),
            previous_shot_time,    submitted_at, false,
            final_ctx.player_tick, true};
    }


#if defined(VESTA_SEED_LOG) && VESTA_SEED_LOG
    foundation::vec3 press_command_angles{};
    research::g_press_sequence = -1;
    (void)m_seed_commands.read_angles(press_command_angles, &research::g_press_sequence);
    research::g_press_tick = final_tick;
    research::g_press_at = std::chrono::steady_clock::now();
    VESTA_SEED_COUNT(presses);
    research::seed_file_write(std::format(
        "PRESS research::unix_ms={} seq={} tick={} target_tick={} phase={} "
        "hash=({:.4f},{:.4f}) punch=({:.4f},{:.4f}) "
        "recoil_index={:.4f} inaccuracy={:.6f} spread={:.6f} "
        "pipeline_ms={:.3f} terminal_us={}",
        research::unix_ms(), research::g_press_sequence, final_tick, m_seed_pending_target_tick,
        static_cast<int>(final_plan->phase), final_plan->current.hash_angles.x, final_plan->current.hash_angles.y,
        final_plan->prepared_punch.x, final_plan->prepared_punch.y, final_ctx.recoil_index, final_ctx.inaccuracy,
        final_ctx.spread, std::chrono::duration<float, std::milli>(final_now - now).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - terminal_guard_at)
            .count()));
#endif
}


} // namespace features::aimbot
