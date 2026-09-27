#pragma once

#include <config/game_controls.hpp>
#include <core/input/bindings.hpp>
#include <windows.h>
#include <string>

namespace game {

[[nodiscard]] inline int configured_key(input_action action,
    const config::game_controls& keys) noexcept
{
    switch (action)
    {
    case input_action::forward: return keys.forward;
    case input_action::back: return keys.back;
    case input_action::left: return keys.left;
    case input_action::right: return keys.right;
    case input_action::walk: return keys.walk;
    case input_action::duck: return keys.duck;
    case input_action::jump: return keys.jump;
    case input_action::attack: return keys.attack;
    case input_action::attack2: return keys.attack2;
    default: return 0;
    }
}

[[nodiscard]] inline bool mouse_key(int key) noexcept
{
    return key == VK_LBUTTON || key == VK_RBUTTON
        || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2;
}

[[nodiscard]] inline bool valid_game_control(input_action action, int key) noexcept
{
    if (action >= input_action::count || key <= 0 || key >= 256 || key == VK_ESCAPE)
        return false;
    if (action <= input_action::jump && mouse_key(key))
        return false;
    return true;
}

[[nodiscard]] inline input_binding configured_binding(input_action action,
    const config::game_controls& keys)
{
    const auto key = configured_key(action, keys);
    if (!valid_game_control(action, key))
        return {};
    switch (key)
    {
    case VK_LBUTTON: return {input_device::mouse_primary, 0, "MOUSE1"};
    case VK_RBUTTON: return {input_device::mouse_secondary, 0, "MOUSE2"};
    case VK_MBUTTON: return {input_device::mouse_middle, 0, "MOUSE3"};
    case VK_XBUTTON1: return {input_device::mouse_auxiliary1, 0, "MOUSE4"};
    case VK_XBUTTON2: return {input_device::mouse_auxiliary2, 0, "MOUSE5"};
    default:
        if (key == VK_SPACE)
            return {input_device::keyboard, VK_SPACE, "SPACE"};
        if (key == VK_SHIFT || key == VK_LSHIFT)
            return {input_device::keyboard, static_cast<std::uint16_t>(key), "SHIFT"};
        if (key == VK_CONTROL || key == VK_LCONTROL)
            return {input_device::keyboard, static_cast<std::uint16_t>(key), "CTRL"};
        if (key == VK_RSHIFT)
            return {input_device::keyboard, VK_RSHIFT, "RSHIFT"};
        if (key == VK_RCONTROL)
            return {input_device::keyboard, VK_RCONTROL, "RCTRL"};
        if (key >= 'A' && key <= 'Z')
            return {input_device::keyboard, static_cast<std::uint16_t>(key),
                std::string(1, static_cast<char>(key))};
        return {input_device::keyboard, static_cast<std::uint16_t>(key),
            "VK_" + std::to_string(key)};
    }
}

} // namespace game
