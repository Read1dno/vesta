#pragma once
#include <core/input/bindings.hpp>
#include <core/input/input.hpp>
#include <windows.h>
#include <span>

namespace game {

template<class Down>
[[nodiscard]] bool any_bound_button_down(std::span<const input_binding> bindings, Down&& down)
{
    for (const auto& binding : bindings)
    {
        std::uint16_t key{};
        switch (binding.device)
        {
        case input_device::keyboard: key = binding.virtual_key; break;
        case input_device::mouse_primary: key = VK_LBUTTON; break;
        case input_device::mouse_secondary: key = VK_RBUTTON; break;
        case input_device::mouse_middle: key = VK_MBUTTON; break;
        case input_device::mouse_auxiliary1: key = VK_XBUTTON1; break;
        case input_device::mouse_auxiliary2: key = VK_XBUTTON2; break;
        default: break;
        }
        if (key && down(key)) return true;
    }
    return false;
}

[[nodiscard]] inline bool physical_action_down(input_action action,
    const platform::windows::input_gateway& input)
{
    const auto bindings = input_bindings().candidates(action);
    return any_bound_button_down(bindings, [&input](std::uint16_t key) {
        return key == VK_LBUTTON && input.injected_primary_down()
            ? false : input.physical_key_down(key);
    });
}
}
