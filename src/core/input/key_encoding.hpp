#pragma once
#include <windows.h>
#include <cstdint>
namespace platform::windows
{
[[nodiscard]] inline KEYBDINPUT encode_key(std::uint16_t key, bool pressed) noexcept
{
    const auto scan = ::MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    KEYBDINPUT result{};
    result.wVk = key;
    result.wScan = static_cast<WORD>(scan & 0xff);
    result.dwFlags = pressed ? 0u : KEYEVENTF_KEYUP;
    const bool extended = key == VK_RCONTROL || key == VK_RMENU || key == VK_INSERT || key == VK_DELETE ||
                          key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT ||
                          key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
                          key == VK_DIVIDE || key == VK_NUMLOCK || key == VK_SNAPSHOT || key == VK_LWIN ||
                          key == VK_RWIN || key == VK_APPS;
    // Navigation VKs may map to the keypad scan code without an E0 prefix.
    if (extended || (scan & 0xff00) == 0xe000)
        result.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    return result;
}
} // namespace platform::windows
