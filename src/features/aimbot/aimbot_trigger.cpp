#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
aimbot_t::trigger_result aimbot_t::trace_direction(const foundation::vec3& eye_pos, const foundation::vec3& direction,
                                                   const std::vector<game::player_snapshot>& players,
                                                   const config::combat_profile::triggerbot& cfg) const
{
    constexpr auto max_range{8192.0f};
    // Trigger prediction follows the sampled target state only. Network ping is not
    // an aim-ahead horizon; server lag compensation consumes the historical sample.
    const auto prediction_time =
        cfg.predictive ? std::clamp(static_cast<float>(cfg.delay) * 0.001f + game::rules::simulation_step, 0.0f, 0.12f)
                       : 0.0f;
    auto local_velocity = foundation::vec3{};
    if (cfg.predictive)
    {
        const auto pawn = game::local_player().pawn();
        if (pawn)
        {
            local_velocity = game::bound_memory().process.load<foundation::vec3>(
                pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
            if (!std::isfinite(local_velocity.x) || !std::isfinite(local_velocity.y) ||
                !std::isfinite(local_velocity.z))
            {
                local_velocity = {};
            }
        }
    }


    struct direct_candidate
    {
        const game::player_snapshot* player{};
        game::skeleton_reader::data bones{};
        float distance{max_range};
        int hitbox{-1};
        int hitgroup{-1};
    } best{};

    for (const auto& player : players)
    {
        if (!game::local_player().is_enemy(player.team))
        {
            continue;
        }

        if (player.invulnerable || player.hitboxes.count <= 0)
        {
            continue;
        }

        const auto simulation_tick_offset = SCHEMA("C_BaseEntity", "m_nSimulationTick"_id);
        const auto tick_before = game::bound_memory().process.load<std::int32_t>(player.pawn + simulation_tick_offset);
        auto bones = game::skeletons().get(player.bone_cache);
        const auto tick_after = game::bound_memory().process.load<std::int32_t>(player.pawn + simulation_tick_offset);
        if (tick_before != tick_after || !bones.is_valid())
            bones = player.bones;
        if (!bones.is_valid())
        {
            continue;
        }

        if (cfg.predictive)
        {
            auto target_velocity = game::bound_memory().process.load<foundation::vec3>(
                player.pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
            if (!std::isfinite(target_velocity.x) || !std::isfinite(target_velocity.y) ||
                !std::isfinite(target_velocity.z))
            {
                target_velocity = player.velocity;
            }
            const auto offset = (target_velocity - local_velocity) * prediction_time;
            for (auto& bone : bones.bones)
            {
                bone.position += offset;
            }
        }

        for (const auto& hitbox : player.hitboxes)
        {
            const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hitbox.index);
            if ((cfg.hitbox_parts & simulation::aim_part_of(hitbox, hitgroup)) == 0)
                continue;
            if (hitbox.index < 0 || hitbox.bone < 0 || hitbox.bone >= 128)
            {
                continue;
            }
            const auto& bone = bones.bones[hitbox.bone];
            const auto rotation_length = bone.rotation.x * bone.rotation.x + bone.rotation.y * bone.rotation.y +
                                         bone.rotation.z * bone.rotation.z + bone.rotation.w * bone.rotation.w;
            if (!std::isfinite(bone.position.x) || !std::isfinite(bone.position.y) || !std::isfinite(bone.position.z) ||
                !std::isfinite(rotation_length) || rotation_length < 0.5f || rotation_length > 1.5f ||
                bone.position.distance_sqr(player.origin) > 512.0f * 512.0f)
            {
                continue;
            }
            const auto start = bone.position + bone.rotation.apply(hitbox.mins);
            const auto end = bone.position + bone.rotation.apply(hitbox.maxs);
            float distance{};
            if (foundation::ray_capsule_entry(eye_pos, direction, start, end, hitbox.radius, distance) &&
                distance < best.distance)
            {
                best = {&player, bones, distance, hitbox.index, hitgroup};
            }
        }
    }

    if (!best.player || best.hitbox < 0 || best.distance >= max_range)
        return {};

    trigger_result result{};
    result.player = best.player;
    result.bones = best.bones;
    result.hitbox = best.hitbox;
    result.hitgroup = best.hitgroup;
    result.point = eye_pos + direction * best.distance;
    if (simulation::collision().valid())
    {
        ballistics_t::penetration::result passage{};
        if (!simulation::ballistics().pen().run_seed(
                eye_pos, direction, *best.player, best.bones, cfg.hitbox_parts,
                cfg.checks.walls == config::combat_profile::wall_policy::penetration, 1.0f, best.hitbox, passage))
            return {};
        result.damage = passage.damage;
        result.penetrated = passage.penetrated;
    }
    else
    {
        const auto& weapon = simulation::ballistics().pen().get_weapon_data();
        if (weapon.range <= best.distance || weapon.damage <= 0.0f)
            return {};
        result.damage = simulation::ballistics().pen().get_max_damage(best.hitgroup, best.player->armor,
                                                                      best.player->has_helmet, best.player->team) *
                        std::pow(std::clamp(weapon.range_modifier, 0.0f, 1.0f), best.distance / 500.0f);
        result.penetrated = false;
    }

    if (cfg.checks.smoke && line_through_smoke(eye_pos, result.point))
        return {};
    return result;
}

void aimbot_t::triggerbot(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                          const std::vector<game::player_snapshot>& players,
                          const config::combat_profile::triggerbot& cfg)
{
    VESTA_PERF_SCOPE(aim_trigger);
    auto now = std::chrono::steady_clock::now();
    const auto cancel_scheduled = [this]
    {
        m_trigger_shot_scheduled = false;
        m_trigger_fire_time = {};
        m_trigger_target_valid = false;
        features::misc::auto_stop().cancel_request(features::misc::auto_stop_source::advanced_trigger);
    };

    if (!g_walk_autofire.load(std::memory_order_acquire) &&
        !config::combat_profile::activation_active(cfg.activation_mode, cfg.key))
    {
        cancel_scheduled();
        return;
    }

    const auto& ctx = ballistics().ctx();
    // Journal why a trigger that should fire does not (at most 4 times a second): code = reason << 24 | a << 8 | b.
    // 10: the game reports something under the crosshair but our own trace hits no enemy hitbox;
    // 11/12: hit but blocked by minimum damage / lethal only; 20: waiting for the planned time (a = ms left);
    // 22: Windows did not accept the press (a = error); 23: a previous press is still held (a = ms overdue);
    // 24: hit chance too low (a = percent reached, b = percent wanted); 25: the target left the crosshair.
    const auto note = [&](const std::uint32_t reason, const float a, const float b)
    {
        if (!m_trigger_note.allow(now))
            return;
        diagnostics::emit(diagnostics::event::trigger_waiting, diagnostics::severity::information,
                          diagnostics::code_domain::mode,
                          (reason << 24) | (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 65535.0f)) << 8) |
                              static_cast<std::uint32_t>(std::clamp(b, 0.0f, 255.0f)));
    };

    if (m_trigger_held)
    {
        VESTA_SEED_COUNT(held_skip);
        note(23, std::chrono::duration<float, std::milli>(now - m_trigger_release_time).count(), 0.0f);
        return;
    }
    if (now < m_trigger_cooldown_until)
    {
        cancel_scheduled();
        return;
    }
    constexpr std::uint16_t revolver_id{64};
    const auto revolver = ctx.item_def_idx == revolver_id;
    if ((!ctx.weapon_ready && !revolver) || ctx.is_reloading || ctx.clip == 0)
    {
        cancel_scheduled();
        return;
    }

    const auto pawn = game::local_player().pawn();
    if (!m_trigger_shot_scheduled)
    {
        if (local_checks_blocked(cfg.checks))
        {
            features::visuals::event_log().push_throttled(0x54524301u, "Trigger blocked: legit check",
                                                          features::visuals::event_kind::blocked,
                                                          std::chrono::milliseconds(750));
            return;
        }

        // CViewRender already contains the final rendered camera orientation. Adding
        // AimPunch here a second time makes the miss grow with target distance.
        const auto trace_angles = view_angles;
        foundation::vec3 forward{};
        trace_angles.to_directions(&forward, nullptr, nullptr);
        const auto result = trace_direction(eye_pos, forward, players, cfg);
        if (!result.player)
        {
            const auto id = game::local_player().crosshair_id();
            if (id > 0 && id < 0x7fff)
                note(10, static_cast<float>(id), 0.0f);
            return;
        }
        if (result.penetrated && result.damage < cfg.min_damage)
        {
            note(11, result.damage, cfg.min_damage);
            return;
        }

        if (cfg.lethal_only && result.hitgroup != game::rules::hitgroup::head &&
            result.damage + 0.001f < static_cast<float>(result.player->health))
        {
            note(12, result.damage, static_cast<float>(result.player->health));
            return;
        }

        now = std::chrono::steady_clock::now();
        m_trigger_shot_scheduled = true;
        const auto jitter = cfg.randomize_ms > 0
                                ? static_cast<int>(std::round(m_rng.uniform(-static_cast<float>(cfg.randomize_ms),
                                                                            static_cast<float>(cfg.randomize_ms))))
                                : 0;
        const auto outlier = m_rng.uniform(0.0f, 100.0f) < std::clamp(cfg.outlier_chance, 0.0f, 100.0f)
                                 ? std::max(cfg.outlier_delay_ms, 0)
                                 : 0;
        const auto committed_delay = std::max(0, cfg.delay + jitter + outlier);
        m_trigger_fire_time = now + std::chrono::milliseconds(committed_delay);
        m_trigger_target = *result.player;
        m_trigger_target_bones = result.bones;
        m_trigger_target_eye = eye_pos;
        m_trigger_target_angles = trace_angles;
        m_trigger_target_parts = cfg.hitbox_parts;
        m_trigger_target_valid = true;
        const auto weapon_max_speed = ctx.fire_mode ? ctx.debug.max_speed.second : ctx.debug.max_speed.first;
        const auto required_speed =
            std::max(0.5f, (std::isfinite(weapon_max_speed) && weapon_max_speed > 0.0f ? weapon_max_speed : 250.0f) *
                               runtime_config()->general.m_auto_stop.required_shoot_speed * 0.01f);
        features::misc::auto_stop().request_stop(features::misc::auto_stop_source::advanced_trigger,
                                                 m_trigger_fire_time, required_speed);
    }

    if (now < m_trigger_fire_time)
    {
        note(20, std::chrono::duration<float, std::milli>(m_trigger_fire_time - now).count(), 0.0f);
        return;
    }

    if (!features::misc::auto_stop().ready_to_fire(features::misc::auto_stop_source::advanced_trigger))
        return;

    const auto hit_probability =
        cfg.hitchance > 0.0f && m_trigger_target_valid
            ? ballistics().estimate_hit_probability(m_trigger_target_eye, m_trigger_target_angles, m_trigger_target,
                                                    m_trigger_target_bones, m_trigger_target_parts)
            : 1.0f;
    if (hit_probability < cfg.hitchance / 100.0f)
    {
        note(24, hit_probability * 100.0f, cfg.hitchance);
        // The estimate must follow the aim, not the moment the shot was planned: the planning snapshot can
        // sit on the edge of a hitbox and never reach the wanted chance however well the crosshair is placed
        // now. Take the current aim and target again; if the crosshair left the enemy, start over.
        if (!players.empty())
        {
            foundation::vec3 forward{};
            view_angles.to_directions(&forward, nullptr, nullptr);
            const auto fresh = trace_direction(eye_pos, forward, players, cfg);
            if (!fresh.player)
            {
                note(25, 0.0f, 0.0f);
                cancel_scheduled();
                return;
            }
            m_trigger_target = *fresh.player;
            m_trigger_target_bones = fresh.bones;
            m_trigger_target_eye = eye_pos;
            m_trigger_target_angles = view_angles;
        }
        features::visuals::event_log().push_throttled(0x54524302u, "Trigger waiting: hitchance",
                                                      features::visuals::event_kind::blocked,
                                                      std::chrono::milliseconds(750));
        return;
    }

    // Once committed, do not consult the target again. A failed syscall is not a
    // shot: retain the schedule and retry on the next 250 Hz pass.
    const auto shots_before =
        pawn ? game::bound_memory().process.load<std::int32_t>(pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id))
             : -1;
    const auto press_delivered = m_revolver_pre_cock_down || platform::windows::bound_input().pointer(
                                                                 0, 0, platform::windows::pointer_action::primary_down);
    if (!press_delivered)
    {
        note(22, static_cast<float>(::GetLastError()), 0.0f);
        return;
    }
    m_revolver_pre_cock_down = false;

    features::misc::auto_stop().notify_shot(features::misc::auto_stop_source::advanced_trigger);
    features::visuals::event_log().begin_trigger_shot(revolver ? "R8 Trigger" : "Trigger", false);
    m_trigger_held = true;
    m_seed_press_active = false;
    m_revolver_committed = revolver;
    m_trigger_press_shots = shots_before;

    const auto hold_ms =
        std::clamp(static_cast<int>(std::ceil(2.0f * game::rules::simulation_step * 1000.0f)) + 4, 32, 48);
    m_trigger_release_time = now + std::chrono::milliseconds(revolver ? 1200 : hold_ms);
    m_trigger_cooldown_until = now + std::chrono::milliseconds(std::max(cfg.delay_after_ms, 0));

    m_trigger_shot_scheduled = false;
    m_trigger_fire_time = {};
    m_trigger_target_valid = false;
}


} // namespace features::aimbot
