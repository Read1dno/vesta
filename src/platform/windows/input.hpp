#pragma once

#include <windows.h>

#include <atomic>
#include <array>
#include <mutex>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>

namespace platform::windows
{

enum class pointer_action : std::uint32_t
{
    none = 0,
    primary_down = 1u << 0,
    primary_up = 1u << 1,
    secondary_down = 1u << 2,
    secondary_up = 1u << 3,
    relative_move = 1u << 4,
    middle_down = 1u << 5,
    middle_up = 1u << 6,
    auxiliary1_down = 1u << 7,
    auxiliary1_up = 1u << 8,
    auxiliary2_down = 1u << 9,
    auxiliary2_up = 1u << 10,
};

[[nodiscard]] constexpr pointer_action operator|(pointer_action left, pointer_action right) noexcept
{
    return static_cast<pointer_action>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

inline constexpr std::array<std::pair<pointer_action, pointer_action>, 5> pointer_buttons{{
    {pointer_action::primary_down, pointer_action::primary_up},
    {pointer_action::secondary_down, pointer_action::secondary_up},
    {pointer_action::middle_down, pointer_action::middle_up},
    {pointer_action::auxiliary1_down, pointer_action::auxiliary1_up},
    {pointer_action::auxiliary2_down, pointer_action::auxiliary2_up},
}};
inline constexpr std::uint32_t pointer_button_action_mask = static_cast<std::uint32_t>(
    pointer_action::primary_down | pointer_action::primary_up | pointer_action::secondary_down |
    pointer_action::secondary_up | pointer_action::middle_down | pointer_action::middle_up |
    pointer_action::auxiliary1_down | pointer_action::auxiliary1_up | pointer_action::auxiliary2_down |
    pointer_action::auxiliary2_up);

[[nodiscard]] constexpr std::uint32_t pointer_owned_after(std::uint32_t owned, pointer_action actions) noexcept
{
    const auto bits = static_cast<std::uint32_t>(actions);
    for (const auto& [down, up] : pointer_buttons)
    {
        const auto pressed = static_cast<std::uint32_t>(down);
        if (bits & pressed)
            owned |= pressed;
        if (bits & static_cast<std::uint32_t>(up))
            owned &= ~pressed;
    }
    return owned;
}

[[nodiscard]] constexpr bool pointer_releases_any(pointer_action actions) noexcept
{
    for (const auto& [down, up] : pointer_buttons)
        if (static_cast<std::uint32_t>(actions) & static_cast<std::uint32_t>(up))
            return true;
    return false;
}

// Which path delivered the calling thread's last input event (recorded in shot traces).
enum class injection_backend : std::uint8_t
{
    none,
    pointer_native,
    pointer_send_input,
    key_native,
    key_send_input,
};

class input_gateway
{
public:
    input_gateway() = default;
    ~input_gateway();

    input_gateway(const input_gateway&) = delete;
    input_gateway& operator=(const input_gateway&) = delete;

    struct key_transition
    {
        std::uint16_t virtual_key{};
        bool pressed{};
    };

    [[nodiscard]] static injection_backend last_injection_backend() noexcept { return s_last_injection_backend; }
    // Resolves the injection back ends and starts the keyboard gate thread. Until it succeeds, and after
    // disconnect(), the gateway sends nothing: every pointer/key call returns false.
    [[nodiscard]] bool connect() noexcept;
    [[nodiscard]] bool connected() const noexcept { return m_connected.load(std::memory_order_acquire); }
    // Called only after every producer has stopped; also used by the destructor.
    void disconnect() noexcept;
    bool pointer(int dx, int dy, pointer_action actions) const noexcept;
    [[nodiscard]] bool injected_primary_down() const noexcept
    {
        return (m_injected_pointer_down.load(std::memory_order_acquire) &
                static_cast<std::uint32_t>(pointer_action::primary_down)) != 0;
    }
    // Relative pointer movement on behalf of the route walker: unlike every other sender it does not count as
    // "someone else turned the view", which is what the walker itself watches for.
    bool walker_pointer(int dx, int dy) const noexcept;
    // When another feature last sent relative pointer movement (steady_clock ticks, 0 = never).
    [[nodiscard]] std::int64_t foreign_pointer_ticks() const noexcept
    {
        return m_foreign_pointer_ticks.load(std::memory_order_acquire);
    }
    bool key(std::uint16_t virtual_key, bool pressed) const noexcept;
    bool keys(std::span<const key_transition> transitions) const noexcept;

    // Keys the gate must never swallow (the application's own menu/exit keys). Set once before the first gate.
    using key_filter = bool (*)(std::uint16_t virtual_key);
    void set_protected_keys(key_filter is_protected) noexcept
    {
        m_protected_keys.store(is_protected, std::memory_order_release);
    }

    void set_key_gate(std::uint16_t virtual_key, bool enabled) noexcept;
    // Each feature that takes movement keys away from the player owns its own set; the gate is the union.
    // Releasing one owner never frees keys another owner still wants.
    enum class gate_owner : std::uint8_t
    {
        auto_stop,
        nade_helper,
        walk_bot,
        count
    };
    void set_movement_gate(gate_owner owner, std::span<const std::uint16_t> virtual_keys, bool enabled) noexcept;
    // Frees every owner's keys at once (the menu opening, shutdown).
    void clear_movement_gates() noexcept;
    [[nodiscard]] bool physical_key_down(std::uint16_t virtual_key) const noexcept;
    [[nodiscard]] bool key_gate_held() const noexcept { return m_gate_physical_down.load(std::memory_order_acquire); }
    // True when the last attempt to install the keyboard hook failed (the notice plate tells the player).
    [[nodiscard]] bool hook_failed() const noexcept { return m_hook_failed.load(std::memory_order_acquire); }
    [[nodiscard]] bool key_gate_ready() const noexcept { return m_gate_hook_ready.load(std::memory_order_acquire); }

private:
    static inline thread_local injection_backend s_last_injection_backend{};
    static LRESULT CALLBACK gate_proc(int code, WPARAM message, LPARAM parameter);
    void gate_thread_main(std::stop_token stop);
    void stop_key_gate() noexcept;

    static inline std::atomic<input_gateway*> s_gate_owner{};

    struct pointer_packet
    {
        POINT point{};
        unsigned long mouse_data{};
        unsigned long flags{};
        unsigned long timestamp{};
        std::uintptr_t extra_info{};
    };

    struct key_packet
    {
        std::uint16_t virtual_key{};
        std::uint16_t scan_code{};
        unsigned long flags{};
        unsigned long timestamp{};
        std::uintptr_t extra_info{};
    };

    using inject_pointer_fn = BOOL(NTAPI*)(pointer_packet*, int);
    using inject_key_fn = BOOL(NTAPI*)(key_packet*, int);

    std::atomic<bool> m_connected{};
    std::atomic<key_filter> m_protected_keys{};
    [[nodiscard]] bool protected_key(std::uint16_t virtual_key) const noexcept
    {
        const auto is_protected = m_protected_keys.load(std::memory_order_acquire);
        return is_protected && is_protected(virtual_key);
    }
    inject_pointer_fn m_pointer_injector{};
    inject_key_fn m_key_injector{};
    mutable std::atomic<std::uint32_t> m_injected_pointer_down{};
    mutable std::atomic<std::int64_t> m_foreign_pointer_ticks{};
    static inline thread_local bool s_walker_pointer{};
    std::jthread m_gate_thread{};
    std::atomic<DWORD> m_gate_thread_id{};
    std::atomic<bool> m_gate_thread_ready{};
    std::atomic<bool> m_gate_hook_ready{};
    std::atomic<bool> m_hook_failed{};
    std::atomic<bool> m_gate_enabled{};
    std::atomic<bool> m_primary_gate_enabled{};
    std::atomic<std::uint16_t> m_gate_key{};
    std::atomic<bool> m_gate_physical_down{};
    void apply_movement_gate(const std::array<bool, 256>& desired) noexcept;
    std::mutex m_gate_owner_mutex{};
    std::array<std::array<bool, 256>, static_cast<std::size_t>(gate_owner::count)> m_owner_keys{};
    std::array<std::atomic<bool>, 256> m_movement_gated{};
    std::array<std::atomic<bool>, 256> m_movement_physical_down{};
    std::atomic<std::uint16_t> m_movement_gate_count{};
    mutable std::array<std::atomic<bool>, 256> m_injected_down{};
    HHOOK m_gate_hook{};
};

// The gateway the application connected, for features and scripts that send input. Bound once at startup by the
// composition root; before that it refers to a disconnected gateway that sends nothing.
void bind_input(input_gateway& gateway) noexcept;
[[nodiscard]] input_gateway& bound_input() noexcept;

} // namespace platform::windows
