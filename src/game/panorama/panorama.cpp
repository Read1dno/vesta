#include <stdafx.hpp>
#include <game/panorama/panorama.hpp>
#include <diagnostics/journal.hpp>
#include <game/memory/bound_services.hpp>
#include <game/memory/compatibility.hpp>
#include <game/memory/remote_pointer.hpp>

#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace game::panorama
{
namespace
{
[[nodiscard]] panel_class_storage decode_class_storage(const std::span<const std::uint8_t> code);
[[nodiscard]] std::uintptr_t decode_class_symbol_table(const platform::windows::process_session& process,
                                                       const std::uintptr_t method, panel_class_storage& storage);
[[nodiscard]] std::string panorama_symbol_text(const runtime& runtime, const std::uint16_t symbol);
} // namespace

HWND active_root_window()
{
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    return ::GetGUIThreadInfo(0, &gui) ? ::GetAncestor(gui.hwndActive, GA_ROOT) : nullptr;
}

std::uint32_t decode_registry_offset(const platform::windows::process_session& process, const std::uintptr_t method)
{
    std::array<std::uint8_t, 48> code{};
    if (!process.copy(method, code.data(), code.size()))
        return 0;
    for (std::size_t index = 0; index + 7 <= code.size(); ++index)
    {
        if (code[index] == 0x48 && code[index + 1] == 0x81 && code[index + 2] == 0xc1)
        {
            std::uint32_t offset{};
            std::memcpy(&offset, code.data() + index + 3, sizeof(offset));
            return offset;
        }
        if (code[index] == 0x48 && code[index + 1] == 0x83 && code[index + 2] == 0xc1)
            return code[index + 3];
    }
    return 0;
}

std::uintptr_t decode_engine_member(const platform::windows::process_session& process, const std::uintptr_t object,
                                    const std::uintptr_t method)
{
    std::array<std::uint8_t, 32> code{};
    if (!process.copy(method, code.data(), code.size()))
        return 0;
    for (std::size_t index = 0; index + 7 <= code.size(); ++index)
    {
        std::int32_t offset{};
        if (code[index] == 0x48 && code[index + 1] == 0x8b && code[index + 2] == 0x81)
        {
            std::memcpy(&offset, code.data() + index + 3, sizeof(offset));
            return process.load<std::uintptr_t>(object + offset);
        }
        if (code[index] == 0x48 && code[index + 1] == 0x8b && code[index + 2] == 0x41)
            return process.load<std::uintptr_t>(object + code[index + 3]);
    }
    return 0;
}

namespace
{
panel_class_storage decode_class_storage(const std::span<const std::uint8_t> code)
{
    panel_class_storage result{};
    const auto compare = [&](const std::size_t at, const std::uint8_t opcode, const std::uint32_t offset)
    {
        if (at + 7 > code.size() || code[at] != 0x41 || code[at + 1] != opcode || code[at + 2] != 0xb5)
            return false;
        std::uint32_t field{};
        std::memcpy(&field, code.data() + at + 3, sizeof(field));
        return field == offset;
    };
    for (std::size_t index = 0; index + 7 <= code.size(); ++index)
    {
        if (code[index] != 0x49 || code[index + 1] != 0x8b || code[index + 2] != 0x85)
            continue;
        std::uint32_t data{};
        std::memcpy(&data, code.data() + index + 3, sizeof(data));
        if (data < 0x108 || data > 0x800 || (data & 7))
            continue;
        const auto count = data - 8;
        bool before{}, after{};
        // Match both bounds of the serializer's uint16 class-vector loop.
        for (auto at = index > 128 ? index - 128 : 0; at < index; ++at)
            before |= compare(at, 0x39, count);
        for (auto at = index + 7; at < std::min(code.size(), index + 512); ++at)
            after |= compare(at, 0x3b, count);
        if (!before || !after)
            continue;
        if (result && (result.count != count || result.data != data))
            return {};
        result = {count, data};
    }
    return result;
}

std::uintptr_t decode_class_symbol_table(const platform::windows::process_session& process, const std::uintptr_t method,
                                         panel_class_storage& storage)
{
    std::array<std::uint8_t, 0x1200> code{};
    if (!process.copy(method, code.data(), code.size()))
        return 0;
    storage = decode_class_storage(code);
    if (!storage)
        return 0;
    for (std::size_t index = 0; index + 13 <= code.size(); ++index)
    {
        if (code[index] != 0x48 || code[index + 1] != 0x8d || code[index + 2] != 0x0d || code[index + 7] != 0xff ||
            code[index + 8] != 0x15)
            continue;
        std::int32_t displacement{};
        std::memcpy(&displacement, code.data() + index + 3, sizeof(displacement));
        const auto candidate = method + index + 7 + displacement;
        const auto indices = process.load<std::uintptr_t>(candidate + 0x28);
        const auto shift = process.load<std::uint32_t>(candidate + 0x38);
        const auto mask = process.load<std::uint32_t>(candidate + 0x3c);
        const auto pool_count = process.load<std::uint32_t>(candidate + 0x40);
        const auto external_pools = process.load<std::uint32_t>(candidate + 0x44);
        const auto pools = process.load<std::uintptr_t>(candidate + 0x48);
        if (game::is_user_pointer(indices) && shift > 0 && shift < 32 && mask && pool_count > 0 && pool_count < 4096 &&
            (external_pools & 0x7fffffffu) != 0 && game::is_user_pointer(pools))
            return candidate;
    }
    return 0;
}

// Class names are interned once per game session and never change: each symbol is read from the game once. Without
// this every visibility check re-read every class name of every ancestor, thousands of reads per check.
std::string read_symbol_text(const runtime& runtime, std::uint16_t symbol);

std::string panorama_symbol_text(const runtime& runtime, const std::uint16_t symbol)
{
    static std::mutex mutex;
    static std::unordered_map<std::uint64_t, std::string> cache;
    const auto key = (static_cast<std::uint64_t>(runtime.class_symbol_table) << 16) | symbol;
    {
        std::scoped_lock lock(mutex);
        if (const auto found = cache.find(key); found != cache.end())
            return found->second;
    }
    auto text = read_symbol_text(runtime, symbol);
    if (!text.empty())
    {
        std::scoped_lock lock(mutex);
        if (cache.size() > 16384)
            cache.clear();
        cache.emplace(key, text);
    }
    return text;
}

std::string read_symbol_text(const runtime& runtime, const std::uint16_t symbol)
{
    if (symbol == 0xffff || !runtime.class_symbol_table)
        return {};
    const auto& process = game::bound_memory().process;
    const auto table = runtime.class_symbol_table;
    const auto indices = process.load<std::uintptr_t>(table + 0x28);
    const auto shift = process.load<std::uint32_t>(table + 0x38);
    const auto mask = process.load<std::uint32_t>(table + 0x3c);
    const auto pool_count = process.load<std::uint32_t>(table + 0x40);
    const auto pools = process.load<std::uintptr_t>(table + 0x48);
    if (!game::is_user_pointer(indices) || shift == 0 || shift >= 32 || !mask || !game::is_user_pointer(pools))
        return {};
    std::uint32_t packed{};
    if (!process.copy(indices + static_cast<std::uintptr_t>(symbol) * sizeof(packed), &packed, sizeof(packed)))
        return {};
    const auto pool = packed >> shift;
    if (pool >= pool_count)
        return {};
    const auto pool_base = process.load<std::uintptr_t>(pools + static_cast<std::uintptr_t>(pool) * 0x10 + 0x08);
    if (!game::is_user_pointer(pool_base))
        return {};
    return process.load_text(pool_base + (packed & mask), 64);
}

} // namespace

bool panel_has_class(const std::uintptr_t panel, const runtime& runtime, const std::string_view expected)
{
    const auto& process = game::bound_memory().process;
    std::uint32_t count{};
    std::uintptr_t data{};
    if (!runtime.class_storage || !process.copy(panel + runtime.class_storage.count, &count, sizeof(count)))
        return true;
    if (count == 0)
        return false;
    // Unresolved CSS state must not turn a hidden panel into a click target.
    if (count > 64 || !process.copy(panel + runtime.class_storage.data, &data, sizeof(data)) ||
        !game::is_user_pointer(data) || !runtime.class_symbol_table)
        return true;
    std::array<std::uint16_t, 64> symbols{};
    if (!process.copy(data, symbols.data(), count * sizeof(symbols[0])))
        return true;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto text = panorama_symbol_text(runtime, symbols[index]);
        if (text.empty() || text == expected)
            return true;
    }
    return false;
}

bool panel_has_class_strict(const std::uintptr_t panel, const runtime& runtime, const std::string_view expected)
{
    return panel_has_class(panel, runtime, expected) && !panel_has_class(panel, runtime, "vesta-no-such-class");
}

bool panel_flag_visible(const std::uintptr_t panel)
{
    const auto layout = game::compatibility::panel(panel);
    return layout && (game::bound_memory().process.load<std::uint8_t>(panel + layout.visible) & 0x08) != 0;
}

const runtime& resolver::get()
{
    auto& runtime = m_runtime;
    if (runtime.engine && runtime.class_symbol_table && runtime.class_storage)
        return runtime;
    const auto now = std::chrono::steady_clock::now();
    if (now < m_next_retry)
        return runtime;
    m_next_retry = now + std::chrono::seconds(2);

    const auto& process = game::bound_memory().process;
    const auto panorama_module = game::bound_memory().modules.panorama ? game::bound_memory().modules.panorama
                                                                       : process.module_base("panorama.dll");
    const auto client_module = game::bound_memory().modules.client;
    if (!panorama_module || !client_module)
        return runtime;
    runtime.attempted = true;

    runtime.ui_panel_vtable = process.locate_vtable(panorama_module, "CUIPanel@panorama");
    runtime.button_vtable = process.locate_vtable(client_module, "CButton@panorama");
    const auto panel_serializer = process.load<std::uintptr_t>(runtime.ui_panel_vtable + 315 * sizeof(std::uintptr_t));
    runtime.class_symbol_table = decode_class_symbol_table(process, panel_serializer, runtime.class_storage);
    const auto interface_object = process.locate_vtable_object(panorama_module, "CPanoramaUIEngine");
    const auto interface_table = process.load<std::uintptr_t>(interface_object);
    const auto engine_getter = process.load<std::uintptr_t>(interface_table + 13 * sizeof(std::uintptr_t));
    runtime.engine = decode_engine_member(process, interface_object, engine_getter);
    const auto engine_table = process.load<std::uintptr_t>(runtime.engine);
    const auto valid_panel_method = process.load<std::uintptr_t>(engine_table + 32 * sizeof(std::uintptr_t));
    runtime.registry_offset = decode_registry_offset(process, valid_panel_method);

    // Journal which resolver stages worked (only when the picture changes): bit0 panorama.dll, bit1 client.dll,
    // bit2 CUIPanel vtable, bit3 CButton vtable, bit4 class symbol table, bit5 class storage, bit6 panorama
    // interface object, bit7 engine pointer, bit8 registry offset in range.
    {
        const auto stages = (panorama_module ? 1u : 0u) | (client_module ? 2u : 0u) |
                            (runtime.ui_panel_vtable ? 4u : 0u) | (runtime.button_vtable ? 8u : 0u) |
                            (runtime.class_symbol_table ? 16u : 0u) | (runtime.class_storage ? 32u : 0u) |
                            (interface_object ? 64u : 0u) | (game::is_user_pointer(runtime.engine) ? 128u : 0u) |
                            (runtime.registry_offset >= 0x40 && runtime.registry_offset <= 0x1000 ? 256u : 0u);
        if (stages != m_reported_stages)
        {
            m_reported_stages = stages;
            diagnostics::emit(diagnostics::event::autoaccept_resolver,
                              stages == 511u ? diagnostics::severity::information : diagnostics::severity::warning,
                              diagnostics::code_domain::mode, stages);
        }
    }
    if (!game::is_user_pointer(runtime.engine) || !runtime.ui_panel_vtable || !runtime.button_vtable ||
        runtime.registry_offset < 0x40 || runtime.registry_offset > 0x1000)
    {
        runtime = {};
        game::bound_memory().log.warning("[panorama] object resolver initialization failed");
    }
    else
    {
        game::bound_memory().log.info("[panorama] Button resolver ready (registry +{:#x})", runtime.registry_offset);
    }
    return runtime;
}

// Why a panel has no usable geometry, as journaled with autoaccept.blocked: 1 no layout probe for a panel,
// 2 visibility bit clear, 3 "hidden" class, 4 position not finite, 5 no root layout (the ancestor depth is in bits
// 8 and up); 6 size too small, with the button width in bits 16-27, its height in bits 4-15 and the root width's
// high bits in 0-3. 0 means the panel is not the accept button or the window has no client area.

namespace
{
// The position property set on the panel itself (id read from the actual-offset getter): value {x, unit, y, unit}.
struct position_property
{
    bool found{};
    float x{};
    std::int32_t x_unit{};
    float y{};
    std::int32_t y_unit{};
};

[[nodiscard]] position_property inline_position(const std::uintptr_t panel)
{
    const auto& process = game::bound_memory().process;
    static std::int32_t property_id = -1;
    if (property_id < 0)
    {
        const auto table = process.load<std::uintptr_t>(panel);
        const auto getter = process.load<std::uintptr_t>(table + 0x380);
        std::array<std::uint8_t, 0x20> code{};
        if (getter && process.copy(getter, code.data(), code.size()))
            for (std::size_t i = 0; i + 7 <= code.size(); ++i)
                if (code[i] == 0x0f && code[i + 1] == 0xb6 && (code[i + 2] & 0xc7) == 0x05)
                {
                    std::int32_t disp{};
                    std::memcpy(&disp, code.data() + i + 3, 4);
                    property_id = process.load<std::uint8_t>(getter + i + 7 + disp);
                    break;
                }
        if (property_id < 0)
            return {};
    }
    const auto count = process.load<std::int32_t>(panel + 0xa0);
    const auto entries = process.load<std::uintptr_t>(panel + 0xa8);
    if (count <= 0 || count > 64 || !game::is_user_pointer(entries))
        return {};
    std::array<std::uint8_t, 64 * 16> table{};
    if (!process.copy(entries, table.data(), static_cast<std::size_t>(count) * 16))
        return {};
    for (std::int32_t k = 0; k < count; ++k)
    {
        if (table[static_cast<std::size_t>(k) * 16] != property_id)
            continue;
        std::uintptr_t value{};
        std::memcpy(&value, table.data() + static_cast<std::size_t>(k) * 16 + 8, sizeof(value));
        if (!game::is_user_pointer(value))
            return {};
        std::array<std::uint8_t, 16> raw{};
        if (!process.copy(value + 0x10, raw.data(), raw.size()))
            return {};
        position_property result{.found = true};
        std::memcpy(&result.x, raw.data(), 4);
        std::memcpy(&result.x_unit, raw.data() + 4, 4);
        std::memcpy(&result.y, raw.data() + 8, 4);
        std::memcpy(&result.y_unit, raw.data() + 12, 4);
        return result;
    }
    return {};
}

geometry_failure failure_at(const std::uint32_t reason, const int depth)
{
    return reason | (static_cast<std::uint32_t>(depth) << 8);
}
} // namespace

std::expected<geometry, geometry_failure> inspect_geometry(const runtime& runtime, const std::uintptr_t panel)
{
    const auto& process = game::bound_memory().process;
    if (!runtime.class_symbol_table || !runtime.class_storage)
        return std::unexpected(0u);
    geometry result{};
    std::uintptr_t current = panel;
    std::uintptr_t root = panel;
    game::compatibility::panel_layout root_layout{};
    std::unordered_set<std::uintptr_t> visited{};

    for (int depth = 0; depth < 40 && game::is_user_pointer(current); ++depth)
    {
        if (!visited.emplace(current).second)
            return std::unexpected(0u);
        const auto layout = game::compatibility::panel(current);
        if (!layout)
            return std::unexpected(failure_at(1, depth));
        const auto visibility = process.load<std::uint8_t>(current + layout.visible);
        if ((visibility & 0x08) == 0)
            return std::unexpected(failure_at(2, depth));
        // Hidden by a class: the generic one, the popups' one, and the main menu's tab and content switching (those
        // keep the visibility flag and move or fade the panel instead).
        for (const auto* hiding : {"hidden", "Hidden", "mainmenu-content--hidden", "mainmenu-content--offscreen"})
            if (panel_has_class(current, runtime, hiding))
                return std::unexpected(failure_at(3, depth));

        auto x = process.load<float>(current + layout.x);
        auto y = process.load<float>(current + layout.y);
        const auto width = process.load<float>(current + layout.width);
        const auto height = process.load<float>(current + layout.height);
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 30000.0f || std::abs(y) > 30000.0f)
            return std::unexpected(failure_at(4, depth));
        // A collapsed container hides everything in it.
        if (!(width > 0.5f) || !(height > 0.5f))
            return std::unexpected(failure_at(7, depth));
        if (current == panel)
        {
            result.width = width;
            result.height = height;
        }

        root = current;
        root_layout = layout;
        const auto parent = process.load<std::uintptr_t>(current + 0x18);
        // The game's own actual offset (vtable 0x380/0x388): the stored offset plus the position property the layout
        // writes on the panel (flowing children get their place there); a percentage is of the parent's content size.
        if (const auto position = inline_position(current); position.found)
        {
            auto px = position.x;
            auto py = position.y;
            if (game::is_user_pointer(parent) && parent != current)
            {
                if (position.x_unit == 2)
                    px = px * process.load<float>(parent + 0x1d0) / 100.0f;
                if (position.y_unit == 2)
                    py = py * process.load<float>(parent + 0x1d4) / 100.0f;
            }
            if (std::isfinite(px) && std::isfinite(py) && std::abs(px) < 30000.0f && std::abs(py) < 30000.0f)
            {
                x += px;
                y += py;
            }
        }
        result.x += x;
        result.y += y;
        if (!game::is_user_pointer(parent) || parent == current)
            break;
        current = parent;
    }

    if (!root_layout)
        return std::unexpected(5u);
    result.root_width = process.load<float>(root + root_layout.width);
    result.root_height = process.load<float>(root + root_layout.height);
    if (!std::isfinite(result.width) || !std::isfinite(result.height) || !std::isfinite(result.root_width) ||
        !std::isfinite(result.root_height) || result.width < 24.0f || result.height < 16.0f ||
        result.root_width < 320.0f || result.root_height < 240.0f)
    {
        const auto pixels = [](const float value)
        { return static_cast<std::uint32_t>(std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 4095.0f)); };
        return std::unexpected((6u << 28) | (pixels(result.width) << 16) | (pixels(result.height) << 4) |
                               (pixels(result.root_width) >> 8));
    }
    return result;
}

std::expected<POINT, geometry_failure> screen_point(const runtime& runtime, const std::uintptr_t panel, const HWND hwnd,
                                                    const float x_fraction, const float y_fraction)
{
    const auto inspected = inspect_geometry(runtime, panel);
    if (!inspected)
        return std::unexpected(inspected.error());
    const auto& geometry = *inspected;
    if (geometry.width <= 0.0f || geometry.height <= 0.0f)
        return std::unexpected(0u);

    RECT client{};
    if (!::GetClientRect(hwnd, &client))
        return std::unexpected(0u);
    const auto client_width = client.right - client.left;
    const auto client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0)
        return std::unexpected(0u);

    const auto scale_x = static_cast<float>(client_width) / geometry.root_width;
    const auto scale_y = static_cast<float>(client_height) / geometry.root_height;
    POINT point{static_cast<LONG>(std::lround((geometry.x + geometry.width * x_fraction) * scale_x)),
                static_cast<LONG>(std::lround((geometry.y + geometry.height * y_fraction) * scale_y))};
    if (point.x < client.left || point.x >= client.right || point.y < client.top || point.y >= client.bottom)
        return std::unexpected(0u);
    if (!::ClientToScreen(hwnd, &point))
        return std::unexpected(0u);
    return point;
}

std::string panel_id(const std::uintptr_t panel)
{
    const auto& process = game::bound_memory().process;
    if (!game::is_user_pointer(panel))
        return {};
    const auto header = process.load<panel_header>(panel);
    return game::is_user_pointer(header.id) ? process.load_text(header.id, 64) : std::string{};
}

bool is_panel(const runtime& runtime, const std::uintptr_t panel)
{
    return runtime.ui_panel_vtable && game::is_user_pointer(panel) &&
           game::bound_memory().process.load<std::uintptr_t>(panel) == runtime.ui_panel_vtable;
}

bool has_ancestor(const std::uintptr_t panel, const std::string_view id, const int depth)
{
    const auto& process = game::bound_memory().process;
    auto current = panel;
    for (int level = 0; level < depth && game::is_user_pointer(current); ++level)
    {
        const auto header = process.load<panel_header>(current);
        if (!game::is_user_pointer(header.parent) || header.parent == current)
            return false;
        current = header.parent;
        if (panel_id(current) == id)
            return true;
    }
    return false;
}

catalog::catalog(std::vector<std::string> ids, const std::uint32_t chunk)
    : m_ids(std::move(ids))
    , m_chunk(std::max<std::uint32_t>(chunk, 64))
{
}

void catalog::reset()
{
    m_cursor = 0;
    m_passes = 0;
    m_found.clear();
    m_building.clear();
    m_slot_panel.clear();
    m_slot_generation.clear();
    m_slot_match.clear();
}

bool catalog::step(const runtime& runtime)
{
    const auto& process = game::bound_memory().process;
    if (!runtime.engine || !runtime.ui_panel_vtable)
        return false;
    const auto registry = runtime.engine + runtime.registry_offset;
    const auto capacity = process.load<std::uint32_t>(registry + 0x0c) & 0x7fffffffu;
    const auto data = process.load<std::uintptr_t>(registry + 0x10);
    if (capacity == 0 || capacity > k_max_capacity || !game::is_user_pointer(data))
        return false;
    if (m_slot_panel.size() != capacity)
    {
        m_slot_panel.assign(capacity, 0);
        m_slot_generation.assign(capacity, 0);
        m_slot_match.assign(capacity, -1);
        m_cursor = 0;
        m_building.clear();
    }
    if (m_cursor >= capacity)
        m_cursor = 0;
    const auto count = std::min(m_chunk, capacity - m_cursor);
    std::vector<registry_entry> entries(count);
    if (!process.copy(data + static_cast<std::uintptr_t>(m_cursor) * sizeof(registry_entry), entries.data(),
                      entries.size() * sizeof(registry_entry)))
    {
        m_cursor = 0;
        m_building.clear();
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& entry = entries[index];
        const auto slot = m_cursor + index;
        const auto generation = static_cast<std::uint32_t>(entry.generation);
        if (m_slot_panel[slot] != entry.panel || m_slot_generation[slot] != generation)
        {
            // A new or reused slot: read the panel once and remember whether it is one of the watched ids.
            m_slot_panel[slot] = entry.panel;
            m_slot_generation[slot] = generation;
            m_slot_match[slot] = -1;
            if (game::is_user_pointer(entry.panel))
            {
                const auto header = process.load<panel_header>(entry.panel);
                if (header.vtable == runtime.ui_panel_vtable && game::is_user_pointer(header.id))
                {
                    const auto id = process.load_text(header.id, 64);
                    if (const auto it = std::ranges::find(m_ids, id); it != m_ids.end())
                        m_slot_match[slot] = static_cast<std::int16_t>(it - m_ids.begin());
                }
            }
        }
        if (m_slot_match[slot] >= 0)
            m_building[m_ids[static_cast<std::size_t>(m_slot_match[slot])]].push_back(entry.panel);
    }
    m_cursor += count;
    if (m_cursor < capacity)
        return false;
    m_cursor = 0;
    m_found = std::move(m_building);
    m_building.clear();
    ++m_passes;
    return true;
}

std::vector<std::uintptr_t> catalog::panels(const runtime& runtime, const std::string_view id) const
{
    std::vector<std::uintptr_t> result;
    const auto found = m_found.find(std::string{id});
    if (found == m_found.end())
        return result;
    for (const auto panel : found->second)
        if (is_panel(runtime, panel) && panel_id(panel) == id)
            result.push_back(panel);
    return result;
}

std::uintptr_t catalog::visible(const runtime& runtime, const std::string_view id,
                                const std::string_view ancestor) const
{
    for (const auto panel : panels(runtime, id))
    {
        if (!ancestor.empty() && !has_ancestor(panel, ancestor))
            continue;
        const auto shape = inspect_geometry(runtime, panel);
        if (shape && shape->width > 0.0f && shape->height > 0.0f)
            return panel;
    }
    return 0;
}
} // namespace game::panorama
