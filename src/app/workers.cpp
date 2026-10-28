#include <stdafx.hpp>
#include <simulation/collision.hpp>
#include <diagnostics/performance.hpp>
#include <config/misc.hpp>
#include <diagnostics/health.hpp>
#include <game/state/services.hpp>
#include <game/memory/remote_pointer.hpp>
#include <app/context.hpp>
#include <app/workers.hpp>
#include <game/state/level.hpp>
#include <diagnostics/journal.hpp>
#include <app/map_loader.hpp>
#include <app/map_absence.hpp>
#include <game/input/bindings.hpp>
#include <game/input/action_state.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/misc.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/misc/walk_bot.hpp>
#include <features/misc/match_flow.hpp>
#include <features/trigger/seed_trigger.hpp>
#include <simulation/shot_state.hpp>
#include <simulation/seed_schedule.hpp>
#include <features/visuals/visuals.hpp>

namespace app::workers
{

namespace
{

// Progress markers for the watchdog: the game loop stamps which stage it is
// about to run, so if it ever stops advancing we can see exactly where.
constexpr const char* k_step_names[]{
    "local.update",   "auto_accept.tick",  "entities.refresh", "collector.run", "map-name read",
    "parse dispatch", "bomb_damage.parse", "heartbeat",        "sleep",
};

std::atomic<int> g_game_step{-1};
std::atomic<std::uint64_t> g_game_iter{0};

[[nodiscard]] bool shared_ballistics_requested()
{
    const auto key_down = [](const int key) { return key > 0 && (::GetAsyncKeyState(key) & 0x8000) != 0; };
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return false;
    const auto& visuals = snapshot->visual;
    const auto& misc = snapshot->general;
    if (misc.m_bullet_tracers.enabled || misc.m_hitmarker.enabled || misc.m_hitsound.enabled ||
        misc.m_hitsound.show_damage || misc.m_grenades.enabled ||
        (visuals.m_crosshair.enabled && visuals.m_crosshair.penetration_enabled))
    {
        return true;
    }

    const auto config = snapshot->combat.get(game::local_player().weapon_type());
    bool rcs_requested{};
    if (config.aimbot.rcs.enabled)
        rcs_requested = game::physical_action_down(game::input_action::attack, app::context().input);
    if (rcs_requested || config.other.penetration_crosshair ||
        (config.aimbot.enabled &&
         config::combat_profile::activation_active(config.aimbot.activation_mode, config.aimbot.key)))
    {
        return true;
    }
    const auto independent_seed = config.triggerbot.seed_type != config::combat_profile::seed_mode::none;
    if (config.triggerbot.enabled && !independent_seed &&
        config::combat_profile::activation_active(config.triggerbot.activation_mode, config.triggerbot.key))
    {
        return true;
    }
    const auto& grenade = snapshot->combat.global.grenade_aim;
    return grenade.enabled && key_down(grenade.key);
}

} // namespace

namespace
{

// Reads the current map name from the engine's global variables. The slot moves between game builds, so it is
// found by probing once and then read from the same offset until it stops holding a map name.
class map_name_reader
{
public:
    std::string read(std::uintptr_t global_vars);

private:
    std::uintptr_t m_offset{};
    std::chrono::steady_clock::time_point m_last_probe{};
};

std::string map_name_reader::read(std::uintptr_t global_vars)
{
    const auto try_read = [](std::uintptr_t addr) -> std::string
    {
        const auto ptr = app::context().process.load<std::uintptr_t>(addr);
        if (!game::is_user_pointer(ptr))
        {
            return {};
        }

        auto value = app::context().process.load_text(ptr, 64);
        // Community servers append their own title ("awp_lego_2 | Server name | site.ru"): the map is the
        // first word. Without this the whole slot was rejected and no geometry was ever requested.
        if (const auto space = value.find(' '); space != std::string::npos)
            value.resize(space);
        if (value.size() < 3)
        {
            return {};
        }

        auto has_separator = false;
        for (const char c : value)
        {
            const auto ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                            c == '-' || c == '.' || c == '/' || c == '\\';
            if (!ok)
            {
                return {};
            }

            has_separator |= (c == '_' || c == '/');
        }

        // All real map identifiers contain '_' or a path separator; plain words
        // are far more likely to be an unrelated string field.
        return has_separator ? value : std::string{};
    };

    if (m_offset)
    {
        const auto value = try_read(global_vars + m_offset);
        if (!value.empty())
            return value;
        m_offset = 0;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - m_last_probe < std::chrono::seconds(1))
    {
        return {};
    }
    m_last_probe = now;

    for (std::uintptr_t off = 0x150; off <= 0x1f0; off += 8)
    {
        const auto value = try_read(global_vars + off);
        if (value.empty())
        {
            continue;
        }

        m_offset = off;
        app::context().diagnostics.info("[diag] map name slot found at global_vars+{:#x} (\"{}\")", off, value);
        return value;
    }

    return {};
}

} // namespace

void game(control control)
{
    constexpr auto update_interval = std::chrono::nanoseconds(1'000'000'000 / 64);
    auto next_update = std::chrono::steady_clock::now();
    auto next_idle_entity_refresh = std::chrono::steady_clock::time_point{};
    auto next_map_probe = std::chrono::steady_clock::time_point{};
    std::string last_map{};
    map_name_reader map_name;
#if defined(VESTA_WINDOWED_PROFILE) && VESTA_WINDOWED_PROFILE
    auto next_profile_state = std::chrono::steady_clock::time_point{};
#endif

    app::map_loader map_loader{diagnostics::health().geometry};
    app::map_absence_guard map_absence;

    control.sleep_for(std::chrono::milliseconds(500));

    std::uint64_t iterations{0};
#if defined(VESTA_ENABLE_CONSOLE) && VESTA_ENABLE_CONSOLE
    auto last_heartbeat = std::chrono::steady_clock::now();
#endif

    while (!control.stopping())
    {
        std::optional<diagnostics::performance::scope> game_profile{};
        game_profile.emplace(diagnostics::performance::zone::game_loop);
        diagnostics::performance::flush_if_due();
        ++iterations;
        g_game_iter.store(iterations, std::memory_order_relaxed);

        g_game_step.store(0, std::memory_order_relaxed);
        {
            VESTA_PERF_SCOPE(local_update);
            game::local_player().update();
        }

#if defined(VESTA_WINDOWED_PROFILE) && VESTA_WINDOWED_PROFILE
        if (std::chrono::steady_clock::now() >= next_profile_state)
        {
            next_profile_state = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            diagnostics::emit(game::local_player().valid() ? diagnostics::event::profile_scene_ready
                                                           : diagnostics::event::profile_scene_unavailable);
        }
#endif

        g_game_step.store(1, std::memory_order_relaxed);
        {
            VESTA_PERF_SCOPE(auto_accept);
            features::misc::auto_accept().tick();
        }
        features::misc::match_flow().tick();

        std::uintptr_t global_vars{0};
        std::string current_map{};

        const auto local_valid = game::local_player().valid();
        if (local_valid)
        {
            map_absence.observe(true, std::chrono::steady_clock::now());
        }

        if (local_valid)
        {
            const auto loop_now = std::chrono::steady_clock::now();
            const auto directory_requested = game::world().entity_directory_requested(app::context().menu.is_open());
            if (directory_requested || loop_now >= next_idle_entity_refresh)
            {
                g_game_step.store(2, std::memory_order_relaxed);
                {
                    VESTA_PERF_SCOPE(entity_refresh);
                    game::entity_index().refresh();
                }
                next_idle_entity_refresh = loop_now + std::chrono::milliseconds(500);
            }

            g_game_step.store(3, std::memory_order_relaxed);
            {
                VESTA_PERF_SCOPE(world_update);
                game::world().run(app::context().menu.is_open());
            }
            {
                VESTA_PERF_SCOPE(grenade_prediction_tick);
                features::visuals::grenade_prediction().tick();
            }
            {
                VESTA_PERF_SCOPE(bullet_feedback_tick);
                features::visuals::bullet_impacts().tick();
            }
            {
                VESTA_PERF_SCOPE(bomb_update);
                features::visuals::bomb().tick();
            }
            {
                VESTA_PERF_SCOPE(radar_update);
                features::visuals::radar().tick();
            }

            g_game_step.store(4, std::memory_order_relaxed);
            if (loop_now >= next_map_probe)
            {
                next_map_probe = loop_now + std::chrono::milliseconds(250);
                global_vars = app::context().process.load<std::uintptr_t>(app::context().addresses.global_vars);
                if (global_vars)
                {
                    current_map = map_name.read(global_vars);

                    if (!current_map.empty() && current_map != last_map)
                    {
                        app::context().diagnostics.info("map change: {} -> {}", last_map.empty() ? "none" : last_map,
                                                        current_map);
                        last_map = current_map;
                        game::level().publish(current_map);

                        g_game_step.store(5, std::memory_order_relaxed);

                        features::visuals::grenade_prediction().reset();
                        features::visuals::bullet_impacts().reset();
                        map_loader.request(current_map);
                    }
                }
            }
        }
        else
        {
            if (!last_map.empty() && map_absence.observe(false, std::chrono::steady_clock::now()))
            {
                last_map = {};
                game::level().publish({});
                features::visuals::grenade_prediction().reset();
                features::visuals::bullet_impacts().reset();
                map_loader.request({});
            }
        }

        g_game_step.store(7, std::memory_order_relaxed);
#if defined(VESTA_ENABLE_CONSOLE) && VESTA_ENABLE_CONSOLE
        const auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= std::chrono::seconds(3))
        {
            last_heartbeat = now;
            app::context().diagnostics.info(
                "[heartbeat] iter={} valid={} controller={:#x} pawn={:#x} entity_list={:#x} entities={} players={} "
                "global_vars={:#x} map=\"{}\" bvh={} bomb_dmg={}",
                iterations, game::local_player().valid(), game::local_player().controller(),
                game::local_player().pawn(), game::entity_index().list_address(), game::entity_index().all()->size(),
                game::world().players()->size(), global_vars, current_map, simulation::collision().valid(),
                simulation::blast_damage().point_count());
        }
#endif

        g_game_step.store(8, std::memory_order_relaxed);
        game_profile.reset();
        next_update += update_interval;
        const auto after_work = std::chrono::steady_clock::now();
        if (next_update <= after_work)
        {
            next_update = after_work;
        }
        else
        {
            control.sleep_until(next_update);
        }
    }
}

void watchdog(control control)
{
    std::uint64_t last_iter{0};

    while (!control.stopping())
    {
        control.sleep_for(std::chrono::seconds(3));
        if (control.stopping())
            return;

        const auto iter = g_game_iter.load(std::memory_order_relaxed);
        const auto step = g_game_step.load(std::memory_order_relaxed);
        const auto* step_name =
            (step >= 0 && step < static_cast<int>(std::size(k_step_names))) ? k_step_names[step] : "not-started";

        if (iter == last_iter)
        {
            app::context().diagnostics.warning("[watchdog] game thread STUCK at \"{}\" (iter={})", step_name, iter);
        }
        else
        {
            app::context().diagnostics.info("[watchdog] game thread alive: iter={} step=\"{}\"", iter, step_name);
        }

        last_iter = iter;
    }
}

void combat(control control)
{
    // 250 Hz lets the seed trigger react within one server tick (15.625 ms at 64 Hz).
    constexpr auto target_tps{250};
    constexpr auto tick_interval = std::chrono::nanoseconds(1'000'000'000 / target_tps);
    auto next_tick = std::chrono::steady_clock::now();

    while (!control.stopping())
    {
        std::optional<diagnostics::performance::scope> combat_profile{};
        combat_profile.emplace(diagnostics::performance::zone::combat_loop);
        if (game::local_player().valid() && app::context().overlay.combat_input_ready() &&
            !app::context().menu.is_open())
        {
            if (shared_ballistics_requested())
            {
                VESTA_PERF_SCOPE(ballistics_tick);
                simulation::ballistics().tick();
            }
            {
                VESTA_PERF_SCOPE(aim_tick);
                features::aimbot::aim().tick();
            }
            {
                VESTA_PERF_SCOPE(grenade_aim_tick);
                features::aimbot::grenade_aim().tick();
            }
        }
        else
        {
            features::aimbot::aim().reset();
        }

        combat_profile.reset();
        next_tick += tick_interval;

        const auto now = std::chrono::steady_clock::now();
        if (next_tick <= now)
        {
            next_tick = now + tick_interval;
        }

        control.sleep_until(next_tick);
    }
}

void movement(control control)
{
    // Movement tricks are command-timing features. Keep their memory reads and
    // input independent from the much heavier aim/ballistics pass.
    constexpr auto active_interval = std::chrono::milliseconds(1);
    constexpr auto idle_interval = std::chrono::milliseconds(4);
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    while (!control.stopping())
    {
        {
            VESTA_PERF_SCOPE(movement_loop);
            {
                VESTA_PERF_SCOPE(bhop_tick);
                features::misc::bhop().tick();
            }
            {
                VESTA_PERF_SCOPE(auto_stop_tick);
                features::misc::auto_stop().tick();
            }
            features::misc::walk_bot().tick();
        }
        const auto snapshot = config::get_runtime_snapshot();
        const auto active =
            snapshot && (snapshot->general.m_bunny_hop.enabled || snapshot->general.m_edge_jump.enabled ||
                         snapshot->general.m_auto_stop.enabled || snapshot->general.m_walk_bot.enabled ||
                         features::misc::auto_stop().active());
        control.sleep_for(active ? active_interval : idle_interval);
    }
}

void nade_helper(control control)
{
    constexpr auto active_interval = std::chrono::milliseconds(1);
    constexpr auto idle_interval = std::chrono::milliseconds(8);
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    while (!control.stopping())
    {
        {
            VESTA_PERF_SCOPE(nade_helper_tick);
            features::misc::nade_helper().tick();
        }
        const auto snapshot = config::get_runtime_snapshot();
        control.sleep_for(snapshot && snapshot->general.m_nade_helper.enabled ? active_interval : idle_interval);
    }
}

void seed_trigger(control control)
{
    auto latency_priority = false;
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    THREAD_POWER_THROTTLING_STATE power_state{THREAD_POWER_THROTTLING_CURRENT_VERSION,
                                              THREAD_POWER_THROTTLING_EXECUTION_SPEED, 0};
    (void)::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &power_state, sizeof(power_state));

    const platform::windows::precise_worker_wait wait(control);

    while (!control.stopping())
    {
        auto& runtime = features::trigger::seed_trigger();
        const auto snapshot = config::get_runtime_snapshot();
        bool can_run{};
        bool configured{};
        bool latency_requested{};
        {
            VESTA_PERF_SCOPE(seed_trigger_tick);
            can_run = app::context().overlay.combat_input_ready() && !app::context().menu.is_open();
            configured = snapshot && snapshot->combat.seed_trigger_configured();
            if (!can_run)
                runtime.reset();

            if (can_run && configured)
            {
                const auto controller =
                    app::context().process.load<std::uintptr_t>(app::context().addresses.local_player_controller);
                const auto binding = game::resolve_local_pawn(controller);
                const auto tick = simulation::read_seed_tick(controller, binding.pawn);
                const auto observed_at = std::chrono::steady_clock::now();
                runtime.sync_phase(tick, observed_at);
            }
            latency_requested = can_run ? runtime.poll() : false;
            if (latency_requested != latency_priority)
            {
                latency_priority = latency_requested;
                ::SetThreadPriority(::GetCurrentThread(),
                                    latency_priority ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL);
            }
        }

        wait.wait(simulation::seed_schedule::poll_interval(can_run && runtime.input_pending(),
                                                           can_run && configured && latency_requested));
    }
}

} // namespace app::workers
