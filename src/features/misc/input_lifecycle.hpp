#pragma once

#include <core/input/bindings.hpp>
#include <span>

namespace features::misc::input_lifecycle {

struct owned_control
{
    game::input_binding binding{};
    bool pressed{};
};

template<class Send>
[[nodiscard]] bool transition(owned_control& control, const bool pressed, Send&& send)
{
    if (control.pressed == pressed)
        return true;
    if (!control.binding || !send(control.binding, pressed))
        return false;
    control.pressed = pressed;
    return true;
}

[[nodiscard]] inline bool discard_released(std::span<owned_control* const> controls)
{
    bool complete = true;
    for (auto* control : controls)
    {
        if (control->pressed)
            complete = false;
        else
            *control = {};
    }
    return complete;
}

template<class PhysicalState>
[[nodiscard]] bool activation_held(const int virtual_key, PhysicalState&& physical_state)
{
    return virtual_key > 0 && virtual_key < 256
        && physical_state(static_cast<std::uint16_t>(virtual_key));
}

} // namespace features::misc::input_lifecycle
