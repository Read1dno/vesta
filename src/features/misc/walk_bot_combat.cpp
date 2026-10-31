#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/body/body.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <simulation/collision.hpp>

#include <algorithm>
#include <cmath>

namespace features::misc
{
using namespace walk_detail;

namespace
{
// What a player sees: the horizontal field of view of CS2's default 16:9 view, and a vertical one to match.
constexpr float k_half_fov_yaw = 53.0f;
constexpr float k_half_fov_pitch = 37.0f;

// The centre of the head hitbox, or where a head would be.
[[nodiscard]] foundation::vec3 head_of(const game::player_snapshot& other)
{
    if (other.bones.is_valid())
        for (const auto& box : other.hitboxes)
        {
            if (box.index < 0 || box.bone < 0 || box.bone >= static_cast<int>(other.bones.bones.size()) ||
                game::hitbox_data().hitgroup_from_hitbox(box.index) != game::rules::hitgroup::head)
                continue;
            return game::place_hitbox(other.bones.bones[static_cast<std::size_t>(box.bone)], box).center();
        }
    return other.origin + foundation::vec3{0.0f, 0.0f, other.is_ducked ? 46.0f : 64.0f};
}

// How far `point` is from the middle of the view, as yaw and pitch offsets in degrees.
[[nodiscard]] foundation::vec2 view_offset(const foundation::vec3& eye, const foundation::vec3& view,
                                           const foundation::vec3& point)
{
    const auto to = point - eye;
    const auto yaw = degrees(std::atan2(to.y, to.x));
    const auto pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
    return {pitch - view.x, foundation::wrap_yaw(yaw - view.y)};
}
} // namespace

// What a player does about enemies. One in sight (inside the 106 degree view, nothing in between, not spawn-protected)
// is fought: the feet stop and the view goes onto its head with the walk bot's own hand, the autofire shoots. A
// spawn-protected one close by is watched and walked round sideways at a distance until it can be fought. A hit from
// someone not in sight turns the view quickly to roughly where it came from (a player knows the side, not the spot)
// while walking on; whoever shows up there is then fought.
walk_bot_t::combat_plan walk_bot_t::combat(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto health = game::local_player().health();
    const bool hit = m_last_health > 0 && health > 0 && health < m_last_health;
    m_last_health = health;
    if (hit)
        m_hit_at = now;
    const auto players = game::world().players();
    if (!players)
        return {};
    const auto& world = simulation::collision();
    const auto in_sight = [&](const foundation::vec3& point)
    { return !world.valid() || !world.trace_ray(player.eye, point).hit; };

    const game::player_snapshot* fight = nullptr;
    float fight_score = 0.0f;
    const game::player_snapshot* protected_one = nullptr;
    float protected_distance = 1200.0f;
    for (const auto& other : *players)
    {
        if (other.health <= 0 || !other.pawn || other.pawn == player.pawn || !game::local_player().is_enemy(other.team))
            continue;
        const auto head = head_of(other);
        const auto offset = view_offset(player.eye, player.view, head);
        const bool in_view = std::abs(offset.y) <= k_half_fov_yaw && std::abs(offset.x) <= k_half_fov_pitch;
        if (!in_view || !in_sight(head))
            continue;
        if (other.invulnerable)
        {
            const auto distance = other.origin.distance(player.origin);
            if (distance < protected_distance)
            {
                protected_distance = distance;
                protected_one = &other;
            }
            continue;
        }
        // The one nearest the crosshair, and the one already fought when it is about as near.
        auto score = std::hypot(offset.x, offset.y) + other.origin.distance(player.origin) * 0.01f;
        if (other.pawn == m_fight_target)
            score *= 0.6f;
        if (!fight || score < fight_score)
        {
            fight = &other;
            fight_score = score;
        }
    }

    if (fight && cfg.aim)
    {
        m_fight_target = fight->pawn;
        m_fight_seen_at = now;
        m_react_until = {};
        return {combat_plan::kind::engage, {gaze::mode::fight, head_of(*fight), 0.0f}, {}};
    }
    // The enemy just fought stays the target for a moment after a sight line flickers (an arm, a box edge), so the
    // view does not jump between it and a corner every tick.
    if (cfg.aim && m_fight_target && now - m_fight_seen_at < std::chrono::milliseconds(300))
        for (const auto& other : *players)
            if (other.pawn == m_fight_target && other.health > 0 && !other.invulnerable)
                return {combat_plan::kind::engage, {gaze::mode::fight, head_of(other), 0.0f}, {}};
    m_fight_target = 0;

    if (protected_one)
    {
        // Sideways round it, keeping some distance; the side stays until it is blocked (then the other one).
        const auto toward = flat_direction(protected_one->origin - player.origin);
        const foundation::vec3 side{-toward.y * m_evade_side, toward.x * m_evade_side, 0.0f};
        if (world.valid())
        {
            const auto knee = player.origin + foundation::vec3{0.0f, 0.0f, 26.0f};
            if (world.trace_ray(knee, knee + side * 48.0f).hit)
                m_evade_side = -m_evade_side;
        }
        const auto away = protected_distance < 450.0f ? toward * -0.5f : foundation::vec3{};
        return {combat_plan::kind::evade, {gaze::mode::fight, head_of(*protected_one), 0.0f}, side + away};
    }

    if (cfg.react_to_hits && hit)
    {
        // The likely shooter: the nearest enemy that can see the player. Only its side is known: a quick turn (the
        // fight's hand) goes there give or take 15 degrees, at its head height; once it is in view it is fought.
        const game::player_snapshot* shooter = nullptr;
        float nearest = 1e9f;
        for (const auto& other : *players)
        {
            if (other.health <= 0 || !other.pawn || other.pawn == player.pawn ||
                !game::local_player().is_enemy(other.team) || other.invulnerable)
                continue;
            const auto distance = other.origin.distance(player.origin);
            if (distance < nearest && in_sight(head_of(other)))
            {
                nearest = distance;
                shooter = &other;
            }
        }
        const auto toward = shooter
                                ? flat_direction(shooter->origin - player.origin)
                                : foundation::vec3{-std::cos(player.view.y * std::numbers::pi_v<float> / 180.0f),
                                                   -std::sin(player.view.y * std::numbers::pi_v<float> / 180.0f), 0.0f};
        const auto spread =
            std::uniform_real_distribution<float>(-15.0f, 15.0f)(m_random) * std::numbers::pi_v<float> / 180.0f;
        const foundation::vec3 rough{toward.x * std::cos(spread) - toward.y * std::sin(spread),
                                     toward.x * std::sin(spread) + toward.y * std::cos(spread), 0.0f};
        const auto reach = shooter ? std::min(nearest, 1500.0f) : 600.0f;
        m_react_point = player.eye + rough * reach;
        if (shooter)
            m_react_point.z = shooter->origin.z + 64.0f;
        m_react_until = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(1200, 1600)(m_random));
    }
    if (now < m_react_until)
        return {combat_plan::kind::react, {gaze::mode::fight, m_react_point, 0.0f}, {}};
    return {};
}
} // namespace features::misc
