#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <features/misc/match_flow.hpp>
#include <features/misc/match_flow_logic.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/journal.hpp>
#include <features/aimbot/aimbot.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/level.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <platform/windows/storage.hpp>
#include <simulation/collision.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace features::misc
{
using namespace walk_detail;

namespace
{
constexpr float k_reload_seconds = 2.6f; // most rifles and pistols; the check adds its own margin
constexpr float k_eye = 64.0f;

[[nodiscard]] std::filesystem::path recordings_folder(const std::string& map, const int team)
{
    return platform::windows::runtime_storage::paths().root / L"walkbot" / std::filesystem::u8path(map) /
           (team == 3 ? L"ct" : L"t");
}

[[nodiscard]] std::string current_map()
{
    const auto level = game::level().name();
    if (!level || level->empty())
        return {};
    auto map = *level;
    if (const auto slash = map.find_last_of("/\\"); slash != std::string::npos)
        map = map.substr(slash + 1);
    if (const auto dot = map.find_last_of('.'); dot != std::string::npos)
        map.resize(dot);
    return map;
}
} // namespace

// Reason codes (journal walkbot.idle): 1 no navigation mesh, 2 game not focused or a Vesta/chat input open, 3 local
// pawn not readable, 4 movement owned by Auto Stop or the lineup helper, 5 no route from here, 6 a game menu or
// screen is open, 7 the player moves on their own, 8 recording, 9 fighting an enemy in sight.
void walk_bot_t::note_idle(const std::uint32_t reason)
{
    if (m_idle_note.allow())
        diagnostics::emit(diagnostics::event::walkbot_idle, diagnostics::severity::information,
                          diagnostics::code_domain::mode, reason);
}

namespace
{
using walk_settings = config::general_profile::walk_bot;

// The preset slot for the mode being played: game_type/game_mode, with premier told apart from competitive by the mode
// the match automation queued for.
[[nodiscard]] int buy_mode_now(const walk_settings& cfg)
{
    const auto game_type = game::variables().get<std::int32_t>(CONVAR("game_type"_id));
    const auto game_mode = game::variables().get<std::int32_t>(CONVAR("game_mode"_id));
    if (game_type == 1 && game_mode == 2)
        return walk_settings::buy_deathmatch;
    if (game_type == 0 && game_mode == 0)
        return walk_settings::buy_casual;
    if (game_type == 0 && game_mode == 2)
        return walk_settings::buy_wingman;
    if (game_type == 0 && game_mode == 1)
        return cfg.game_mode == static_cast<int>(match_logic::game_mode::premier) ? walk_settings::buy_premier
                                                                                  : walk_settings::buy_competitive;
    return walk_settings::buy_default;
}

[[nodiscard]] const walk_settings::buy_preset& buy_preset_for(const walk_settings& cfg, const int mode)
{
    const auto& own = cfg.buy_presets[static_cast<std::size_t>(mode)];
    return mode != walk_settings::buy_default && own.custom ? own : cfg.buy_presets[walk_settings::buy_default];
}
} // namespace

bool walk_bot_t::buy_holds_movement(const player_state& player) const
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!cfg.auto_buy || buy_mode_now(cfg) != walk_settings::buy_deathmatch)
        return false;
    // Keys still to type, or this life undecided while the buy window is open.
    return !m_buy_queue.empty() || m_tapped_key != 0 ||
           (m_bought_life != m_life &&
            walk_logic::deathmatch_should_buy(player.immune, player.moved, player.has_main_weapon));
}

// In round modes once per round in the buy zone; in deathmatch right after a respawn without a main weapon, while the
// protection lasts and before the first step (the walk bot stands still meanwhile). The items of the mode's preset
// that apply to the side and the money are typed like a player would: the menu key, then (once the game reports the
// menu open) each item's digits, then the menu key again.
void walk_bot_t::auto_buy(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!m_buy_queue.empty())
    {
        // Deathmatch takes the menu away once the protection ends or the player moved: the rest is pointless.
        if (m_buy_deathmatch && (!player.immune || player.moved))
        {
            m_buy_queue.clear();
            return;
        }
        if (m_buy_waiting_menu)
        {
            if (player.buy_menu_open)
            {
                m_buy_waiting_menu = false;
                m_next_buy_key = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(
                                           m_buy_deathmatch ? 110 : 180, m_buy_deathmatch ? 170 : 280)(m_random));
            }
            else if (now - m_buy_first_key < std::chrono::milliseconds(1000))
                return;
            else
            {
                m_buy_queue.clear(); // the menu never opened: typing digits into the game would switch weapons
                m_buy_waiting_menu = false;
                return;
            }
        }
        if (now < m_next_buy_key || m_tapped_key)
            return;
        const bool first = m_buy_first_key == clock::time_point{};
        tap_key(m_buy_queue.front());
        m_buy_queue.erase(m_buy_queue.begin());
        if (first)
        {
            m_buy_first_key = now;
            m_buy_waiting_menu = !player.buy_menu_open;
        }
        m_next_buy_key = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(
                                   m_buy_deathmatch ? 90 : 140, m_buy_deathmatch ? 150 : 240)(m_random));
        return;
    }
    if (!cfg.auto_buy)
        return;

    const auto mode = buy_mode_now(cfg);
    const bool deathmatch = mode == walk_settings::buy_deathmatch;
    if (deathmatch)
    {
        if (m_bought_life == m_life)
            return; // decided for this life
        if (!walk_logic::deathmatch_should_buy(player.immune, player.moved, player.has_main_weapon))
        {
            // Already armed (the game handed the gun out) or the window is gone: nothing this life. While the
            // protection has not even started yet (the first ticks of a spawn), look again.
            if (player.has_main_weapon || player.moved || now - m_spawned_at > std::chrono::milliseconds(400))
                m_bought_life = m_life;
            return;
        }
        if (now - m_spawned_at < std::chrono::milliseconds(60))
            return;
        m_bought_life = m_life;
    }
    else
    {
        const auto round = read_game_rules().rounds;
        if (m_bought_round == round && m_bought_life_round == m_life)
            return;
        if (!player.in_buy_zone || now - m_spawned_at < std::chrono::milliseconds(500))
            return;
        m_bought_round = round;
        m_bought_life_round = m_life; // once per round, and per life within it (warmup respawns)
    }
    const auto& preset = buy_preset_for(cfg, mode);
    std::array<walk_logic::buy_entry, walk_settings::k_buy_items> entries{};
    const auto used = static_cast<std::size_t>(std::clamp(preset.count, 0, walk_settings::k_buy_items));
    for (std::size_t i = 0; i < used; ++i)
    {
        const auto& item = preset.items[i];
        entries[i] = {item.enabled, std::string_view{item.code.data()}, item.side, item.money_rule, item.money};
    }
    m_buy_queue = walk_logic::buy_keys(
        std::span{entries.data(), used}, player.team, player.money, preset.random_one,
        std::uniform_real_distribution<float>(0.0f, 1.0f)(m_random),
        static_cast<std::uint16_t>(std::clamp(snapshot->general.m_game_controls.buy_menu, 1, 255)));
    m_buy_deathmatch = deathmatch;
    m_buy_waiting_menu = false;
    m_buy_first_key = {};
    m_next_buy_key = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(
                               deathmatch ? 40 : 200, deathmatch ? 90 : 450)(m_random));
}

// Reload early (under half a magazine) but only when nobody can get a line of fire during the reload: every enemy is
// assumed to keep its velocity, the player to keep walking the route.
void walk_bot_t::auto_reload(const player_state& player, const path* route, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot->general.m_walk_bot.auto_reload || now < m_next_reload_check || now < m_reload_cooldown)
        return;
    m_next_reload_check = now + std::chrono::milliseconds(200);
    if (!game::rules::is_firearm(game::local_player().weapon_type()))
        return; // knife, grenades, the bomb, the taser...
    const auto weapon = game::local_player().weapon();
    const auto data = game::local_player().weapon_vdata();
    if (!weapon || !data)
        return;
    const auto& process = game::bound_memory().process;
    const auto clip = process.load<std::int32_t>(weapon + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id));
    const auto max_clip = process.load<std::int32_t>(data + SCHEMA("CBasePlayerWeaponVData", "m_iMaxClip1"_id));
    if (!walk_logic::wants_reload(clip, max_clip))
        return;
    if (recent(features::aimbot::aim().view_engaged_ticks(), now, std::chrono::milliseconds(600)))
        return; // in a fight

    std::vector<walk_logic::threat> threats;
    if (const auto players = game::world().players())
        for (const auto& other : *players)
            if (other.health > 0 && other.pawn != player.pawn && game::local_player().is_enemy(other.team))
                threats.push_back({other.origin + foundation::vec3{0.0f, 0.0f, k_eye}, other.velocity});
    const auto& world = simulation::collision();
    const auto progress = m_progress.load(std::memory_order_acquire);
    const auto speed = std::max(flat(player.velocity).length(), 0.0f);
    const auto own_eye = [&](const float t)
    {
        if (!route || route->empty())
            return player.eye;
        return point_at(*route, progress + speed * t) + foundation::vec3{0.0f, 0.0f, k_eye};
    };
    const auto visible = [&](const foundation::vec3& a, const foundation::vec3& b)
    {
        if (!world.valid())
            return a.distance(b) < 1500.0f; // no geometry: only distance protects
        return !world.trace_ray(a, b).hit;
    };
    if (!walk_logic::reload_window_safe(own_eye, threats, k_reload_seconds + 0.4f, visible))
        return;
    tap_key(static_cast<std::uint16_t>(std::clamp(snapshot->general.m_game_controls.reload, 1, 255)));
    m_reload_cooldown = now + std::chrono::milliseconds(3500);
    diagnostics::emit(diagnostics::event::walkbot_obstacle, diagnostics::severity::information,
                      diagnostics::code_domain::mode, 30u);
}

void walk_bot_t::load_recordings(const std::string& map, const int team)
{
    m_recordings_map = map;
    m_recordings_team = team;
    m_recordings.clear();
    std::error_code error;
    const auto folder = recordings_folder(map, team);
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (!entry.is_regular_file() || entry.path().extension() != L".route")
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        if (auto parsed = walk_logic::parse_recording(text.str()))
            m_recordings.push_back(std::move(*parsed));
    }
    auto shown = std::make_shared<const std::vector<walk_logic::recording>>(m_recordings);
    std::scoped_lock lock(m_route_mutex);
    m_recordings_shown = std::move(shown);
}

// The record key starts and stops a recording of the player's own walk: position every 24 units and every change of
// jump, crouch or use. Saved per map and team; the recorded-routes mode plays them back.
void walk_bot_t::record(const player_state& player, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto& gateway = platform::windows::bound_input();
    const bool key = focus_ready() && cfg.record_key > 0 && cfg.record_key < 256 &&
                     gateway.physical_key_down(static_cast<std::uint16_t>(cfg.record_key));
    const bool pressed = key && !m_record_key_was_down;
    m_record_key_was_down = key;
    static_cast<void>(now);

    if (pressed && !m_recording_active)
    {
        m_recording = {current_map(), player.team, {}};
        std::scoped_lock lock(m_route_mutex);
        m_recording_active = !m_recording.map.empty();
        return;
    }
    if (pressed && m_recording_active)
    {
        {
            std::scoped_lock lock(m_route_mutex);
            m_recording_active = false;
        }
        if (m_recording.points.size() >= 2)
        {
            std::error_code error;
            const auto folder = recordings_folder(m_recording.map, m_recording.team);
            std::filesystem::create_directories(folder, error);
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            std::ofstream out(folder / (std::to_string(stamp) + ".route"), std::ios::binary | std::ios::trunc);
            out << walk_logic::serialize(m_recording);
            diagnostics::emit(diagnostics::event::walkbot_route, diagnostics::severity::information,
                              diagnostics::code_domain::worker, static_cast<std::uint32_t>(m_recording.points.size()));
            load_recordings(m_recording.map, m_recording.team);
        }
        return;
    }
    if (!m_recording_active)
        return;

    const auto held = [&](const game::input_action which)
    {
        const auto binding = game::input_bindings().resolve(which);
        return binding.device == game::input_device::keyboard && binding.virtual_key &&
               gateway.physical_key_down(binding.virtual_key);
    };
    walk_logic::recorded_point next{player.origin, 0};
    if (held(game::input_action::jump) || (!player.on_ground && player.velocity.z > 120.0f))
        next.flags |= walk_logic::recorded_point::jump;
    if (held(game::input_action::duck))
        next.flags |= walk_logic::recorded_point::duck;
    const auto use_key = snapshot->general.m_game_controls.use;
    if (use_key > 0 && use_key < 256 && gateway.physical_key_down(static_cast<std::uint16_t>(use_key)))
        next.flags |= walk_logic::recorded_point::use;
    if (m_recording.points.empty() || walk_logic::should_record(m_recording.points.back(), next))
        m_recording.points.push_back(next);
}
} // namespace features::misc
