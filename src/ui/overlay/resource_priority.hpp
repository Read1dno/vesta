#pragma once
#include <algorithm>

namespace ui
{
// How the overlay shares the CPU and GPU with the game (Misc > Engine). The same three levels apply to both.
enum class resource_priority : int
{
    game_first = 0,    // the game wins every contention
    balanced = 1,      // the overlay competes as an equal
    overlay_first = 2, // the overlay is served before the game
};
inline constexpr int k_resource_priority_count = 3;

[[nodiscard]] constexpr resource_priority to_resource_priority(const int value) noexcept
{
    return static_cast<resource_priority>(std::clamp(value, 0, k_resource_priority_count - 1));
}

// Windows priority classes in ascending order. Thread priorities are relative to the class, so moving the class moves
// every Vesta thread (render, input, workers) together.
enum class cpu_class_rank : int
{
    idle = 0,
    below_normal = 1,
    normal = 2,
    above_normal = 3,
    high = 4, // the ceiling: realtime would starve the game and the system input stack
};

struct cpu_priority_plan
{
    cpu_class_rank priority_class{cpu_class_rank::normal};
    // Opts the process out of EcoQoS. Vesta is never the foreground process while the game runs, and Windows 11 may
    // otherwise run it on efficiency cores at reduced clocks.
    bool disable_power_throttling{};
    bool raise_render_thread{};
};

// The class is chosen relative to the game's own class, so "balanced" still means equal when the game was started
// with a raised priority.
[[nodiscard]] constexpr cpu_priority_plan plan_cpu_priority(const resource_priority level,
                                                            const cpu_class_rank game) noexcept
{
    const auto game_rank = static_cast<int>(game);
    switch (level)
    {
        case resource_priority::game_first:
            return {
                static_cast<cpu_class_rank>(std::max(game_rank - 1, static_cast<int>(cpu_class_rank::below_normal))),
                false, false};
        case resource_priority::overlay_first:
            return {static_cast<cpu_class_rank>(std::min(game_rank + 1, static_cast<int>(cpu_class_rank::high))), true,
                    true};
        case resource_priority::balanced:
        default:
            return {static_cast<cpu_class_rank>(std::clamp(game_rank, static_cast<int>(cpu_class_rank::below_normal),
                                                           static_cast<int>(cpu_class_rank::high))),
                    true, false};
    }
}

// D3DKMT_SCHEDULINGPRIORITYCLASS values; the class orders this process's GPU work against other processes'.
enum class gpu_scheduling_class : int
{
    idle = 0,
    below_normal = 1,
    normal = 2,
    above_normal = 3,
    high = 4,
};

struct gpu_priority_plan
{
    gpu_scheduling_class scheduling_class{gpu_scheduling_class::normal};
    int thread_priority{}; // IDXGIDevice::SetGPUThreadPriority, -7..7
};

// The game runs in the normal scheduling class and, as the foreground window, gets the scheduler's foreground boost;
// the overlay never is the foreground window, so overlay first takes the high class. Realtime is never used: it can
// starve the desktop compositor. Game first is the default (-2 within the normal class).
[[nodiscard]] constexpr gpu_priority_plan plan_gpu_priority(const resource_priority level) noexcept
{
    switch (level)
    {
        case resource_priority::balanced:
            return {gpu_scheduling_class::normal, 0};
        case resource_priority::overlay_first:
            return {gpu_scheduling_class::high, 7};
        case resource_priority::game_first:
        default:
            return {gpu_scheduling_class::normal, -2};
    }
}
} // namespace ui
