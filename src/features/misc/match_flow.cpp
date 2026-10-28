#include <stdafx.hpp>
#include <features/misc/match_flow.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <diagnostics/journal.hpp>
#include <foundation/identity.hpp>
#include <game/memory/bound_services.hpp>
#include <game/memory/remote_pointer.hpp>
#include <game/state/level.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <render/presentation.hpp>

#include <array>

namespace features::misc
{
namespace
{
using clock = std::chrono::steady_clock;

// Panel ids of the game's UI, from its Panorama layouts and scripts (panorama/layout, panorama/scripts) and the client's
// generic popup.
constexpr std::string_view k_play_button{"MainMenuNavBarPlay"};
constexpr std::string_view k_matchmaking_tab{"Play-official"};
constexpr std::string_view k_mode_container{"GameModeSelectionRadios"};
constexpr std::string_view k_start_button{"StartMatchBtn"};
constexpr std::string_view k_cancel_search{"PartyCancelBtn"};
constexpr std::string_view k_accept_button{"AcceptMatchBtn"};
constexpr std::string_view k_accept_popup{"id-accept-match"};
constexpr std::string_view k_team_t{"BtnSelectTeam-TERRORIST"};
constexpr std::string_view k_team_ct{"BtnSelectTeam-CT"};
constexpr std::string_view k_exit_game{"MainMenuNavBarExitGame"};
constexpr std::string_view k_switch_team{"MainMenuNavBarSwitchTeams"};
// The pause menu is the main menu in pause mode: mainmenu.js adds this class on CSGOShowPauseMenu and removes it on
// CSGOHidePauseMenu. The exit button alone is no signal, it stays laid out (and "visible") while the menu is closed.
constexpr std::string_view k_menu_root{"MainMenu"}; // the class sits here (verified live), not on CSGOMainMenu
constexpr std::string_view k_pause_mode{"MainMenuRootPanel--PauseMenuMode"};
constexpr std::string_view k_popup_button{"Button0"};
constexpr std::string_view k_popup_buttons{"ButtonContainer"};
// Popups stack inside the PopupManager (later children on top). Only the top one takes input, so it is the one
// handled; a click on a button of one below lands on the one on top (the weekly drop over a generic popup).
constexpr std::string_view k_popup_manager{"PopupManager"};
// Close or acknowledge buttons of the popups that show up on their own, from their layouts (Escape does not close all
// of them: the weekly drop needs its CLOSE button). The generic popup's first button comes last.
constexpr std::array<std::string_view, 5> k_popup_closers{"PopupRankUpRedemptionStoreClose", "id-close-button",
                                                          "AcknowledgeItemBtn", "InspectCloseBtn", "Button0"};
constexpr std::string_view k_end_of_match{"EndOfMatch"};
constexpr int k_match_ended_phase = 5; // GAMEPHASE_MATCH_ENDED

[[nodiscard]] std::vector<std::string> watched_ids()
{
    std::vector<std::string> ids{
        std::string{k_play_button},   std::string{k_matchmaking_tab}, std::string{k_start_button},
        std::string{k_cancel_search}, std::string{k_accept_button},   std::string{k_team_t},
        std::string{k_team_ct},       std::string{k_exit_game},       std::string{k_switch_team},
        std::string{k_menu_root},     std::string{k_popup_button},    std::string{k_end_of_match},
        std::string{k_popup_manager}};
    for (const auto closer : k_popup_closers)
        if (std::ranges::find(ids, closer) == ids.end())
            ids.emplace_back(closer);
    for (int mode = 0; mode < static_cast<int>(match_logic::game_mode::count); ++mode)
        ids.emplace_back(match_logic::mode_panel_id(static_cast<match_logic::game_mode>(mode)));
    return ids;
}

struct rules_cache
{
    std::mutex mutex{};
    game_rules_state state{};
    clock::time_point next{};
    std::uintptr_t rules{};
};

[[nodiscard]] rules_cache& rules()
{
    static rules_cache value{};
    return value;
}
} // namespace

game_rules_state read_game_rules()
{
    auto& cache = rules();
    std::scoped_lock lock(cache.mutex);
    const auto now = clock::now();
    if (now < cache.next)
        return cache.state;
    cache.next = now + std::chrono::milliseconds(250);
    const auto& process = game::bound_memory().process;
    if (!cache.rules || !process.load<std::uintptr_t>(cache.rules))
    {
        cache.rules = 0;
        const auto entities = game::entity_index().all();
        for (const auto& entity : *entities)
        {
            if (entity.schema_id != "C_CSGameRulesProxy"_id)
                continue;
            cache.rules = process.load<std::uintptr_t>(entity.ptr + SCHEMA("C_CSGameRulesProxy", "m_pGameRules"_id));
            if (cache.rules)
                break;
        }
    }
    game_rules_state state{};
    if (cache.rules)
    {
        state.valid = true;
        state.freeze = process.load<bool>(cache.rules + SCHEMA("C_CSGameRules", "m_bFreezePeriod"_id));
        state.warmup = process.load<bool>(cache.rules + SCHEMA("C_CSGameRules", "m_bWarmupPeriod"_id));
        state.phase = process.load<std::int32_t>(cache.rules + SCHEMA("C_CSGameRules", "m_gamePhase"_id));
        state.rounds = process.load<std::int32_t>(cache.rules + SCHEMA("C_CSGameRules", "m_totalRoundsPlayed"_id));
        state.match_ended = state.phase == k_match_ended_phase;
    }
    cache.state = state;
    return state;
}

match_flow_t::match_flow_t()
    : m_resolver(std::make_unique<game::panorama::resolver>())
    , m_catalog(watched_ids())
{
}

match_flow_t::~match_flow_t() = default;

match_flow_t& match_flow()
{
    static match_flow_t value{};
    return value;
}

match_logic::observation match_flow_t::observe(const game::panorama::runtime& runtime, const clock::time_point now)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    match_logic::observation seen{};
    const auto level = game::level().name();
    // The controller straight from the game: on the team select there is no pawn yet, and the local player state
    // (which needs one) reads as "no controller", which hid the team select from the automation.
    const auto& process = game::bound_memory().process;
    const auto controller = process.load<std::uintptr_t>(game::bound_addresses().local_player_controller);
    seen.in_game = level && !level->empty() && controller != 0;
    if (seen.in_game != m_in_game)
    {
        m_in_game = seen.in_game;
        m_search_started = {};
        if (seen.in_game)
        {
            // The time limit for this server, with a random extra so leaving does not happen on the same second.
            m_joined_at = now;
            const auto spread = std::clamp(cfg.disconnect_spread, 0, 100) / 100.0f;
            m_seconds_limit = static_cast<float>(cfg.disconnect_minutes) * 60.0f *
                              (1.0f + std::uniform_real_distribution<float>(0.0f, spread)(m_random));
        }
    }
    seen.seconds_on_server = seen.in_game ? std::chrono::duration<float>(now - m_joined_at).count() : 0.0f;
    if (seen.in_game)
    {
        seen.score = process.load<std::int32_t>(controller + SCHEMA("CCSPlayerController", "m_iScore"_id));
        seen.match_ended = read_game_rules().match_ended;
    }

    // Only popups with a known close button are handled: one without (the connecting spinner) is waited out, an
    // Escape there could cancel joining the match. Any popup on top still keeps the walk bot's hands off.
    m_popup_close = 0;
    m_popup_open = top_popup(runtime) != 0;
    seen.popup = m_popup_open && m_popup_close != 0;
    seen.main_menu = m_catalog.visible(runtime, k_play_button) != 0;
    const auto start = m_catalog.visible(runtime, k_start_button);
    seen.play_menu = start || m_catalog.visible(runtime, k_cancel_search);
    // Searching: START in the play menu carries "pressed" (mainmenu_play.js adds it on the press and takes it away
    // when the lobby reports no search), or it is gone while the cancel button stands in its place. The status in
    // the friends sidebar is no signal: after leaving a match it keeps its last "searching" state, which left the
    // automation waiting in the main menu until PLAY was pressed by hand.
    const bool just_pressed =
        m_search_started != clock::time_point{} && now - m_search_started < std::chrono::seconds(4);
    const bool start_pressed = start && game::panorama::panel_has_class_strict(start, runtime, "pressed");
    seen.searching = just_pressed || (seen.play_menu && (!start || start_pressed));
    // A search that runs this long without a match is stuck (or a stale "pressed"): cancel and start over.
    if (!seen.searching || just_pressed)
        m_searching_since = {};
    else if (m_searching_since == clock::time_point{})
        m_searching_since = now;
    seen.search_stuck = m_searching_since != clock::time_point{} && now - m_searching_since > std::chrono::minutes(10);
    seen.start_visible = start && !seen.searching;
    seen.accept_visible = m_catalog.visible(runtime, k_accept_button, k_accept_popup) != 0;
    // The HUD (and its team select) keeps its panels laid out even while it is not drawn, so the buttons alone do not
    // tell: the team select is up when the player is in a game without a team yet.
    const auto team =
        seen.in_game ? process.load<std::int32_t>(controller + SCHEMA("C_BaseEntity", "m_iTeamNum"_id)) : 0;
    seen.team = team;
    seen.team_select =
        seen.in_game && team < 2 && (m_catalog.visible(runtime, k_team_t) || m_catalog.visible(runtime, k_team_ct));
    if (seen.in_game)
        for (const auto root : m_catalog.panels(runtime, k_menu_root))
            seen.pause_menu = seen.pause_menu || game::panorama::panel_has_class_strict(root, runtime, k_pause_mode);
    seen.match_ended = seen.match_ended || (seen.in_game && m_catalog.visible(runtime, k_end_of_match) != 0);
    return seen;
}

// The popup on top of the PopupManager's stack and, when it has one of the known close buttons on screen, that button.
std::uintptr_t match_flow_t::top_popup(const game::panorama::runtime& runtime)
{
    const auto& process = game::bound_memory().process;
    for (const auto manager : m_catalog.panels(runtime, k_popup_manager))
    {
        const auto count = process.load<std::int32_t>(manager + 0x28);
        const auto children = process.load<std::uintptr_t>(manager + 0x30);
        if (count <= 0 || count > 256 || !game::is_user_pointer(children))
            continue;
        std::vector<std::uintptr_t> stack(static_cast<std::size_t>(count));
        if (!process.copy(children, stack.data(), stack.size() * sizeof(std::uintptr_t)))
            continue;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        {
            const auto shape = game::panorama::inspect_geometry(runtime, *it);
            if (!shape || shape->width <= 0.0f || shape->height <= 0.0f)
                continue;
            const auto inside = [&](std::uintptr_t panel)
            {
                for (int depth = 0; depth < 24 && game::is_user_pointer(panel); ++depth)
                {
                    if (panel == *it)
                        return true;
                    panel = process.load<game::panorama::panel_header>(panel).parent;
                }
                return false;
            };
            for (const auto closer : k_popup_closers)
                for (const auto button : m_catalog.panels(runtime, closer))
                    if (!m_popup_close && inside(button))
                        if (const auto at = game::panorama::inspect_geometry(runtime, button);
                            at && at->width > 0.0f && at->height > 0.0f)
                            m_popup_close = button;
            return *it;
        }
    }
    return 0;
}

bool match_flow_t::start_click(const game::panorama::runtime& runtime, const std::uintptr_t panel, const HWND window)
{
    if (!panel)
        return false;
    // A slightly different spot inside the button every time, like a hand would.
    std::uniform_real_distribution<float> spot(0.35f, 0.65f);
    const auto point = game::panorama::screen_point(runtime, panel, window, spot(m_random), spot(m_random));
    if (!point)
        return false;
    POINT saved{};
    m_click.saved_valid = ::GetCursorPos(&saved) != FALSE;
    m_click.saved = saved;
    if (!::SetCursorPos(point->x, point->y))
        return false;
    m_click.step = click::phase::moved;
    return true;
}

void match_flow_t::advance_click()
{
    auto& gateway = platform::windows::bound_input();
    switch (m_click.step)
    {
        case click::phase::moved:
            if (gateway.pointer(0, 0, platform::windows::pointer_action::primary_down))
                m_click.step = click::phase::pressed;
            else
                m_click = {};
            return;
        case click::phase::pressed:
            if (!gateway.pointer(0, 0, platform::windows::pointer_action::primary_up))
                return; // a down edge is always released
            if (m_click.saved_valid)
                ::SetCursorPos(m_click.saved.x, m_click.saved.y);
            m_click = {};
            return;
        case click::phase::idle:
            return;
    }
}

// Escape is held for a few frames like a finger would: a press and release in the same instant can fall between two
// game frames and never be seen.
void match_flow_t::tap_escape()
{
    if (platform::windows::bound_input().key(VK_ESCAPE, true))
    {
        m_escape_held = true;
        m_escape_release =
            clock::now() + std::chrono::milliseconds(std::uniform_int_distribution<int>(45, 85)(m_random));
    }
}

void match_flow_t::release_escape(const bool now_anyway)
{
    if (m_escape_held && (now_anyway || clock::now() >= m_escape_release))
    {
        (void)platform::windows::bound_input().key(VK_ESCAPE, false);
        m_escape_held = false;
    }
}

bool match_flow_t::perform(const action next, const game::panorama::runtime& runtime, const HWND window)
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto& cfg = snapshot->general.m_walk_bot;
    const auto click_id = [&](const std::string_view id, const std::string_view ancestor = {})
    { return start_click(runtime, m_catalog.visible(runtime, id, ancestor), window); };
    switch (next)
    {
        case action::dismiss_popup:
            // The popup's own close button (Escape is ignored by some, and cancels others).
            return m_popup_close && start_click(runtime, m_popup_close, window);
        case action::open_pause_menu:
            tap_escape();
            return true;
        case action::confirm_popup:
            if (!m_popup_close)
                return false;
            return start_click(runtime, m_popup_close, window);
        case action::confirm_exit:
            return click_id(k_popup_button, k_popup_buttons);
        case action::open_play:
            return click_id(k_play_button);
        case action::open_matchmaking:
            return click_id(k_matchmaking_tab);
        case action::pick_mode:
            return click_id(match_logic::mode_panel_id(static_cast<match_logic::game_mode>(cfg.game_mode)),
                            k_mode_container);
        case action::start_search:
            if (!click_id(k_start_button))
                return false;
            m_search_started = clock::now();
            return true;
        case action::cancel_search:
            m_searching_since = {};
            return click_id(k_cancel_search);
        case action::accept_match:
            return click_id(k_accept_button, k_accept_popup);
        case action::pick_team:
            return click_id(cfg.team == 1 ? k_team_ct : k_team_t);
        case action::exit_to_menu:
            return click_id(k_exit_game);
        case action::open_team_menu:
            return click_id(k_switch_team);
        case action::none:
            return true;
    }
    return false;
}

void match_flow_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    // The screen is watched whenever the walk bot runs (it must not walk or turn the view under a game menu); buttons
    // are only pressed when rejoin or disconnect is on.
    const bool acting = cfg.enabled && (cfg.auto_rejoin || cfg.auto_disconnect);
    release_escape(!cfg.enabled);
    if (!cfg.enabled)
    {
        if (m_click.step != click::phase::idle)
            advance_click();
        m_holds_input.store(false, std::memory_order_release);
        m_memory = {};
        return;
    }
    // A click in progress is finished first, one edge per tick.
    if (m_click.step != click::phase::idle)
    {
        advance_click();
        m_holds_input.store(true, std::memory_order_release);
        return;
    }

    const auto& runtime = m_resolver->get();
    if (!runtime.engine)
        return;
    (void)m_catalog.step(runtime);
    if (!m_catalog.pass_completed())
        return;

    const auto now = clock::now();
    // Looking at the screen costs thousands of reads from the game; four times a second is plenty for menus.
    if (now < m_next_observe)
        return;
    // Four looks a second at rest; ten while the automation is going through menus.
    m_next_observe = now + std::chrono::milliseconds(now - m_last_action < std::chrono::seconds(3) ? 100 : 250);
    const auto seen = observe(runtime, now);
    // Game screens over the world (menus, popups, team select, the end of the match): the walk bot keeps its keys and
    // the view to itself meanwhile, and so it does for a moment after every automated action.
    m_holds_input.store(seen.popup || m_popup_open || seen.team_select || seen.pause_menu || seen.match_ended ||
                            !seen.in_game || now - m_last_action < std::chrono::milliseconds(1500),
                        std::memory_order_release);

    if (!acting || now < m_next_action || render::menu_open())
        return;
    const auto window = ::FindWindowW(nullptr, L"Counter-Strike 2");
    if (!window || game::panorama::active_root_window() != window)
        return; // only ever act on the game in front

    match_logic::settings rules{};
    rules.rejoin = cfg.auto_rejoin;
    rules.disconnect = cfg.auto_disconnect;
    rules.mode = static_cast<match_logic::game_mode>(std::clamp(cfg.game_mode, 0, 4));
    rules.score_limit = cfg.disconnect_score;
    rules.seconds_limit = m_seconds_limit;
    rules.team = cfg.team == 1 ? 3 : 2;
    const auto next = match_logic::decide(seen, rules, m_memory);
    if (next == action::none)
        return;
    const bool done = perform(next, runtime, window);
    diagnostics::emit(done ? diagnostics::event::matchflow_action : diagnostics::event::matchflow_missing,
                      done ? diagnostics::severity::information : diagnostics::severity::warning,
                      diagnostics::code_domain::mode, static_cast<std::uint32_t>(next));
    if (!done)
    {
        // The button was not on screen after all: look again on the next pass rather than pressing blindly.
        m_memory.matchmaking_pressed = m_memory.matchmaking_pressed && next != action::open_matchmaking;
        m_memory.mode_pressed = m_memory.mode_pressed && next != action::pick_mode;
    }
    m_last_action = now;
    // A pause before the next step. Going through the menus (PLAY, the matchmaking tab, the mode, START; Escape and
    // "switch team") is one quick sequence of clicks; the rest gets a longer human pause.
    const bool chain = next == action::open_play || next == action::open_matchmaking || next == action::pick_mode ||
                       next == action::open_pause_menu || next == action::dismiss_popup;
    // The team select fades in for half a second after "switch team".
    const auto pause = next == action::open_team_menu ? std::uniform_int_distribution<int>(650, 900)(m_random)
                       : chain                        ? std::uniform_int_distribution<int>(160, 320)(m_random)
                                                      : std::uniform_int_distribution<int>(700, 1500)(m_random);
    m_next_action = now + std::chrono::milliseconds(pause);
}
} // namespace features::misc
