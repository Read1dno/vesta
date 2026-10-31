#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
aimbot_t::target aimbot_t::choose_target(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                         const std::vector<game::player_snapshot>& players,
                                         const config::combat_profile::resolved_config& config, bool enforce_fov) const
{
    VESTA_PERF_SCOPE(aim_choose);
    target selected{};
    selected.fov = std::numeric_limits<float>::max();
    auto best_score = selected.fov;

    for (const auto& player : players)
    {
        const auto eligible = game::local_player().is_enemy(player.team) && !player.invulnerable &&
                              player.hitboxes.count > 0 && player.bones.is_valid();
        if (!eligible)
            continue;

        target candidate{};
        candidate.player = &player;
        candidate.bones = player.bones;
        candidate.aim_point =
            get_aim_point(eye_pos, view_angles, player, candidate.bones, config, enforce_fov, candidate.damage,
                          candidate.hitbox, candidate.bone, candidate.penetrated, candidate.aim_offset);
        if (candidate.hitbox < 0)
            continue;

        candidate.fov = foundation::angular_distance(view_angles, eye_pos, candidate.aim_point);
        const auto candidate_limit = selection_fov(config.aimbot, eye_pos, candidate.aim_point);
        const auto score = detail::selection_cost(candidate.fov, player.pawn == m_aim_pawn);
        if ((!enforce_fov || candidate.fov <= candidate_limit) && score <= best_score)
        {
            best_score = score;
            selected = std::move(candidate);
        }
    }
    return selected;
}

foundation::vec3 aimbot_t::get_aim_point(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                         const game::player_snapshot& player, const game::skeleton_reader::data& bones,
                                         const config::combat_profile::resolved_config& cfg, bool enforce_fov,
                                         float& out_damage, int& out_hitbox, int& out_bone, bool& out_penetrated,
                                         foundation::vec3& out_offset) const
{
    out_hitbox = -1;
    out_bone = -1;
    out_offset = {};
    const bool collision_ready = simulation::collision().valid();

    foundation::vec3 best_point{};
    auto best_score = std::numeric_limits<float>::max();
    foundation::vec3 selection_prediction{};
    if (cfg.aimbot.prediction.enabled)
    {
        const auto local_pawn = game::local_player().pawn();
        auto local_velocity = local_pawn ? game::bound_memory().process.load<foundation::vec3>(
                                               local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id))
                                         : foundation::vec3{};
        auto target_velocity = player.velocity;
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
            game::bound_memory().process.load<std::uint32_t>(player.pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id));
        const auto local_flags =
            local_pawn
                ? game::bound_memory().process.load<std::uint32_t>(local_pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id))
                : 0u;
        if ((target_flags & 1u) != 0u && (local_flags & 1u) != 0u)
        {
            target_velocity.z = 0.0f;
            local_velocity.z = 0.0f;
        }
        const auto combat = ballistics().ctx();
        const auto sample_age =
            player.simulation_time > 0.0f && std::isfinite(player.simulation_time) && std::isfinite(combat.current_time)
                ? std::clamp(combat.current_time - player.simulation_time, 0.0f, 0.12f)
                : 0.0f;
        const auto horizon = std::clamp(sample_age + 0.004f, 0.0f,
                                        std::clamp(cfg.aimbot.prediction.max_horizon_ms, 0.0f, 120.0f) * 0.001f);
        selection_prediction = (target_velocity - local_velocity) * horizon;
    }

    for (const auto& hb : player.hitboxes)
    {
        if (hb.index < 0 || hb.bone < 0 || hb.bone >= 128)
        {
            continue;
        }

        const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hb.index);
        const auto part = simulation::aim_part_of(hb, hitgroup);
        if (!(cfg.aimbot.hitbox_parts & part))
        {
            continue;
        }

        const auto& bone = bones.bones[hb.bone];
        const auto candidates = build_aim_points(bone, hb, cfg.aimbot.multipoint, part, cfg.aimbot.multipoint_config);

        for (int i = 0; i < candidates.count; ++i)
        {
            const auto base_pos = candidates.points[i];
            const auto pos = base_pos + selection_prediction;

            const auto fov = get_fov(view_angles, eye_pos, pos);
            if (!detail::should_trace_candidate(fov, selection_fov(cfg.aimbot, eye_pos, pos), enforce_fov))
                continue;
            const auto score = detail::selection_cost(fov, player.pawn == m_aim_pawn && hb.bone == m_aim_last_bone);
            if (score >= best_score)
            {
                continue;
            }

            auto damage{0.0f};
            auto penetrated{false};
            auto accepted{false};

            if (cfg.aimbot.checks.smoke && line_through_smoke(eye_pos, pos))
            {
                continue;
            }

            const auto trace = collision_ready ? simulation::collision().trace_ray(eye_pos, pos)
                                               : simulation::collision_world::trace_result{};

            if (!trace.hit)
            {
                damage = simulation::ballistics().pen().get_max_damage(hitgroup, player.armor, player.has_helmet,
                                                                       player.team);
                accepted = true;
            }
            else if (cfg.aimbot.checks.walls != config::combat_profile::wall_policy::block)
            {
                ballistics_t::penetration::result pen_result{};
                if (simulation::ballistics().pen().run(eye_pos, pos, player, bones, pen_result) &&
                    pen_result.damage >= cfg.aimbot.min_damage)
                {
                    damage = pen_result.damage;
                    penetrated = pen_result.penetrated;
                    accepted = true;
                }
            }

            if (!accepted)
            {
                continue;
            }

            constexpr auto head_hitgroup = 1;
            if (cfg.aimbot.lethal_only && hitgroup != head_hitgroup &&
                damage + 0.001f < static_cast<float>(player.health))
            {
                continue;
            }

            best_score = score;
            best_point = pos;
            out_damage = damage;
            out_hitbox = hb.index;
            out_bone = hb.bone;
            out_penetrated = penetrated;

            out_offset = bone.to_local(base_pos);
        }
    }

    return best_point;
}

float aimbot_t::get_fov(const foundation::vec3& view_angles, const foundation::vec3& eye_pos,
                        const foundation::vec3& target_pos) const
{
    return foundation::angular_distance(view_angles, eye_pos, target_pos);
}

float aimbot_t::screen_radius_for_fov(const foundation::vec3& eye_pos, const foundation::vec3& view_angles,
                                      float angle_degrees) const
{
    if (angle_degrees <= 0.0f)
        return 0.0f;
    const auto [width, height] = render::draw::get_display_size();
    if (angle_degrees >= 90.0f)
        return static_cast<float>(std::max(width, height)) * 2.0f;

    const auto projected_direction = [&](foundation::vec3 angles)
    {
        foundation::vec3 direction{};
        angles.to_directions(&direction, nullptr, nullptr);
        return game::camera().project(eye_pos + direction * 1000.0f);
    };

    const auto center = projected_direction(view_angles);
    auto boundary_angles = view_angles;
    boundary_angles.x -= angle_degrees;
    const auto boundary = projected_direction(boundary_angles);
    if (!game::camera().projection_valid(center) || !game::camera().projection_valid(boundary))
    {
        return 0.0f;
    }
    return std::hypot(boundary.x - center.x, boundary.y - center.y);
}

void aimbot_t::draw_penetration_crosshair(render::draw::draw_list& draw_list, const foundation::vec3& eye_pos,
                                          const foundation::vec3& view_angles,
                                          const config::combat_profile::resolved_config& cfg, float current_time)
{
    foundation::vec3 forward{};
    view_angles.to_directions(&forward, nullptr, nullptr);

    const auto first_hit =
        simulation::collision().trace_ray(eye_pos, eye_pos + forward * ballistics().pen().get_weapon_data().range);
    if (!first_hit.hit)
    {
        return;
    }

    auto pen_damage{0.0f};
    const auto can_pen = ballistics().pen().can(eye_pos, forward, pen_damage);

    if (m_pen_color_can_pen != can_pen)
    {
        const auto& from_color = can_pen ? cfg.other.penetration_color_no : cfg.other.penetration_color_yes;
        const auto& to_color = can_pen ? cfg.other.penetration_color_yes : cfg.other.penetration_color_no;

        m_pen_color_tween.start(0.0f, 1.0f, 0.15f, foundation::curve::smooth);
        m_pen_color_can_pen = can_pen;
    }

    m_pen_color_tween.update(render::draw::get_delta_time());

    const auto t = m_pen_color_tween.value();
    const auto& color_a = m_pen_color_can_pen ? cfg.other.penetration_color_no : cfg.other.penetration_color_yes;
    const auto& color_b = m_pen_color_can_pen ? cfg.other.penetration_color_yes : cfg.other.penetration_color_no;

    const auto interp_r = static_cast<std::uint8_t>(color_a.r + (color_b.r - color_a.r) * t);
    const auto interp_g = static_cast<std::uint8_t>(color_a.g + (color_b.g - color_a.g) * t);
    const auto interp_b = static_cast<std::uint8_t>(color_a.b + (color_b.b - color_a.b) * t);
    const auto interp_a = static_cast<std::uint8_t>(color_a.a + (color_b.a - color_a.a) * t);

    const auto color = render::draw::rgba(interp_r, interp_g, interp_b, interp_a);

    const auto [w, h] = render::draw::get_display_size();
    const auto cx = w * 0.5f;
    const auto cy = h * 0.5f;
    constexpr auto size{6.0f};

    draw_list.add_line(cx - size, cy, cx + size, cy, color, 2.0f);
    draw_list.add_line(cx, cy - size, cx, cy + size, color, 2.0f);
}

void aimbot_t::draw_fov_ring(render::draw::draw_list& draw_list, const foundation::vec3& eye_pos,
                             const foundation::vec3& view_angles, const config::combat_profile::aimbot& config)
{
    const auto visibility = m_fov_alpha.value();
    foundation::vec3 point{};
    std::chrono::steady_clock::time_point updated{};
    float selected_fov{};
    std::uintptr_t selected_pawn{};
    std::uintptr_t selected_bone_cache{};
    int selected_bone{-1};
    foundation::vec3 selected_offset{};
    {
        std::scoped_lock lock(m_indicator_mutex);
        point = m_indicator_point;
        updated = m_indicator_time;
        selected_fov = m_indicator_fov;
        selected_pawn = m_indicator_pawn;
        selected_bone_cache = m_indicator_bone_cache;
        selected_bone = m_indicator_bone;
        selected_offset = m_indicator_offset;
    }
    if (selected_pawn && selected_bone >= 0)
    {
        bool refreshed{};
        if (const auto pose = game::render_poses().latest())
        {
            for (const auto& player : pose->players)
            {
                if (player.pawn != selected_pawn || player.bone_cache != selected_bone_cache)
                    continue;
                if (selected_bone < 128 && player.bones.is_valid())
                {
                    const auto& bone = player.bones.bones[selected_bone];
                    point = bone.to_world(selected_offset);
                    refreshed = true;
                }
                break;
            }
        }
        if (!refreshed && selected_bone_cache)
        {
            const auto bones = game::skeletons().get(selected_bone_cache);
            if (selected_bone < 128 && bones.is_valid())
            {
                const auto& bone = bones.bones[selected_bone];
                point = bone.to_world(selected_offset);
            }
        }
    }
    const auto target_recent = updated.time_since_epoch().count() != 0 &&
                               std::chrono::steady_clock::now() - updated <= std::chrono::milliseconds(100);

    const auto mode = config.fov_config.selection;
    if (config.draw_fov && mode != config::combat_profile::fov_settings::target_distance)
    {
        auto degrees = static_cast<float>(config.fov);
        if (mode == config::combat_profile::fov_settings::distance)
        {
            if (target_recent && selected_fov > 0.0f)
                degrees = selected_fov;
            else
            {
                foundation::vec3 forward{};
                view_angles.to_directions(&forward, nullptr, nullptr);
                const auto max_distance = std::max(config.fov_config.far_distance_m * 52.4934f, 1.0f);
                const auto surface = simulation::collision().trace_ray(eye_pos, eye_pos + forward * max_distance);
                degrees = distance_fov(config, surface.hit ? surface.distance : max_distance);
            }
        }
        const auto radius = screen_radius_for_fov(eye_pos, view_angles, degrees);
        if (radius > 0.5f)
        {
            const auto [width, height] = render::draw::get_display_size();
            const auto color = render::draw::rgba{config.fov_color.r, config.fov_color.g, config.fov_color.b,
                                                  static_cast<std::uint8_t>(visibility * 125.0f)};
            draw_list.add_circle(width * 0.5f, height * 0.5f, radius, color, 64);
        }
    }

    if (config.draw_fov && mode == config::combat_profile::fov_settings::target_distance && target_recent)
    {
        const auto screen = game::camera().project(point);
        if (!game::camera().projection_valid(screen))
            return;
        const auto color = render::draw::rgba{config.fov_color.r, config.fov_color.g, config.fov_color.b,
                                              static_cast<std::uint8_t>(visibility * 150.0f)};
        const auto target_angles = foundation::look_at_angles(eye_pos, point);
        const auto radius = screen_radius_for_fov(
            eye_pos, target_angles,
            selected_fov > 0.0f ? selected_fov : distance_fov(config, (point - eye_pos).length()));
        if (radius > 0.5f)
            draw_list.add_circle(screen.x, screen.y, radius, color, 64);
    }
}


} // namespace features::aimbot
