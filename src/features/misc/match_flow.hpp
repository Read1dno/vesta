#pragma once

#include <features/misc/match_flow_logic.hpp>
#include <game/panorama/panorama.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>

namespace features::misc
{
// What the game rules say about the match right now (cached for a quarter second; any thread).
struct game_rules_state
{
    bool valid{};
    bool freeze{};
    bool warmup{};
    bool match_ended{};
    int phase{};
    int rounds{};
};
[[nodiscard]] game_rules_state read_game_rules();

// Match automation for the walk bot: closes leftover popups, starts matchmaking in the chosen mode, accepts the match,
// picks the team, and leaves the server once the score and time targets are reached. It drives the game's own UI
// through Panorama: it finds the buttons by id, clicks them with the system cursor and puts the cursor back.
class match_flow_t
{
public:
    match_flow_t();
    ~match_flow_t();
    match_flow_t(const match_flow_t&) = delete;
    match_flow_t& operator=(const match_flow_t&) = delete;

    void tick(); // game worker
    // The automation is working the UI (a click in progress, or the screen is one it handles): movement and the view
    // stay untouched meanwhile.
    [[nodiscard]] bool holds_input() const noexcept { return m_holds_input.load(std::memory_order_acquire); }

private:
    using clock = std::chrono::steady_clock;
    using action = match_logic::action;

    struct click
    {
        enum class phase : std::uint8_t
        {
            idle,
            moved,
            pressed,
        };
        phase step{phase::idle};
        POINT saved{};
        bool saved_valid{};
    };

    [[nodiscard]] match_logic::observation observe(const game::panorama::runtime& runtime, clock::time_point now);
    [[nodiscard]] bool perform(action next, const game::panorama::runtime& runtime, HWND window);
    [[nodiscard]] bool start_click(const game::panorama::runtime& runtime, std::uintptr_t panel, HWND window);
    void advance_click();
    void tap_escape();
    [[nodiscard]] std::uintptr_t top_popup(const game::panorama::runtime& runtime);
    void release_escape(bool now_anyway);

    std::unique_ptr<game::panorama::resolver> m_resolver;
    game::panorama::catalog m_catalog;
    match_logic::memory m_memory{};
    click m_click{};
    clock::time_point m_next_action{};
    clock::time_point m_next_observe{};
    clock::time_point m_joined_at{};
    clock::time_point m_search_started{};
    clock::time_point m_searching_since{};
    clock::time_point m_last_action{};
    float m_seconds_limit{};
    bool m_in_game{};
    std::atomic<bool> m_holds_input{};
    std::uintptr_t m_popup_close{}; // the close button of the popup on top, found by the last look
    bool m_popup_open{};
    bool m_escape_held{};
    clock::time_point m_escape_release{};
    std::mt19937 m_random{std::random_device{}()};
};

match_flow_t& match_flow();
} // namespace features::misc
