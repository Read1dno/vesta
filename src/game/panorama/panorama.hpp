#pragma once

// Access to the game's Panorama UI from outside the process: the engine, its panel registry, a panel's id, CSS
// classes and on-screen rectangle. Shared by Auto Accept and the match automation.

#include <windows.h>

#include <platform/windows/process.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::panorama
{
// The registry holds 131 072 slots in current CS2 (it grows with the session).
inline constexpr std::uint32_t k_scan_chunk{1024};
inline constexpr std::uint32_t k_max_capacity{1u << 20};

struct panel_header
{
    std::uintptr_t vtable{};
    std::uintptr_t client{};
    std::uintptr_t id{};
    std::uintptr_t parent{};
};

struct registry_entry
{
    std::array<std::byte, 0x10> metadata{};
    std::uintptr_t panel{};
    std::uintptr_t generation{};
};

static_assert(sizeof(registry_entry) == 0x20);

struct panel_class_storage
{
    std::uint32_t count{};
    std::uint32_t data{};
    [[nodiscard]] explicit operator bool() const { return count && data; }
};

struct runtime
{
    std::uintptr_t engine{};
    std::uintptr_t ui_panel_vtable{};
    std::uintptr_t button_vtable{};
    std::uintptr_t class_symbol_table{};
    std::uint32_t registry_offset{};
    bool attempted{};
    panel_class_storage class_storage{};
};

struct geometry
{
    float x{};
    float y{};
    float width{};
    float height{};
    float root_width{};
    float root_height{};
};

using geometry_failure = std::uint32_t;

// Finds the Panorama engine, its panel registry and the class symbol table. Retried every two seconds until it
// succeeds, then cached for the rest of the game session.
class resolver
{
public:
    const runtime& get();

private:
    runtime m_runtime{};
    std::chrono::steady_clock::time_point m_next_retry{};
    std::uint32_t m_reported_stages{0xffffffffu};
};

// Root window of the thread with keyboard focus (what the player is looking at).
[[nodiscard]] HWND active_root_window();
[[nodiscard]] std::uint32_t decode_registry_offset(const platform::windows::process_session& process,
                                                   std::uintptr_t method);
[[nodiscard]] std::uintptr_t decode_engine_member(const platform::windows::process_session& process,
                                                  std::uintptr_t object, std::uintptr_t method);
// Whether a panel carries a CSS class. Unknown (unreadable) class state counts as "has it", so a hidden panel is
// never mistaken for a visible one.
[[nodiscard]] bool panel_has_class(std::uintptr_t panel, const runtime& runtime, std::string_view expected);
// Whether the class is certainly there: false when the class list cannot be read (panel_has_class errs towards true,
// which is right for "hidden" but wrong for a state the automation acts on).
[[nodiscard]] bool panel_has_class_strict(std::uintptr_t panel, const runtime& runtime, std::string_view expected);
// The panel's own visibility flag (what scripts set with `panel.visible = ...`), ignoring its ancestors and position.
[[nodiscard]] bool panel_flag_visible(std::uintptr_t panel);
// The panel's rectangle within the UI root, walking up its parents; fails when any of them is hidden.
[[nodiscard]] std::expected<geometry, geometry_failure> inspect_geometry(const runtime& runtime, std::uintptr_t panel);
// A screen point inside the panel (`x_fraction`, `y_fraction` of its width and height), or why there is none.
[[nodiscard]] std::expected<POINT, geometry_failure> screen_point(const runtime& runtime, std::uintptr_t panel,
                                                                  HWND window, float x_fraction = 0.5f,
                                                                  float y_fraction = 0.5f);
// The panel's id ("StartMatchBtn"), empty when it has none.
[[nodiscard]] std::string panel_id(std::uintptr_t panel);
// Whether the address is a live CUIPanel.
[[nodiscard]] bool is_panel(const runtime& runtime, std::uintptr_t panel);
// Whether a panel with this id is among the panel's ancestors (the panel itself excluded), up to `depth` levels.
[[nodiscard]] bool has_ancestor(std::uintptr_t panel, std::string_view id, int depth = 40);

// Finds panels by id across the registry, a chunk per step. Panels are checked again (still a panel, same id) every
// time they are asked for, so a freed panel is never handed out.
class catalog
{
public:
    explicit catalog(std::vector<std::string> ids, std::uint32_t chunk = 2048);
    // One step of the registry walk; true when a full pass has just completed.
    bool step(const runtime& runtime);
    // Live panels with this id.
    [[nodiscard]] std::vector<std::uintptr_t> panels(const runtime& runtime, std::string_view id) const;
    // The first live panel with this id that is visible on screen and (when given) sits inside `ancestor`.
    [[nodiscard]] std::uintptr_t visible(const runtime& runtime, std::string_view id,
                                         std::string_view ancestor = {}) const;
    [[nodiscard]] bool pass_completed() const noexcept { return m_passes > 0; }
    void reset();

private:
    std::vector<std::string> m_ids{};
    std::uint32_t m_chunk{};
    std::uint32_t m_cursor{};
    std::uint64_t m_passes{};
    std::unordered_map<std::string, std::vector<std::uintptr_t>> m_found{};
    std::unordered_map<std::string, std::vector<std::uintptr_t>> m_building{};
    // What each registry slot held at the last look: only slots whose panel or generation changed are read again.
    std::vector<std::uintptr_t> m_slot_panel{};
    std::vector<std::uint32_t> m_slot_generation{};
    std::vector<std::int16_t> m_slot_match{}; // index into m_ids, -1 for none
};
} // namespace game::panorama
