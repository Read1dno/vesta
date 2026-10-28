#pragma once

// What the match automation does next, decided from what is on screen. One action per decision; the caller spaces
// actions out with a human delay and reports back what it saw after each one. Free of game and Windows state so the
// tests can walk it through whole scenarios.

#include <cstdint>

namespace features::misc::match_logic
{
enum class game_mode : int
{
    deathmatch = 0,
    casual,
    competitive,
    wingman,
    premier,
    count
};

// Panel id of a mode's radio button in the play menu (inside GameModeSelectionRadios).
[[nodiscard]] constexpr const char* mode_panel_id(const game_mode mode) noexcept
{
    switch (mode)
    {
        case game_mode::casual:
            return "casual";
        case game_mode::competitive:
            return "competitive";
        case game_mode::wingman:
            return "scrimcomp2v2";
        case game_mode::premier:
            return "premier";
        case game_mode::deathmatch:
        default:
            return "deathmatch";
    }
}

// Modes whose match has to be accepted when found.
[[nodiscard]] constexpr bool mode_needs_accept(const game_mode mode) noexcept
{
    return mode == game_mode::competitive || mode == game_mode::wingman || mode == game_mode::premier;
}

enum class action : std::uint8_t
{
    none,
    dismiss_popup,    // a popup on top (kick, lost connection, weekly drop, ...): its close button
    confirm_popup,    // it is still there after three presses: press once more after a pause
    open_play,        // main menu: PLAY
    open_matchmaking, // play menu: the matchmaking tab
    pick_mode,        // play menu: the mode radio
    start_search,     // play menu: START
    cancel_search,    // play menu: a search that found nothing for too long is cancelled and started again
    accept_match,     // match found: ACCEPT
    pick_team,        // team select: T or CT
    open_team_menu,   // pause menu: "switch team" (the server put the player on the other side)
    open_pause_menu,  // in game, leaving: Escape
    exit_to_menu,     // pause menu: exit to main menu
    confirm_exit,     // exit confirmation: YES
};

// What is on screen right now.
struct observation
{
    bool in_game{};       // a map is loaded and the local player exists
    bool popup{};         // a generic popup is visible
    bool main_menu{};     // the main menu's PLAY button is visible
    bool play_menu{};     // the play menu is open (START or the cancel-search button is visible)
    bool start_visible{}; // START can be pressed
    bool searching{};     // matchmaking is running
    bool search_stuck{};  // it has been running far too long
    bool accept_visible{};
    bool team_select{};
    int team{};         // 2 T, 3 CT, anything else none yet
    bool pause_menu{};  // in game, the pause menu is open (the game's own pause-mode flag)
    bool match_ended{}; // the end-of-match screens
    int score{};
    float seconds_on_server{};
};

struct settings
{
    bool rejoin{};
    bool disconnect{};
    game_mode mode{game_mode::deathmatch};
    int score_limit{450};
    float seconds_limit{240.0f}; // already randomised by the caller for this server
    int team{};                  // the side wanted: 2 T, 3 CT, 0 no preference
};

// What the automation remembers between decisions. Reset by the caller when the game state changes completely.
struct memory
{
    bool matchmaking_pressed{};
    bool mode_pressed{};
    bool leaving{}; // auto disconnect has started
    bool exit_pressed{};
    int escapes{};      // Escapes sent at the current popup
    int switch_tries{}; // attempts on this server to get onto the wanted side
    bool switch_open{}; // the team menu was opened from the pause menu: pick the side next
    bool was_in_game{};
};

[[nodiscard]] inline action decide(const observation& seen, const settings& rules, memory& state)
{
    // Entering or leaving a game starts every sequence over.
    if (seen.in_game != state.was_in_game)
    {
        state = {};
        state.was_in_game = seen.in_game;
    }

    // Popups are handled outside a game only: in a game Escape would open or close the pause menu instead, and the
    // only popup of ours there is the exit confirmation.
    if (seen.popup && !seen.in_game)
    {
        // Its close button; one that stays after three presses gets one more after a pause, then the cycle repeats.
        if (state.escapes < 3)
        {
            ++state.escapes;
            return action::dismiss_popup;
        }
        state.escapes = 0;
        return action::confirm_popup;
    }
    if (!seen.popup)
        state.escapes = 0;

    if (seen.in_game)
    {
        if (seen.team_select)
            return rules.rejoin || rules.disconnect ? action::pick_team : action::none;
        if (seen.match_ended)
            return action::none;
        // Official servers put the player on a side themselves (there is no team select on joining): when it is not
        // the wanted one, switch through the pause menu, twice at most per server (the other side may be full).
        if (state.switch_open)
        {
            state.switch_open = false;
            ++state.switch_tries;
            return action::pick_team;
        }
        if (rules.rejoin && !state.leaving && (rules.team == 2 || rules.team == 3) &&
            (seen.team == 2 || seen.team == 3) && seen.team != rules.team && state.switch_tries < 2)
        {
            if (!seen.pause_menu)
                return action::open_pause_menu;
            state.switch_open = true;
            return action::open_team_menu;
        }
        // A menu the player opened is theirs: the automation never closes it.
        if (!rules.disconnect)
            return action::none;
        if (!state.leaving && seen.score >= rules.score_limit && seen.seconds_on_server >= rules.seconds_limit)
            state.leaving = true;
        if (!state.leaving)
            return action::none;
        if (seen.popup && state.exit_pressed)
            return action::confirm_exit;
        if (!seen.pause_menu)
            return action::open_pause_menu;
        state.exit_pressed = true;
        return action::exit_to_menu;
    }

    if (!rules.rejoin)
        return action::none;
    if (seen.accept_visible)
        return action::accept_match;
    if (seen.searching)
    {
        if (!seen.search_stuck)
            return action::none;
        state.mode_pressed = false; // pick the mode again on the way back
        return action::cancel_search;
    }
    if (!seen.play_menu)
    {
        state.matchmaking_pressed = false;
        state.mode_pressed = false;
        return seen.main_menu ? action::open_play : action::none;
    }
    if (!state.matchmaking_pressed)
    {
        state.matchmaking_pressed = true;
        return action::open_matchmaking;
    }
    if (!state.mode_pressed)
    {
        state.mode_pressed = true;
        return action::pick_mode;
    }
    return seen.start_visible ? action::start_search : action::none;
}
} // namespace features::misc::match_logic
