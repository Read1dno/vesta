#include <stdafx.hpp>
#include <features/aimbot/aimbot_internal.hpp>

namespace features::aimbot
{
std::optional<bool> is_host_seed_session(std::uintptr_t controller)
{
    std::int32_t ping{};
    if (!controller || !game::bound_memory().process.copy(controller + SCHEMA("CCSPlayerController", "m_iPing"_id),
                                                          &ping, sizeof(ping)))
        return std::nullopt;
    return ping == 0;
}

int read_seed_tick(std::uintptr_t controller, std::uintptr_t pawn, bool host_session)
{
    if (!controller || !pawn)
        return -1;
    auto& process = game::bound_memory().process;
    int host_tick{-1}, simulation_tick{-1}, tick_base{-1};
    if (host_session)
    {
        std::uintptr_t globals{};
        if (!process.copy(game::bound_addresses().global_vars, &globals, sizeof(globals)) || !globals ||
            !process.copy(globals + 0x44, &host_tick, sizeof(host_tick)))
            return -1;
    }
    else if (!process.copy(pawn + SCHEMA("C_BaseEntity", "m_nSimulationTick"_id), &simulation_tick,
                           sizeof(simulation_tick)))
        return -1;
    if (host_tick <= 0 && simulation_tick < 0 &&
        !process.copy(controller + SCHEMA("CBasePlayerController", "m_nTickBase"_id), &tick_base, sizeof(tick_base)))
        return -1;
    return simulation::seed_timing::select_tick(host_session, host_tick, simulation_tick, tick_base);
}

int read_seed_tick()
{
    const auto controller = game::local_player().controller();
    const auto pawn = game::local_player().pawn();
    const auto host_session = is_host_seed_session(controller);
    return host_session ? read_seed_tick(controller, pawn, *host_session) : -1;
}

point_buffer build_aim_points(const game::skeleton_reader::data::bone& bone, const game::hitbox_catalog::entry& hb,
                              bool multipoint, int part, const config::combat_profile::multipoint_settings& settings)
{
    point_buffer out{};

    const auto capsule = game::place_hitbox(bone, hb);
    const auto start = capsule.start;
    const auto end = capsule.end;
    const auto center = capsule.center();

    out.points[out.count++] = center;
    if (!multipoint)
    {
        return out;
    }

    if (settings.caps)
    {
        out.points[out.count++] = start;
        out.points[out.count++] = end;
    }

    // Lateral spread perpendicular to the capsule axis, so points cover the sides
    // of the hull (a shoulder/temple edge), not just its length.
    auto axis = end - start;
    if (settings.sides && axis.length_sqr() > 0.0001f)
    {
        axis.normalize();
        auto right = axis.cross(foundation::vec3{0.0f, 0.0f, 1.0f});
        if (right.length_sqr() < 0.0001f)
        {
            right = axis.cross(foundation::vec3{1.0f, 0.0f, 0.0f});
        }
        right.normalize();
        const auto up = right.cross(axis).normalized();

        const auto scale =
            part == config::combat_profile::aim_part::head
                ? settings.head_scale
                : (part == config::combat_profile::aim_part::body ? settings.body_scale : settings.limb_scale);
        const auto reach = capsule.radius * std::clamp(scale, 0.05f, 0.95f);

        out.points[out.count++] = center + right * reach;
        out.points[out.count++] = center - right * reach;
        out.points[out.count++] = center + up * reach;
        out.points[out.count++] = center - up * reach;
    }

    return out;
}

bool line_through_smoke(const foundation::vec3& start, const foundation::vec3& end)
{
    const auto projectiles = game::world().projectiles();
    if (!projectiles)
        return true;
    for (const auto& projectile : *projectiles)
    {
        if (projectile.subtype != game::projectile_kind::smoke_grenade || !projectile.smoke_active)
        {
            continue;
        }
        if (game::smoke_zone_of(projectile).crosses(start, end))
        {
            return true;
        }
    }
    return false;
}

combat_block_reason local_block_reason(const config::combat_profile::legit_checks& checks)
{
    const auto pawn = game::local_player().pawn();
    if (!pawn)
        return combat_block_reason::inactive;
    if (checks.airborne)
    {
        const auto flags =
            game::bound_memory().process.load<std::uint32_t>(pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id));
        if ((flags & 1u) == 0u)
            return combat_block_reason::airborne;
    }
    if (checks.flashed)
    {
        const auto raw_alpha = game::local_player().flash_alpha();

        const auto blindness = std::clamp(raw_alpha > 1.5f ? raw_alpha / 255.0f : raw_alpha, 0.0f, 1.0f);
        const auto threshold = std::clamp(checks.flash_threshold * 0.01f, 0.0f, 1.0f);
        if (blindness >= threshold)
            return combat_block_reason::flashed;
    }
    return combat_block_reason::none;
}

bool local_checks_blocked(const config::combat_profile::legit_checks& checks)
{
    return local_block_reason(checks) != combat_block_reason::none;
}

bool live_target_invulnerable(std::uintptr_t pawn, const bool snapshot_value)
{
    if (!pawn)
        return true;
    bool immunity{};
    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_id), &immunity,
                                           sizeof(immunity)))
    {
        return snapshot_value;
    }
    return immunity;
}

float distance_fov(const config::combat_profile::aimbot& cfg, float distance_units)
{
    const auto target_mode = cfg.fov_config.selection == config::combat_profile::fov_settings::target_distance;
    const auto near_distance =
        std::max(target_mode ? cfg.fov_config.target_near_distance_m : cfg.fov_config.near_distance_m, 0.25f);
    const auto far_distance = std::max(
        target_mode ? cfg.fov_config.target_far_distance_m : cfg.fov_config.far_distance_m, near_distance + 0.25f);
    const auto distance_m = std::clamp(distance_units / 52.4934f, near_distance, far_distance);

    auto t = std::log(distance_m / near_distance) / std::log(far_distance / near_distance);
    t = std::pow(
        std::clamp(t, 0.0f, 1.0f),
        std::clamp(target_mode ? cfg.fov_config.target_distance_curve : cfg.fov_config.distance_curve, 0.25f, 4.0f));
    const auto near_fov = std::max(target_mode ? cfg.fov_config.target_near_fov : cfg.fov_config.near_fov, 0.25f);
    const auto far_fov =
        std::clamp(target_mode ? cfg.fov_config.target_far_fov : cfg.fov_config.far_fov, 0.25f, near_fov);
    return std::exp(std::lerp(std::log(near_fov), std::log(far_fov), t));
}

float selection_fov(const config::combat_profile::aimbot& cfg, const foundation::vec3& eye,
                    const foundation::vec3& target)
{
    if (cfg.fov_config.selection == config::combat_profile::fov_settings::fixed)
        return static_cast<float>(cfg.fov);
    return distance_fov(cfg, (target - eye).length());
}

} // namespace features::aimbot
