#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/misc/match_flow.hpp>
#include <features/misc/misc.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/level.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>

#include <algorithm>

namespace features::misc
{
using namespace walk_detail;

walk_bot_t& walk_bot()
{
    static walk_bot_t value{};
    return value;
}

bool walk_bot_t::same_level(const player_state& player, const foundation::vec3& point)
{
    return same_level_impl(player.on_ground || player.on_ladder, player.origin, point);
}

bool walk_bot_t::read_player(player_state& out) const
{
    const auto pawn = game::local_player().pawn();
    if (!pawn || !game::local_player().alive())
        return false;
    const auto& process = game::bound_memory().process;
    std::uintptr_t node{};
    std::uint32_t flags{};
    std::uint8_t move_type{};
    if (!process.copy(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id), &node, sizeof(node)) || !node ||
        !process.copy(node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id), &out.origin, sizeof(out.origin)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id), &out.velocity, sizeof(out.velocity)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_fFlags"_id), &flags, sizeof(flags)) ||
        !process.copy(pawn + SCHEMA("C_BaseEntity", "m_MoveType"_id), &move_type, sizeof(move_type)))
        return false;
    if (!std::isfinite(out.origin.x) || !std::isfinite(out.origin.y) || !std::isfinite(out.origin.z))
        return false;
    if (!game::camera().sample(out.eye, out.view))
        return false;
    out.pawn = pawn;
    out.controller = game::local_player().controller();
    out.team = game::local_player().team();
    out.on_ground = (flags & 1u) != 0u;
    out.on_ladder = move_type == k_movetype_ladder;
    out.in_buy_zone = process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bInBuyZone"_id));
    out.buy_menu_open = process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bIsBuyMenuOpen"_id));
    const auto pawn_flag = [&](const std::int32_t offset) { return offset > 0 && process.load<bool>(pawn + offset); };
    // The current schema keeps these on C_CSPlayerPawn and the move flag on C_CSPlayerPawnBase (verified live; asking
    // the other class finds nothing, which once made every deathmatch buy window look closed).
    out.immune = pawn_flag(SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_id));
    out.moved = pawn_flag(SCHEMA("C_CSPlayerPawnBase", "m_bHasMovedSinceSpawn"_id));
    if (const auto spawn_offset = SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_id); spawn_offset > 0)
        out.spawn_time = process.load<float>(pawn + spawn_offset);
    if (const auto services = process.load<std::uintptr_t>(pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id)))
    {
        // m_hMyWeapons: a count and a pointer to the handles.
        const auto list = services + SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_id);
        const auto count = std::clamp(process.load<std::int32_t>(list), 0, 16);
        const auto handles = process.load<std::uintptr_t>(list + 8);
        std::array<std::uint32_t, 16> held{};
        if (count > 0 && handles && process.copy(handles, held.data(), static_cast<std::size_t>(count) * 4))
            for (int i = 0; i < count && !out.has_main_weapon; ++i)
                if (const auto weapon = game::entity_index().lookup(held[static_cast<std::size_t>(i)]))
                    if (const auto vdata =
                            process.load<std::uintptr_t>(weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8))
                        out.has_main_weapon = walk_logic::main_weapon_type(
                            process.load<std::uint32_t>(vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id)));
    }
    if (const auto services_offset = SCHEMA("CCSPlayerController", "m_pInGameMoneyServices"_id);
        services_offset > 0 && out.controller)
        if (const auto services = process.load<std::uintptr_t>(out.controller + services_offset))
            out.money = process.load<std::int32_t>(services +
                                                   SCHEMA("CCSPlayerController_InGameMoneyServices", "m_iAccount"_id));
    return true;
}

// Whether a game screen is over the world: the pause menu, the buy menu, the console, team select, a popup, the end of
// the match. The game shows the system cursor exactly while one of them takes the mouse (in play it is hidden and the
// mouse is relative), so the cursor is the first test; the screens the match automation tracks by their Panorama
// panels are the second.
bool walk_bot_t::game_ui_open(const player_state& player) const
{
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (::GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr)
        return true;
    return player.buy_menu_open || match_flow().holds_input();
}

void walk_bot_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    release_tapped_key(!cfg.enabled);
    if (!cfg.enabled)
    {
        features::aimbot::g_walk_autofire.store(false, std::memory_order_release);
        if (m_walking || !m_gated_keys.empty() || m_recording_active)
        {
            stop_walking();
            std::scoped_lock lock(m_route_mutex);
            m_recording_active = false;
        }
        return;
    }

    const auto graph = simulation::nav_mesh().current();
    if (graph != m_graph)
    {
        stop_walking();
        m_banned.clear();
        m_recent.clear();
        m_unreachable.clear();
        std::scoped_lock lock(m_route_mutex);
        m_graph = graph;
    }
    bind_controls();
    {
        std::scoped_lock lock(m_route_mutex);
        m_walking = true;
    }

    const auto now = clock::now();
    player_state player{};
    if (!read_player(player))
    {
        // Dead (or not readable): the route of this life is over.
        if (m_was_alive)
            reset_route();
        m_was_alive = false;
        m_previous_origin_valid = false;
        note_idle(3);
        release_keys();
        return;
    }
    // A new life, a bot takeover or a teleport starts from a new spot: plan afresh. CS2 reuses the pawn after a
    // respawn, so the pawn alone does not tell.
    // A spawn is told by the game's spawn time, which changes on every spawn: also the ones that move a living player
    // (warmup ending, a new round), which reuse the pawn and do not kill it.
    const bool teleported = m_previous_origin_valid && player.origin.distance(m_previous_origin) > 200.0f;
    const bool respawned = player.spawn_time != m_spawn_time;
    m_spawn_time = player.spawn_time;
    if (player.pawn != m_pawn || !m_was_alive || teleported || respawned)
    {
        if (player.pawn != m_pawn || !m_was_alive || respawned)
        {
            ++m_life;
            m_spawned_at = now;
        }
        m_pawn = player.pawn;
        m_banned.clear();
        m_hunt_target = 0;
        reset_route();
        m_player_until = {};
    }
    m_was_alive = true;
    m_previous_origin = player.origin;
    m_previous_origin_valid = true;
    // The walking direction, smoothed: random destinations prefer to keep it.
    if (const auto ground = flat(player.velocity); ground.length() > 60.0f)
    {
        const auto blended = m_heading * 0.95f + flat_direction(ground) * 0.05f;
        m_heading = flat_direction(blended);
    }
    {
        std::scoped_lock lock(m_route_mutex);
        m_last_origin = player.origin;
    }

    // Recording takes the player's own walk: the bot keeps its hands off meanwhile.
    record(player, now);
    if (m_recording_active)
    {
        note_idle(8);
        release_keys();
        look(player, {}, true, now);
        return;
    }
    // Recordings follow the map and the team (played back, or only drawn).
    if (cfg.mode == config::general_profile::walk_bot::recorded || cfg.show_recordings)
    {
        const auto level = game::level().name();
        auto map = level ? *level : std::string{};
        if (const auto slash = map.find_last_of("/\\"); slash != std::string::npos)
            map = map.substr(slash + 1);
        if (const auto dot = map.find_last_of('.'); dot != std::string::npos)
            map.resize(dot);
        if (map != m_recordings_map || player.team != m_recordings_team)
            load_recordings(map, player.team);
    }

    const bool focused = focus_ready();
    // Keys only go to the game in front: a buy sequence cut off by Alt+Tab is dropped, not typed into another window.
    if (game_window_in_front())
        auto_buy(player, now);
    else if (!m_buy_queue.empty())
        m_buy_queue.clear();

    const bool ui_open = game_ui_open(player);
    const bool movement_taken =
        auto_stop().active() || nade_helper().movement_taken() || !m_buy_queue.empty() || buy_holds_movement(player);
    // The player's own movement wins: while gated their keys are seen here, once released the plain key state.
    bool player_moving = false;
    for (const auto* control : {&m_forward, &m_back, &m_left, &m_right})
        if (control->binding.device == game::input_device::keyboard && control->binding.virtual_key &&
            !control->pressed && platform::windows::bound_input().physical_key_down(control->binding.virtual_key))
            player_moving = true;
    if (player_moving)
        m_player_until = now + std::chrono::milliseconds(1500);

    // Autofire: the trigger bot runs (with its own settings) while the walk bot has the game in play.
    features::aimbot::g_walk_autofire.store(cfg.autofire && focused && !ui_open, std::memory_order_release);
    if (!focused || ui_open || movement_taken || now < m_player_until)
    {
        note_idle(!focused ? 2u : ui_open ? 6u : movement_taken ? 4u : 7u);
        release_keys();
        look(player, {}, true, now);
        return;
    }
    if (!graph)
    {
        note_idle(1);
        release_keys();
        return;
    }
    gate_movement(true);

    // Fighting comes first: an enemy in sight stops the walk for the aim, a spawn-protected one is walked round
    // sideways, a hit from someone unseen turns the view while the walk goes on.
    const auto fight = combat(player, now);
    if (fight.what == combat_plan::kind::engage)
    {
        note_idle(9);
        for (auto* control : {&m_forward, &m_back, &m_left, &m_right, &m_duck, &m_jump})
            (void)set_control(*control, false);
        look(player, fight.view, false, now);
        return;
    }
    if (fight.what == combat_plan::kind::evade)
    {
        press_towards(fight.wish, player.view.y);
        (void)set_control(m_jump, false);
        (void)set_control(m_duck, false);
        look(player, fight.view, false, now);
        return;
    }

    // A plan for the selected mode; hunt keeps re-planning as its target moves.
    std::shared_ptr<const path> route;
    {
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }
    const auto mode = cfg.mode;
    // Hunting replaces the route with one to a known enemy (re-planned as it moves); with nobody to hunt the route
    // source below takes over.
    if (cfg.hunt)
        (void)plan_hunt(*graph, player, now);
    {
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }
    if (!route)
    {
        if (now < m_next_plan)
        {
            release_keys();
            return;
        }
        const bool planned = (mode == config::general_profile::walk_bot::recorded && plan_recording(*graph, player)) ||
                             plan_random(*graph, player);
        if (!planned)
        {
            note_idle(5);
            m_next_plan = now + std::chrono::milliseconds(500);
            release_keys();
            return;
        }
        std::scoped_lock lock(m_route_mutex);
        route = m_path;
    }

    scan_obstacles(*route, player, now);
    const auto view = drive(*graph, *route, player, now);
    {
        std::scoped_lock lock(m_route_mutex);
        if (m_path != route)
        {
            // drive() gave up on a crossing and dropped the route; the next tick plans again.
            look(player, fight.what == combat_plan::kind::react ? fight.view : view, false, now);
            return;
        }
    }

    const auto progress = m_progress.load(std::memory_order_acquire);
    // Arrived: remember the point (random walks avoid going back to it) and pick the next one on the next tick.
    if (progress >= route->back().distance - 24.0f && distance_2d(player.origin, route->back().position) < 40.0f)
    {
        m_recent.push_back(route->back().position);
        if (m_recent.size() > 6)
            m_recent.pop_front();
        release_keys();
        reset_route();
        return;
    }
    const bool pressing = m_forward.pressed || m_back.pressed || m_left.pressed || m_right.pressed;
    watch_progress(*graph, *route, player, now, pressing);
    auto_reload(player, route.get(), now);
    look(player, fight.what == combat_plan::kind::react ? fight.view : check_corners(*graph, player, view, now), false,
         now);
}
} // namespace features::misc
