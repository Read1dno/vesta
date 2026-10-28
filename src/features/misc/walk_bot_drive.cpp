#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/journal.hpp>
#include <features/aimbot/aimbot.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <simulation/collision.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace features::misc
{
using namespace walk_detail;

namespace
{
constexpr float k_knee_height = 26.0f; // above a stair step, below a crouch-jump ledge
constexpr foundation::vec3 k_hull_half{15.0f, 15.0f, 8.0f};
} // namespace

// The steering point: a fixed distance ahead along the line, pulled closer while a player-sized box moving there at
// knee height would hit something (a wall corner the line passes close to).
foundation::vec3 walk_bot_t::clear_steering(const path& route, const player_state& player, const float progress,
                                            const float cap) const
{
    const auto& world = simulation::collision();
    const std::array lookahead{k_steer_lookahead, 40.0f, 28.0f, 16.0f};
    const auto from = player.origin + foundation::vec3{0.0f, 0.0f, k_knee_height};
    for (const auto distance : lookahead)
    {
        const auto target = point_at(route, std::min(progress + distance, cap));
        if (!world.valid())
            return target;
        const auto to = foundation::vec3{target.x, target.y, target.z + k_knee_height};
        if (distance_2d(from, to) < 4.0f)
            return target;
        const auto hit = world.sweep_hull(from, to, k_hull_half);
        if (!hit.hit || hit.fraction > 0.98f)
            return target;
    }
    return point_at(route, std::min(progress + 16.0f, cap));
}

walk_bot_t::gaze walk_bot_t::drive(const simulation::nav_graph& graph, const path& route, const player_state& player,
                                   const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto goal_distance = [&](const std::size_t index) { return route[index].goal_distance; };

    auto progress = m_progress.load(std::memory_order_acquire);
    // The next action not yet completed; the feet may not run past it.
    std::size_t action_index = route.size();
    for (std::size_t i = 0; i < route.size(); ++i)
        if (route[i].act != action::none && goal_distance(i) > progress + 0.5f)
        {
            action_index = i;
            break;
        }
    const auto cap = action_index < route.size() ? route[action_index].distance : route.back().distance;

    // Where on the line the player is: the nearest sample a little behind to well ahead of the last progress, on the
    // player's own level (stacked floors: the line can run straight below or above the player).
    float nearest_score = std::numeric_limits<float>::max();
    float nearest_progress = progress;
    for (const auto& point : route)
    {
        if (point.distance < progress - 48.0f || point.distance > progress + 240.0f || point.distance > cap)
            continue;
        if (!same_level(player, point.position))
            continue;
        const auto dz = std::abs(player.origin.z - point.position.z);
        const auto score = distance_2d(player.origin, point.position) + dz * 0.25f;
        if (score < nearest_score)
        {
            nearest_score = score;
            nearest_progress = point.distance;
        }
    }
    progress = std::max(progress, nearest_progress);
    m_progress.store(progress, std::memory_order_release);

    foundation::vec3 wish{};
    bool duck = false;
    bool jump = false;
    gaze view{};
    view.point = point_at(route, progress + k_look_ahead);

    const auto* pending = action_index < route.size() ? &route[action_index] : nullptr;
    const auto approach = pending ? distance_2d(player.origin, pending->position) : 0.0f;
    const bool engaged = pending && (approach < 48.0f || m_action_index == action_index || player.on_ladder ||
                                     progress >= pending->distance - 4.0f);
    const auto finish_action = [&]
    {
        progress = goal_distance(action_index);
        m_progress.store(progress, std::memory_order_release);
        m_action_index = static_cast<std::size_t>(-1);
    };
    const auto give_up_action = [&]
    {
        if (pending->from != 0xffffffffu)
            m_banned.insert({pending->from, pending->to});
        diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 10u + static_cast<std::uint32_t>(pending->act));
        reset_route();
    };

    if (pending && engaged && (pending->act == action::jump || pending->act == action::crouch_jump))
    {
        // Run at the landing point; take off at the edge, again after a failed try once back on the ground.
        wish = pending->goal - player.origin;
        view.point = pending->goal + flat_direction(pending->goal - pending->position) * 120.0f;
        const auto takeoff_range = pending->act == action::crouch_jump ? 40.0f : 34.0f;
        const bool at_edge = approach < takeoff_range ||
                             distance_2d(player.origin, pending->goal) <= distance_2d(pending->position, pending->goal);
        if (m_action_index != action_index)
        {
            m_action_index = action_index;
            m_action_started = now;
            m_jump_attempts = 0;
            m_runups = 0;
            m_jump_pressed = {};
            m_left_ground = false;
        }
        const auto since = now - m_jump_pressed;
        const bool raised = pending->goal.z - pending->position.z > 12.0f;
        const bool on_far_floor = raised && std::abs(player.origin.z - pending->goal.z) < 12.0f;
        // A jump across a gap needs the run-up: arriving at the edge slow (just out of a crouch, say) means stepping
        // back a little and running at it again, instead of jumping short.
        const auto speed = flat(player.velocity).length();
        if (!raised && player.on_ground && at_edge && speed < 190.0f && now >= m_runup_until && !m_left_ground &&
            since > std::chrono::milliseconds(450) && m_runups < 2)
        {
            m_runup_until = now + std::chrono::milliseconds(350);
            ++m_runups;
        }
        if (now < m_runup_until)
        {
            wish = pending->position - pending->goal; // back, away from the edge
            press_towards(wish, player.view.y);
            (void)set_control(m_jump, false);
            (void)set_control(m_duck, false);
            return view;
        }
        if (player.on_ground && at_edge && !on_far_floor && since > std::chrono::milliseconds(450) &&
            (raised || speed >= 190.0f || m_runups >= 2 || m_jump_attempts > 0))
        {
            if (m_jump_attempts >= 3)
            {
                give_up_action();
                return view;
            }
            jump = true;
            m_jump_pressed = now;
            m_left_ground = false;
            ++m_jump_attempts;
        }
        // Knocked off course (a turn in the air for a kill, a push): the crossing is given up after a while instead of
        // trying it forever, and the next plan starts from wherever the player came down.
        if (now - m_action_started > std::chrono::seconds(5))
        {
            give_up_action();
            return view;
        }
        if (since < std::chrono::milliseconds(50))
            jump = true;
        if (!player.on_ground)
            m_left_ground = true;
        // A crouch jump pulls the legs up once in the air and keeps them up until the landing.
        if (pending->act == action::crouch_jump && m_left_ground && !player.on_ground)
            duck = true;
        // Landed on the far side: on its floor level near the landing point. A ledge counts once the feet are up on
        // it; a gap only after an actual jump (both sides are at the same height).
        if (player.on_ground && std::abs(player.origin.z - pending->goal.z) < 12.0f &&
            distance_2d(player.origin, pending->goal) < 96.0f && (m_left_ground || raised))
            finish_action();
    }
    else if (pending && engaged && (pending->act == action::ladder_up || pending->act == action::ladder_down) &&
             pending->ladder >= 0 && static_cast<std::size_t>(pending->ladder) < graph.ladders().size())
    {
        const auto& ladder = graph.ladders()[static_cast<std::size_t>(pending->ladder)];
        const bool up = pending->act == action::ladder_up;
        if (m_action_index != action_index)
        {
            m_action_index = action_index;
            m_action_started = now;
            m_ladder_exit_since = {};
        }
        view.how = gaze::mode::fixed_pitch;
        // Across the ladder (to its left as seen climbing) and how far the player is off its centre line.
        const foundation::vec3 across{-ladder.facing.y, ladder.facing.x, 0.0f};
        const auto offset = player.origin - ladder.bottom;
        const auto lateral = offset.x * across.x + offset.y * across.y;
        const auto standoff = offset.x * ladder.facing.x + offset.y * ladder.facing.y;
        if (up)
        {
            // Face the rungs, look up. Off the ladder: first line up in front of its centre, then walk straight into
            // it; on it: keep pushing into the rungs and slide back to the centre. Off the top once the feet are up
            // at the landing's floor, or when the climb stalls under a ceiling near the top.
            view.point = player.origin - ladder.facing * 100.0f;
            view.pitch = -35.0f;
            if (player.on_ladder)
            {
                if (m_ladder_peak_z < player.origin.z - 1.0f)
                {
                    m_ladder_peak_z = player.origin.z;
                    m_ladder_rising_at = now;
                }
            }
            else
            {
                m_ladder_peak_z = player.origin.z;
                m_ladder_rising_at = now;
            }
            // The climb is over when the feet are level with the exit, or when they stop rising on the ladder at any
            // height (a shaft whose ceiling is below the ladder's top in the mesh, like Nuke's vent: waiting for the
            // mesh's top there meant pushing into the rungs forever). Then face the exit and go; still on the
            // ladder a moment later, jump off towards it with the legs pulled up to clear the lip.
            const bool level_with_exit = player.origin.z >= pending->goal.z - 4.0f;
            const bool stalled = player.on_ladder && player.origin.z > ladder.bottom.z + 32.0f &&
                                 now - m_ladder_rising_at > std::chrono::milliseconds(300);
            if (level_with_exit || stalled)
            {
                if (m_ladder_exit_since == clock::time_point{})
                    m_ladder_exit_since = now;
                wish = pending->goal - player.origin;
                view.point = pending->goal + flat_direction(pending->goal - player.origin) * 100.0f;
                view.pitch = 0.0f;
                const auto exiting_for = now - m_ladder_exit_since;
                if (player.on_ladder && exiting_for > std::chrono::milliseconds(300) &&
                    exiting_for < std::chrono::milliseconds(360))
                    jump = true;
                if (!player.on_ground && exiting_for > std::chrono::milliseconds(300))
                    duck = true;
            }
            else
            {
                m_ladder_exit_since = {};
                // Back to the rungs' centre only when clearly off it: a correction every tick flipped the side keys
                // back and forth (the shaking on the ladder).
                const auto centring = std::abs(lateral) > 6.0f ? std::clamp(lateral / 16.0f, -1.0f, 1.0f) * 0.6f : 0.0f;
                if (!player.on_ladder && (std::abs(lateral) > 8.0f || standoff > 40.0f || standoff < 4.0f))
                    wish = ladder.bottom + ladder.facing * 16.0f - player.origin; // line up first
                else
                    wish = ladder.facing * -1.0f - across * centring;
            }
        }
        else
        {
            // Walk out over the top facing away from the wall, looking down: forward then climbs down.
            view.point = player.origin + ladder.facing * 100.0f;
            view.pitch = 30.0f;
            wish =
                player.on_ladder || distance_2d(player.origin, ladder.top) < 24.0f
                    ? ladder.facing -
                          across * (std::abs(lateral) > 6.0f ? std::clamp(lateral / 16.0f, -1.0f, 1.0f) * 0.6f : 0.0f)
                    : ladder.top + ladder.facing * 4.0f - player.origin;
            if (player.origin.z <= ladder.bottom.z + 8.0f)
                wish = pending->goal - player.origin;
        }
        // The ceiling over a ladder can be low too.
        for (const auto& point : route)
            if (point.duck && std::abs(point.distance - pending->distance) < 48.0f)
                duck = true;
        const bool done = player.on_ground && !player.on_ladder &&
                          distance_2d(player.origin, pending->goal) < (up ? 40.0f : 64.0f) &&
                          std::abs(player.origin.z - pending->goal.z) < 28.0f;
        if (done)
            finish_action();
        else if (now - m_action_started > std::chrono::seconds(10))
        {
            give_up_action();
            return view;
        }
    }
    else
    {
        // Ordinary walking: aim the feet ahead along the line (closer where a corner is in the way), never past the
        // next action, and keep a little off the walls on either side.
        auto target = clear_steering(route, player, progress, cap);
        // A steering point right at the feet flips the direction every tick (pressing left, right, left on one spot):
        // steer at the full look-ahead then and let the side probes below keep the player off the wall.
        if (distance_2d(player.origin, target) < 20.0f)
            target = point_at(route, std::min(progress + k_steer_lookahead, cap));
        wish = flat_direction(target - player.origin);
        if (pending && distance_2d(player.origin, pending->position) < 4.0f)
            wish = flat_direction(pending->goal - player.origin);
        const auto& world = simulation::collision();
        if (world.valid() && wish.length() > 0.5f)
        {
            const foundation::vec3 side{-wish.y, wish.x, 0.0f};
            const auto knee = player.origin + foundation::vec3{0.0f, 0.0f, k_knee_height};
            constexpr float k_probe = 24.0f;
            for (const float sign : {1.0f, -1.0f})
            {
                const auto hit = world.trace_ray(knee, knee + side * (k_probe * sign));
                if (hit.hit && hit.distance < k_probe)
                    wish -= side * (sign * 0.6f * (k_probe - hit.distance) / k_probe);
            }
        }
        for (const auto& point : route)
            if (point.duck && point.distance > progress - 8.0f && point.distance < progress + 40.0f)
            {
                duck = true;
                break;
            }
    }

    // A stuck response from watch_progress(): one jump (a crouch jump) to get over what blocks the feet.
    if (now < m_stuck_jump_until)
    {
        jump = now < m_stuck_jump_until - std::chrono::milliseconds(700);
        duck = duck || (!player.on_ground && now < m_stuck_jump_until);
    }

    // A door or a breakable across the way ahead: look at it and use or break it.
    if (m_obstacle.type != obstacle::kind::none &&
        (!(pending && engaged) || (m_obstacle.type == obstacle::kind::breakable && player.on_ground)))
    {
        const bool door = m_obstacle.type == obstacle::kind::door;
        if (m_obstacle.distance < (door ? 110.0f : 220.0f))
        {
            view.how = gaze::mode::aim;
            view.point = m_obstacle.center;
            const auto to = m_obstacle.center - player.eye;
            const auto want_yaw = degrees(std::atan2(to.y, to.x));
            const auto want_pitch = -degrees(std::atan2(to.z, std::hypot(to.x, to.y)));
            const auto off = std::hypot(foundation::wrap_yaw(want_yaw - player.view.y), want_pitch - player.view.x);
            if (door && m_obstacle.distance < 80.0f && off < 12.0f && now >= m_next_use)
            {
                tap_key(static_cast<std::uint16_t>(std::clamp(snapshot->general.m_game_controls.use, 1, 255)));
                m_next_use = now + std::chrono::milliseconds(1200);
                // Once per door: an opened door that swings towards the player still lies across the line, and a
                // second press closed it on the player (stuck between the door and the wall).
                m_used_doors[m_obstacle.entity] = now;
                diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                                  diagnostics::code_domain::mode, 1u);
            }
            if (!door)
            {
                // Break it before going on: stand off a little and do not take a pending jump into it.
                if (off < 5.0f)
                    tap_attack(now);
                if (m_obstacle.distance < 72.0f)
                    wish = {};
                jump = false;
            }
            // Wait for the door to swing instead of walking into it.
            if (door && m_obstacle.distance < 48.0f)
                wish = {};
        }
    }
    if (m_attack_down && now >= m_attack_release)
    {
        (void)platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_attack_down = false;
    }

    press_towards(wish, player.view.y);
    (void)set_control(m_jump, jump);
    (void)set_control(m_duck, duck);
    return view;
}

// Off the line or stuck while pressing keys: plan again from where the player is. Stuck goes in stages: one crouch jump
// (a low obstacle or a lip), then the crossing the player is stuck at is left out of the next plan, and after a couple
// of re-plans at the same spot the destination is given up.
void walk_bot_t::watch_progress(const simulation::nav_graph& graph, const path& route, const player_state& player,
                                const clock::time_point now, const bool pressing)
{
    // Headway: the feet have to get 96 units away from where they last were within a while, whatever the speed says
    // (a crouch walk does it in about 1.1 s).
    // A door being opened or a board being shot is not being stuck: the feet wait at it on purpose.
    if (m_obstacle.type != obstacle::kind::none && m_obstacle.distance < 220.0f)
    {
        m_headway.reset();
        m_stuck_since = {};
        return;
    }
    const auto still_for = m_headway.update(player.origin, now, pressing);
    if (m_action_index != static_cast<std::size_t>(-1) && m_action_index < route.size() && !player.on_ladder)
    {
        // Inside a jump crossing: no headway for 2.5 s gives the crossing up (the ladder has its own limit).
        if (still_for > std::chrono::milliseconds(2500))
        {
            const auto& pending = route[m_action_index];
            if (pending.from != 0xffffffffu)
                m_banned.insert({pending.from, pending.to});
            diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::warning,
                              diagnostics::code_domain::mode, 23u);
            reset_route();
        }
        return;
    }
    if (player.on_ladder || m_action_index != static_cast<std::size_t>(-1))
    {
        m_stuck_since = {};
        return;
    }
    const auto progress = m_progress.load(std::memory_order_acquire);
    float deviation = std::numeric_limits<float>::max();
    for (const auto& point : route)
        if (point.distance > progress - 64.0f && point.distance < progress + 300.0f &&
            same_level(player, point.position))
            deviation = std::min(deviation, distance_2d(player.origin, point.position));
    if (player.on_ground)
    {
        if (deviation < 72.0f)
            m_off_path_since = {};
        else if (m_off_path_since == clock::time_point{})
            m_off_path_since = now;
        else if (now - m_off_path_since > std::chrono::milliseconds(500))
        {
            diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                              diagnostics::code_domain::mode, 20u);
            reset_route();
            return;
        }
    }

    // Slow, or moving without getting anywhere for 1.5 s.
    const bool slow = flat(player.velocity).length() < 35.0f || still_for > std::chrono::milliseconds(1500);
    if (!pressing || !slow || !player.on_ground)
    {
        if (!slow)
            m_stuck_since = {};
        return;
    }
    if (m_stuck_since == clock::time_point{})
    {
        m_stuck_since = now;
        return;
    }
    const auto stuck_for = now - m_stuck_since;
    if (m_stuck_stage == 0 && stuck_for > std::chrono::milliseconds(600))
    {
        // One crouch jump to get over what blocks the feet.
        m_stuck_stage = 1;
        m_stuck_since = now;
        m_stuck_jump_until = now + std::chrono::milliseconds(760);
        m_headway.reset();
        diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                          diagnostics::code_domain::mode, 21u);
        return;
    }
    if (m_stuck_stage >= 1 && stuck_for > std::chrono::milliseconds(900))
    {
        // Leave out the crossing between where the player stands and where it was heading.
        const auto here = graph.locate(player.origin);
        const auto ahead = graph.locate(point_at(route, progress + 48.0f), 64.0f);
        if (here >= 0 && ahead >= 0 && here != ahead)
            m_banned.insert({static_cast<std::uint32_t>(here), static_cast<std::uint32_t>(ahead)});
        m_replans_here = m_stuck_at.distance(player.origin) < 96.0f ? m_replans_here + 1 : 1;
        m_stuck_at = player.origin;
        if (m_replans_here >= 2)
        {
            // This destination cannot be reached from here: pick another one, and never this spot again on this map.
            if (const auto area = graph.locate(m_destination, 64.0f); area >= 0)
                m_unreachable.insert(static_cast<std::uint32_t>(area));
            m_recent.push_back(m_destination);
            if (m_recent.size() > 6)
                m_recent.pop_front();
            m_replans_here = 0;
        }
        diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::warning,
                          diagnostics::code_domain::mode, 22u);
        reset_route();
    }
}

// Doors and breakables whose box lies across the next stretch of the line and actually blocks it.
void walk_bot_t::scan_obstacles(const path& route, const player_state& player, const clock::time_point now)
{
    if (now < m_next_obstacle_scan)
        return;
    m_next_obstacle_scan = now + std::chrono::milliseconds(200);
    m_obstacle = {};
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    if (route.empty())
        return;
    const auto& process = game::bound_memory().process;
    const auto progress = m_progress.load(std::memory_order_acquire);
    // Doors and breakables come from the map geometry's scan of the whole entity list (with live collision only, so
    // broken glass and vents drop out by themselves); the general entity catalogue does not reach most map props.
    const auto interactive = simulation::interactive_entities();
    if (!interactive)
        return;
    for (const auto& entity : *interactive)
    {
        const auto type = entity.door ? obstacle::kind::door : obstacle::kind::breakable;
        if (!entity.door && !cfg.breakables)
            continue;
        if (entity.door)
            if (const auto used = m_used_doors.find(entity.entity);
                used != m_used_doors.end() && now - used->second < std::chrono::seconds(4))
                continue; // just opened: walk through, do not press again
        const auto origin = entity.origin;
        if (!std::isfinite(origin.x) || origin.distance(player.origin) > 420.0f)
            continue;
        const auto rotation = entity.angles;
        const auto collision = entity.entity + SCHEMA("C_BaseModelEntity", "m_Collision"_id);
        const auto mins = process.load<foundation::vec3>(collision + SCHEMA("CCollisionProperty", "m_vecMins"_id));
        const auto maxs = process.load<foundation::vec3>(collision + SCHEMA("CCollisionProperty", "m_vecMaxs"_id));
        if (!std::isfinite(mins.x) || !std::isfinite(maxs.x) || maxs.x < mins.x || maxs.y < mins.y)
            continue;
        const auto yaw = (std::isfinite(rotation.y) ? rotation.y : 0.0f) * std::numbers::pi_v<float> / 180.0f;
        const auto c = std::cos(yaw), s = std::sin(yaw);
        const auto to_world = [&](const foundation::vec3& local)
        { return origin + foundation::vec3{c * local.x - s * local.y, s * local.x + c * local.y, local.z}; };
        const auto center = to_world((mins + maxs) * 0.5f);
        // The next stretch of the line against the box, widened by the player's radius.
        for (float along = 0.0f; along <= 140.0f; along += 10.0f)
        {
            const auto feet = point_at(route, progress + along);
            const auto d = feet - origin;
            const foundation::vec3 local{c * d.x + s * d.y, -s * d.x + c * d.y, d.z};
            constexpr float k_radius = 16.0f;
            const bool inside = local.x > mins.x - k_radius && local.x < maxs.x + k_radius &&
                                local.y > mins.y - k_radius && local.y < maxs.y + k_radius &&
                                local.z + 64.0f > mins.z && local.z < maxs.z;
            if (!inside)
                continue;
            // Only when it really blocks: the way from the player to that point, at knee and chest height, passes
            // through the object's own box (an open door has swung out of the way). The box, not the map geometry:
            // brush breakables have no model in it, and a ray through the geometry missed them.
            const auto local_point = [&](const foundation::vec3& world)
            {
                const auto d = world - origin;
                return foundation::vec3{c * d.x + s * d.y, -s * d.x + c * d.y, d.z};
            };
            const auto crosses_box = [&](const foundation::vec3& from, const foundation::vec3& to)
            {
                // Slab test of the segment against the box in the object's frame.
                const auto a = local_point(from);
                const auto b = local_point(to);
                float enter = 0.0f, leave = 1.0f;
                const float lo[3]{mins.x, mins.y, mins.z}, hi[3]{maxs.x, maxs.y, maxs.z};
                const float pa[3]{a.x, a.y, a.z}, pb[3]{b.x, b.y, b.z};
                for (int axis = 0; axis < 3; ++axis)
                {
                    const auto d = pb[axis] - pa[axis];
                    if (std::abs(d) < 1e-5f)
                    {
                        if (pa[axis] < lo[axis] || pa[axis] > hi[axis])
                            return false;
                        continue;
                    }
                    auto t0 = (lo[axis] - pa[axis]) / d;
                    auto t1 = (hi[axis] - pa[axis]) / d;
                    if (t0 > t1)
                        std::swap(t0, t1);
                    enter = std::max(enter, t0);
                    leave = std::min(leave, t1);
                    if (enter > leave)
                        return false;
                }
                return true;
            };
            // From the player to a point past this one along the way: the first point inside the widened box
            // usually lies just before a thin door or a vent's grate, and the line to it alone never crosses the
            // object (which threw every door and vent out until now).
            const auto beyond = point_at(route, progress + along + 40.0f);
            bool blocked = false;
            for (const float height : {20.0f, 44.0f})
                blocked = blocked || crosses_box(player.origin + foundation::vec3{0.0f, 0.0f, height},
                                                 beyond + foundation::vec3{0.0f, 0.0f, height});
            if (!blocked)
                continue;
            if (m_obstacle.type == obstacle::kind::none || along < m_obstacle.distance)
                m_obstacle = {type, entity.entity, center, along};
            break;
        }
    }
    m_render_obstacle.store(m_obstacle.type == obstacle::kind::none ? foundation::vec3{} : m_obstacle.center,
                            std::memory_order_release);
    // Journal: walkbot.obstacle code 3 a door, 4 a breakable across the way (1 use pressed, 2 attack pressed; 21 stuck:
    // crouch jump, 22 stuck: crossing left out).
    if (m_obstacle.type != obstacle::kind::none && m_obstacle.entity != m_noted_obstacle)
    {
        m_noted_obstacle = m_obstacle.entity;
        diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                          diagnostics::code_domain::mode, m_obstacle.type == obstacle::kind::door ? 3u : 4u);
    }
}

void walk_bot_t::press_towards(const foundation::vec3& direction, const float view_yaw)
{
    const auto wish = flat_direction(direction);
    if (wish.length() < 0.5f)
    {
        (void)set_control(m_forward, false);
        (void)set_control(m_back, false);
        (void)set_control(m_left, false);
        (void)set_control(m_right, false);
        return;
    }
    const auto yaw = view_yaw * std::numbers::pi_v<float> / 180.0f;
    const foundation::vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
    const foundation::vec3 right{std::sin(yaw), -std::cos(yaw), 0.0f};
    const auto along = wish.dot(forward);
    const auto across = wish.dot(right);
    // Eight directions: a key is held when the wish has more than ~22.5 degrees of its axis.
    constexpr float k_axis = 0.38f;
    (void)set_control(m_forward, along > k_axis);
    (void)set_control(m_back, along < -k_axis);
    (void)set_control(m_right, across > k_axis);
    (void)set_control(m_left, across < -k_axis);
}

bool walk_bot_t::set_control(owned_control& control, const bool pressed)
{
    return input_lifecycle::transition(control, pressed,
                                       [](const game::input_binding& binding, const bool down)
                                       {
                                           return binding.device == game::input_device::keyboard &&
                                                  platform::windows::bound_input().key(binding.virtual_key, down);
                                       });
}

void walk_bot_t::bind_controls()
{
    auto& bindings = game::input_bindings();
    const auto rebind = [&](owned_control& control, const game::input_action action_id)
    {
        if (!control.pressed)
            control.binding = bindings.resolve(action_id);
    };
    rebind(m_forward, game::input_action::forward);
    rebind(m_back, game::input_action::back);
    rebind(m_left, game::input_action::left);
    rebind(m_right, game::input_action::right);
    rebind(m_duck, game::input_action::duck);
    rebind(m_jump, game::input_action::jump);
}

// While walking the player's own movement keys are held back, so a press on one of them is seen as "take over".
void walk_bot_t::gate_movement(const bool enabled)
{
    std::vector<std::uint16_t> keys;
    if (enabled)
        for (const auto* control : {&m_forward, &m_back, &m_left, &m_right})
            if (control->binding.device == game::input_device::keyboard && control->binding.virtual_key)
                keys.push_back(control->binding.virtual_key);
    if (keys == m_gated_keys)
        return;
    platform::windows::bound_input().set_movement_gate(platform::windows::input_gateway::gate_owner::walk_bot, keys,
                                                       !keys.empty());
    m_gated_keys = std::move(keys);
}

void walk_bot_t::release_keys()
{
    for (auto* control : {&m_forward, &m_back, &m_left, &m_right, &m_duck, &m_jump})
        (void)set_control(*control, false);
    if (m_attack_down)
    {
        (void)platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        m_attack_down = false;
    }
    gate_movement(false);
}

void walk_bot_t::stop_walking()
{
    release_tapped_key(true);
    release_keys();
    reset_route();
    m_buy_queue.clear();
    std::scoped_lock lock(m_route_mutex);
    m_walking = false;
}

// A key press held for a few frames like a finger would. A press and release in the same instant can fall between two
// game frames, and the buy menu then never sees the number keys.
void walk_bot_t::tap_key(const std::uint16_t key)
{
    release_tapped_key(true);
    if (!game_window_in_front())
        return;
    if (!platform::windows::bound_input().key(key, true))
        return;
    m_tapped_key = key;
    m_tapped_release = clock::now() + std::chrono::milliseconds(std::uniform_int_distribution<int>(45, 85)(m_random));
}

void walk_bot_t::release_tapped_key(const bool now_anyway)
{
    if (m_tapped_key && (now_anyway || clock::now() >= m_tapped_release))
    {
        (void)platform::windows::bound_input().key(m_tapped_key, false);
        m_tapped_key = 0;
    }
}

void walk_bot_t::tap_attack(const clock::time_point now)
{
    // Never while the aim owns the trigger, and one shot at a time.
    if (m_attack_down || now < m_next_attack ||
        recent(features::aimbot::aim().view_engaged_ticks(), now, std::chrono::milliseconds(400)))
        return;
    if (platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_down))
    {
        m_attack_down = true;
        m_attack_release = now + std::chrono::milliseconds(70);
        m_next_attack = now + std::chrono::milliseconds(380);
        diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                          diagnostics::code_domain::mode, 2u);
    }
}
} // namespace features::misc
