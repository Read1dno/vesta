#include <stdafx.hpp>
#include <platform/windows/storage.hpp>
#include <config/settings.hpp>
#include <platform/windows/input.hpp>
#include <config/misc.hpp>
#include <game/memory/bound_services.hpp>
#include <game/memory/remote_pointer.hpp>
#include <features/misc/misc.hpp>
#include <game/memory/compatibility.hpp>
#include <diagnostics/journal.hpp>
#include <expected>
#include <game/panorama/panorama.hpp>

namespace
{
constexpr std::string_view k_accept_button_id{"AcceptMatchBtn"};
constexpr std::string_view k_accept_popup_id{"id-accept-match"};
using game::panorama::active_root_window;
using game::panorama::decode_engine_member;
using game::panorama::decode_registry_offset;
using game::panorama::geometry_failure;
using game::panorama::inspect_geometry;
using game::panorama::panel_header;
using game::panorama::registry_entry;
using panorama_runtime = game::panorama::runtime;
using panorama_resolver = game::panorama::resolver;
constexpr auto k_panorama_scan_chunk = game::panorama::k_scan_chunk;
constexpr auto k_panorama_max_capacity = game::panorama::k_max_capacity;
// The registry holds 131 072 slots in current CS2 (it grows with the session); a pass takes 128 worker ticks.
template<typename... arguments_t>
void trace_auto_accept(std::format_string<arguments_t...> format, arguments_t&&... arguments)
{
#if defined(_DEBUG) || (defined(VESTA_PERF_LOG) && VESTA_PERF_LOG)
    const auto& directory = platform::windows::runtime_storage::paths().research_logs;
    const auto path = directory.empty() ? std::filesystem::path{} : directory / L"auto_accept.log";
    if (path.empty())
        return;
    std::ofstream stream{path, std::ios::app};
    if (stream)
        stream << std::format(format, std::forward<arguments_t>(arguments)...) << '\n';
#else
    static_cast<void>(format);
    (static_cast<void>(arguments), ...);
#endif
}

using scan_stats = features::misc::auto_accept_t::scan_stats;

// `stats`, when given, counts how far the panel got through the test (registry scan only).
[[nodiscard]] bool is_accept_button(const panorama_runtime& runtime, const std::uintptr_t panel,
                                    scan_stats* stats = nullptr)
{
    const auto& process = game::bound_memory().process;
    if (!game::is_user_pointer(panel) || !runtime.engine)
        return false;

    scan_stats ignored{};
    auto& counted = stats ? *stats : ignored;
    ++counted.panels;
    const auto header = process.load<panel_header>(panel);
    if (header.vtable != runtime.ui_panel_vtable)
        return false;
    ++counted.ui_panels;
    if (!game::is_user_pointer(header.client))
        return false;
    const auto is_button = process.load<std::uintptr_t>(header.client) == runtime.button_vtable;
    counted.buttons += is_button;
    if (!is_button || !game::is_user_pointer(header.id))
        return false;
    const auto button_id = process.load_text(header.id, 64);
    if (button_id != k_accept_button_id)
        return false;
    ++counted.accept_ids;

    auto current = panel;
    bool accept_layout{};
    for (int depth = 0; depth < 40 && game::is_user_pointer(current); ++depth)
    {
        const auto ancestor = process.load<panel_header>(current);
        const auto id = game::is_user_pointer(ancestor.id) ? process.load_text(ancestor.id, 64) : std::string{};
        accept_layout |= id == k_accept_popup_id;
        if (!game::is_user_pointer(ancestor.parent) || ancestor.parent == current)
            break;
        current = ancestor.parent;
    }

    counted.accept_layout += accept_layout;
    // Keep hidden candidates: Panorama can reuse the same button on the next ready-up.
    return accept_layout;
}

// Screen point to click on the accept button, or why there is none.
[[nodiscard]] std::expected<POINT, geometry_failure> button_center(const panorama_runtime& runtime,
                                                                   const std::uintptr_t panel, const HWND hwnd)
{
    if (!is_accept_button(runtime, panel))
        return std::unexpected(0u);
    return game::panorama::screen_point(runtime, panel, hwnd, 0.5f, 0.60f);
}

// Advances the registry walk by one chunk. Returns the newest accept button found, at the end of a full pass or right
// away when a recently registered panel turns into one.
[[nodiscard]] std::uintptr_t scan_accept_button(const panorama_runtime& runtime,
                                                features::misc::auto_accept_t::registry_scan& scan)
{
    const auto& process = game::bound_memory().process;
    if (!runtime.engine)
        return 0;

    const auto registry = runtime.engine + runtime.registry_offset;
    const auto capacity = process.load<std::uint32_t>(registry + 0x0c) & 0x7fffffffu;
    const auto data = process.load<std::uintptr_t>(registry + 0x10);
    if (capacity == 0 || capacity > k_panorama_max_capacity || !game::is_user_pointer(data))
    {
        scan = {};
        return 0;
    }
    if (scan.panels.size() != capacity)
    {
        scan = {};
        scan.panels.assign(capacity, 0);
        scan.generations.assign(capacity, 0);
    }

    const auto take_best = [&scan]
    {
        const auto found = scan.best_panel;
        scan.best_panel = 0;
        scan.best_generation = 0;
        return found;
    };
    const auto offer = [&scan](const std::uintptr_t panel, const std::uint32_t generation)
    {
        if (!scan.best_panel || static_cast<std::int32_t>(generation - scan.best_generation) > 0)
        {
            scan.best_panel = panel;
            scan.best_generation = generation;
        }
    };

    const auto now = std::chrono::steady_clock::now();
    for (auto it = scan.young.begin(); it != scan.young.end();)
    {
        const auto slot = it->first;
        if (now - it->second > std::chrono::seconds(3) || slot >= capacity)
        {
            it = scan.young.erase(it);
            continue;
        }
        registry_entry entry{};
        if (process.copy(data + static_cast<std::uintptr_t>(slot) * sizeof(registry_entry), &entry, sizeof(entry)) &&
            entry.panel == scan.panels[slot] && is_accept_button(runtime, entry.panel))
        {
            offer(entry.panel, static_cast<std::uint32_t>(entry.generation));
            scan.young.erase(it);
            // A found button is reported right away; waiting for a full registry cycle would cost seconds.
            return take_best();
        }
        ++it;
    }

    if (scan.cursor >= capacity)
        scan.cursor = 0;
    const auto begin = scan.cursor;
    const auto count = std::min(k_panorama_scan_chunk, capacity - scan.cursor);
    std::vector<registry_entry> entries(count);
    if (!process.copy(data + static_cast<std::uintptr_t>(begin) * sizeof(registry_entry), entries.data(),
                      entries.size() * sizeof(registry_entry)))
    {
        scan.cursor = 0;
        return 0;
    }

    scan.cursor += count;
    const auto completed_cycle = scan.cursor >= capacity;
    if (completed_cycle)
    {
        scan.cursor = 0;
        const auto first_cycle = !scan.baseline_done;
        scan.baseline_done = true;
        const auto& stats = scan.stats;
        // Panel counts drift on every cycle while the game runs; journaling them would flood the journal (a cycle
        // takes about a second) and push out the events that matter. Only the first cycle and a change in the accept
        // popup's own panels are recorded.
        if (first_cycle || stats.accept_ids != scan.reported.accept_ids ||
            stats.accept_layout != scan.reported.accept_layout)
        {
            scan.reported = stats;
            const auto cap = [](const std::uint32_t value) { return std::min<std::uint32_t>(value, 255u); };
            diagnostics::emit(diagnostics::event::autoaccept_scan, diagnostics::severity::information,
                              diagnostics::code_domain::mode,
                              (cap(stats.ui_panels) << 24) | (cap(stats.buttons) << 16) | (cap(stats.accept_ids) << 8) |
                                  cap(stats.accept_layout));
            diagnostics::emit(diagnostics::event::autoaccept_scan, diagnostics::severity::information,
                              diagnostics::code_domain::worker,
                              (capacity << 12) | std::min<std::uint32_t>(stats.panels, 4095u));
        }
        scan.stats = {};
    }

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto slot = begin + index;
        const auto panel = entries[index].panel;
        const auto generation = static_cast<std::uint32_t>(entries[index].generation);
        if (scan.panels[slot] == panel && scan.generations[slot] == generation)
            continue;
        scan.panels[slot] = panel;
        scan.generations[slot] = generation;
        if (is_accept_button(runtime, panel, &scan.stats))
            offer(panel, generation);
        else if (scan.baseline_done && game::is_user_pointer(panel) && scan.young.size() < 256)
            scan.young[slot] = now;
    }
    return completed_cycle ? take_best() : 0;
}
} // namespace

namespace features::misc
{
struct auto_accept_t::panorama : panorama_resolver
{
};

auto_accept_t::auto_accept_t()
    : m_panorama(std::make_unique<panorama>())
{
}

auto_accept_t::~auto_accept_t() = default;

void auto_accept_t::tick()
{
    const auto runtime_settings = config::get_runtime_snapshot();
    const auto clear_click = [this](const bool release_button)
    {
        if (release_button && m_click_phase == click_phase::pressed)
        {
            platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up);
        }
        if (m_click_phase != click_phase::idle && m_saved_cursor_valid)
            ::SetCursorPos(m_saved_cursor_x, m_saved_cursor_y);
        m_click_phase = click_phase::idle;
        m_click_window = 0;
        m_click_panel = 0;
        m_saved_cursor_valid = false;
    };

    if (!runtime_settings->general.auto_accept)
    {
        clear_click(true);
        m_signal_identity = 0;
        m_candidate_panel = 0;
        m_clicked_panel = 0;
        m_clicked_panel_hidden = false;
        m_scan = {};
        return;
    }

    const auto signal = game::bound_addresses().auto_accept
                            ? game::bound_memory().process.load<std::uintptr_t>(game::bound_addresses().auto_accept)
                            : 0;
    if (signal != m_signal_identity)
    {
        clear_click(true);
        m_signal_identity = signal;
        m_candidate_panel = 0;
        m_clicked_panel = 0;
        m_clicked_panel_hidden = false;
        m_scan = {};
    }

    const auto& runtime = m_panorama->get();
    if (m_click_phase != click_phase::idle)
    {
        const auto cs2_hwnd = reinterpret_cast<HWND>(m_click_window);
        const auto foreground = cs2_hwnd && ::IsWindow(cs2_hwnd) && active_root_window() == cs2_hwnd;
        const auto live_point = foreground ? button_center(runtime, m_click_panel, cs2_hwnd)
                                           : std::expected<POINT, geometry_failure>{std::unexpect, 0u};
        const auto finish_visible_button = [this, &clear_click](const bool hidden)
        {
            const auto completed_panel = m_click_panel;
            clear_click(false);
            m_candidate_panel = 0;
            m_clicked_panel = completed_panel;

            m_clicked_panel_hidden = hidden;
        };

        switch (m_click_phase)
        {
            case click_phase::moved:
                if (!foreground)
                {
                    trace_auto_accept("hardware click paused on focus loss panel={:#x}", m_click_panel);
                    clear_click(false);
                    return;
                }
                if (!live_point)
                {
                    trace_auto_accept("accept button disappeared panel={:#x}", m_click_panel);
                    finish_visible_button(true);
                    return;
                }
                m_click_x = live_point->x;
                m_click_y = live_point->y;
                // Reassert the target once: if Windows coalesced the initial move,
                // the button still receives both hover and down at the same point.
                ::SetCursorPos(m_click_x, m_click_y);
                if (!platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_down))
                {
                    trace_auto_accept("hardware button-down failed panel={:#x}", m_click_panel);
                    clear_click(false);
                    return;
                }
                m_click_phase = click_phase::pressed;
                trace_auto_accept("hardware button-down panel={:#x} point={},{}", m_click_panel, m_click_x, m_click_y);
                return;

            case click_phase::pressed:
                // Always release a successfully injected down edge, even if the popup
                // disappeared or focus changed between worker passes.
                if (!platform::windows::bound_input().pointer(0, 0, platform::windows::pointer_action::primary_up))
                {
                    trace_auto_accept("hardware button-up retry panel={:#x}", m_click_panel);
                    return;
                }
                if (live_point)
                {
                    trace_auto_accept("hardware click completed once; waiting for hide panel={:#x}", m_click_panel);
                }
                else
                {
                    trace_auto_accept("hardware click accepted; button disappeared panel={:#x}", m_click_panel);
                }
                finish_visible_button(!live_point);
                return;

            case click_phase::idle:
                break;
        }
    }

    if (m_clicked_panel)
    {
        if (!is_accept_button(runtime, m_clicked_panel))
        {
            m_clicked_panel = 0;
            m_clicked_panel_hidden = false;
        }
        else if (const auto cs2_hwnd = ::FindWindowW(nullptr, L"Counter-Strike 2"))
        {
            const auto visible_point = button_center(runtime, m_clicked_panel, cs2_hwnd);
            if (!visible_point)
            {
                m_clicked_panel_hidden = true;
            }
            else if (m_clicked_panel_hidden)
            {
                trace_auto_accept("reused accept button appeared panel={:#x}", m_clicked_panel);
                m_candidate_panel = m_clicked_panel;
                m_clicked_panel = 0;
                m_clicked_panel_hidden = false;
            }
        }
    }

    if (const auto discovered = scan_accept_button(runtime, m_scan))
    {
        if (discovered != m_clicked_panel)
        {
            diagnostics::emit(diagnostics::event::autoaccept_candidate);
            m_candidate_panel = discovered;
            trace_auto_accept("candidate panel={:#x}", discovered);
        }
    }

    if (m_candidate_panel && !is_accept_button(runtime, m_candidate_panel))
    {
        m_candidate_panel = 0;
        m_scan = {};
    }
    if (!m_candidate_panel)
        return;

    const auto cs2_hwnd = ::FindWindowW(nullptr, L"Counter-Strike 2");
    if (!cs2_hwnd)
        return;

    const auto point = button_center(runtime, m_candidate_panel, cs2_hwnd);
    if (!point)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_last_blocked_report >= std::chrono::seconds(1))
        {
            m_last_blocked_report = now;
            diagnostics::emit(diagnostics::event::autoaccept_blocked, diagnostics::severity::warning,
                              diagnostics::code_domain::mode, point.error());
        }
        return;
    }

    const auto foreground = active_root_window() == cs2_hwnd;
    if (!foreground)
        return;

    POINT saved{};
    if (!::GetCursorPos(&saved) || !::SetCursorPos(point->x, point->y))
    {
        trace_auto_accept("hardware cursor move failed panel={:#x} point={},{}", m_candidate_panel, point->x, point->y);
        return;
    }

    m_click_window = reinterpret_cast<std::uintptr_t>(cs2_hwnd);
    m_click_panel = m_candidate_panel;
    m_saved_cursor_x = saved.x;
    m_saved_cursor_y = saved.y;
    m_click_x = point->x;
    m_click_y = point->y;
    m_saved_cursor_valid = true;
    m_click_phase = click_phase::moved;
    diagnostics::emit(diagnostics::event::autoaccept_click);
    trace_auto_accept("hardware cursor moved panel={:#x} point={},{} saved={},{}", m_click_panel, m_click_x, m_click_y,
                      m_saved_cursor_x, m_saved_cursor_y);
}

} // namespace features::misc

namespace features::misc
{

int auto_accept_report(std::ostream& out, const game::attach_status& attached)
{
    panorama_resolver resolver{};
    out << std::unitbuf << "auto-accept-report v1\n";
    if (!attached.process)
    {
        out << "process=unavailable\n";
        return 2;
    }
    const auto& process = game::bound_memory().process;
    const auto addresses_ready = attached.addresses;
    const auto signal_address = game::bound_addresses().auto_accept;
    auto previous_signal = signal_address ? process.load<std::uintptr_t>(signal_address) : 0;
    std::size_t signal_changes{};
    for (int sample = 0; sample < 20; ++sample)
    {
        ::Sleep(50);
        const auto value = signal_address ? process.load<std::uintptr_t>(signal_address) : 0;
        signal_changes += value != previous_signal;
        previous_signal = value;
    }
    out << "addresses_ready=" << addresses_ready << std::hex << " signal.address=0x" << signal_address
        << " signal.value=0x" << previous_signal << std::dec << " signal.changes_1s=" << signal_changes << '\n';
    const auto panorama_module = game::bound_memory().modules.panorama ? game::bound_memory().modules.panorama
                                                                       : process.module_base("panorama.dll");
    const auto client_module = game::bound_memory().modules.client;
    out << std::hex << "client=0x" << client_module << " panorama=0x" << panorama_module << std::dec << '\n';
    const auto ui_vtable = process.locate_vtable(panorama_module, "CUIPanel@panorama");
    const auto button_vtable = process.locate_vtable(client_module, "CButton@panorama");
    const auto interface_object = process.locate_vtable_object(panorama_module, "CPanoramaUIEngine");
    const auto interface_table = process.load<std::uintptr_t>(interface_object);
    const auto getter = process.load<std::uintptr_t>(interface_table + 13 * sizeof(std::uintptr_t));
    const auto engine = decode_engine_member(process, interface_object, getter);
    const auto engine_table = process.load<std::uintptr_t>(engine);
    const auto registry_method = process.load<std::uintptr_t>(engine_table + 32 * sizeof(std::uintptr_t));
    const auto offset = decode_registry_offset(process, registry_method);
    out << std::hex << "ui_vtable=0x" << ui_vtable << " button_vtable=0x" << button_vtable << " interface_object=0x"
        << interface_object << " getter=0x" << getter << " engine=0x" << engine << " registry_method=0x"
        << registry_method << " registry_offset=0x" << offset << std::dec << '\n';
    if (!game::is_user_pointer(engine) || !ui_vtable || !offset || offset < 0x40 || offset > 0x1000)
    {
        out << "resolver=FAIL\n";
        return 2;
    }
    const auto registry = engine + offset;
    const auto capacity = process.load<std::uint32_t>(registry + 0x0c) & 0x7fffffffu;
    const auto data = process.load<std::uintptr_t>(registry + 0x10);
    out << "capacity=" << capacity << std::hex << " data=0x" << data << std::dec << '\n';
    if (!capacity || capacity > k_panorama_max_capacity || !game::is_user_pointer(data))
    {
        out << "registry=FAIL\n";
        return 2;
    }
    std::size_t panels{}, button_class{}, accept_id{}, read_failures{};
    for (std::uint32_t cursor = 0; cursor < capacity; cursor += k_panorama_scan_chunk)
    {
        const auto count = std::min(k_panorama_scan_chunk, capacity - cursor);
        std::vector<registry_entry> entries(count);
        if (!process.copy(data + static_cast<std::uintptr_t>(cursor) * sizeof(registry_entry), entries.data(),
                          entries.size() * sizeof(registry_entry)))
        {
            ++read_failures;
            continue;
        }
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto panel = entries[index].panel;
            if (!game::is_user_pointer(panel))
                continue;
            const auto header = process.load<panel_header>(panel);
            if (header.vtable != ui_vtable)
                continue;
            ++panels;
            const auto client_class = process.load<std::uintptr_t>(header.client);
            if (client_class == button_vtable)
                ++button_class;
            if (!game::is_user_pointer(header.id))
                continue;
            const auto id = process.load_text(header.id, 64);
            auto lower = id;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("accept") == std::string::npos && lower.find("match") == std::string::npos)
                continue;
            ++accept_id;
            if (accept_id > 40)
                continue;
            out << std::hex << "candidate.panel=0x" << panel << " client_vtable=0x" << client_class << " parent=0x"
                << header.parent << std::dec << " id=" << id << " button_class=" << (client_class == button_vtable)
                << " known_accept=" << (id == k_accept_button_id) << '\n';
            if (id == "StartMatchBtn")
            {
                const auto layout = game::compatibility::panel(panel);
                const auto geometry = inspect_geometry(resolver.get(), panel);
                out << std::hex << "  layout.width=0x" << layout.width << " layout.x=0x" << layout.x
                    << " layout.visible=0x" << layout.visible << std::dec
                    << " raw.width=" << process.load<float>(panel + layout.width)
                    << " legacy.width=" << process.load<float>(panel + 0x1c0)
                    << " raw.x=" << process.load<float>(panel + layout.x)
                    << " legacy.x=" << process.load<float>(panel + 0x1b0)
                    << " geometry.valid=" << (geometry && geometry->width > 0.0f) << '\n';
            }
            auto current = panel;
            for (int depth = 0; depth < 8 && game::is_user_pointer(current); ++depth)
            {
                const auto ancestor = process.load<panel_header>(current);
                const auto ancestor_id =
                    game::is_user_pointer(ancestor.id) ? process.load_text(ancestor.id, 64) : std::string{};
                out << "  ancestor." << depth << '=' << ancestor_id << '\n';
                if (!game::is_user_pointer(ancestor.parent) || ancestor.parent == current)
                    break;
                current = ancestor.parent;
            }
        }
    }
    out << "panels=" << panels << " button_class=" << button_class << " accept_or_match_ids=" << accept_id
        << " read_failures=" << read_failures << '\n';
    return 0;
}

} // namespace features::misc
