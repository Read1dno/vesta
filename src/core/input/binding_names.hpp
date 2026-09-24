#pragma once
#include <core/input/bindings.hpp>
#include <windows.h>
#include <algorithm>
#include <ranges>
#include <cctype>
#include <string_view>
namespace game::binding_detail
{
[[nodiscard]] inline bool contains_command(std::string_view command, std::string_view token)
{
    if (token.empty())
        return false;
    std::size_t i{};
    while (i < command.size())
    {
        while (i < command.size() &&
               (std::isspace(static_cast<unsigned char>(command[i])) || command[i] == ';'))
            ++i;
        const bool quoted = i < command.size() && command[i] == '"';
        if (quoted)
            ++i;
        const auto start = i;
        while (i < command.size() &&
               (quoted ? command[i] != '"'
                       : !std::isspace(static_cast<unsigned char>(command[i])) && command[i] != ';'))
            ++i;
        auto word = command.substr(start, i - start);
        if (word.size() == token.size() &&
            std::equal(word.begin(), word.end(), token.begin(),
                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
            return true;
        if (quoted && i < command.size())
            ++i;
        bool in_string{};
        while (i < command.size())
        {
            const char c = command[i++];
            if (c == '\\' && i < command.size())
            {
                ++i;
                continue;
            }
            if (c == '"')
                in_string = !in_string;
            if (c == ';' && !in_string)
                break;
        }
    }
    return false;
}
[[nodiscard]] inline bool same_text(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    return std::ranges::equal(left, right, [](const char a, const char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}
[[nodiscard]] inline input_binding source_key_to_binding(std::string_view name)
{
    std::string normalized{name};
    std::ranges::transform(normalized, normalized.begin(), [](const char value) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    });

    const auto mouse = [&normalized](const input_device device) {
        return input_binding{device, 0, normalized};
    };
    if (normalized == "MOUSE1")
        return mouse(input_device::mouse_primary);
    if (normalized == "MOUSE2")
        return mouse(input_device::mouse_secondary);
    if (normalized == "MOUSE3")
        return mouse(input_device::mouse_middle);
    if (normalized == "MOUSE4")
        return mouse(input_device::mouse_auxiliary1);
    if (normalized == "MOUSE5")
        return mouse(input_device::mouse_auxiliary2);

    std::uint16_t virtual_key{};
    if (normalized.size() == 1)
    {
        const auto value = normalized.front();
        if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
        {
            virtual_key = static_cast<std::uint16_t>(value);
        }
    }
    if (!virtual_key && normalized.size() >= 2 && normalized.front() == 'F')
    {
        auto number = 0;
        for (auto index = std::size_t{1}; index < normalized.size(); ++index)
        {
            if (normalized[index] < '0' || normalized[index] > '9')
            {
                number = 0;
                break;
            }
            number = number * 10 + normalized[index] - '0';
        }
        if (number >= 1 && number <= 24)
            virtual_key = static_cast<std::uint16_t>(VK_F1 + number - 1);
    }

    static constexpr std::array named_keys{
        std::pair{std::string_view{"SPACE"}, std::uint16_t{VK_SPACE}},
        std::pair{std::string_view{"TAB"}, std::uint16_t{VK_TAB}},
        std::pair{std::string_view{"ENTER"}, std::uint16_t{VK_RETURN}},
        std::pair{std::string_view{"ESCAPE"}, std::uint16_t{VK_ESCAPE}},
        std::pair{std::string_view{"BACKSPACE"}, std::uint16_t{VK_BACK}},
        std::pair{std::string_view{"CAPSLOCK"}, std::uint16_t{VK_CAPITAL}},
        std::pair{std::string_view{"NUMLOCK"}, std::uint16_t{VK_NUMLOCK}},
        std::pair{std::string_view{"SCROLLLOCK"}, std::uint16_t{VK_SCROLL}},
        std::pair{std::string_view{"INSERT"}, std::uint16_t{VK_INSERT}},
        std::pair{std::string_view{"DELETE"}, std::uint16_t{VK_DELETE}},
        std::pair{std::string_view{"HOME"}, std::uint16_t{VK_HOME}},
        std::pair{std::string_view{"END"}, std::uint16_t{VK_END}},
        std::pair{std::string_view{"PGUP"}, std::uint16_t{VK_PRIOR}},
        std::pair{std::string_view{"PGDN"}, std::uint16_t{VK_NEXT}},
        std::pair{std::string_view{"PAUSE"}, std::uint16_t{VK_PAUSE}},
        std::pair{std::string_view{"SHIFT"}, std::uint16_t{VK_LSHIFT}},
        std::pair{std::string_view{"RSHIFT"}, std::uint16_t{VK_RSHIFT}},
        std::pair{std::string_view{"CTRL"}, std::uint16_t{VK_LCONTROL}},
        std::pair{std::string_view{"RCTRL"}, std::uint16_t{VK_RCONTROL}},
        std::pair{std::string_view{"ALT"}, std::uint16_t{VK_LMENU}},
        std::pair{std::string_view{"RALT"}, std::uint16_t{VK_RMENU}},
        std::pair{std::string_view{"UPARROW"}, std::uint16_t{VK_UP}},
        std::pair{std::string_view{"DOWNARROW"}, std::uint16_t{VK_DOWN}},
        std::pair{std::string_view{"LEFTARROW"}, std::uint16_t{VK_LEFT}},
        std::pair{std::string_view{"RIGHTARROW"}, std::uint16_t{VK_RIGHT}},
        std::pair{std::string_view{"SEMICOLON"}, std::uint16_t{VK_OEM_1}},
        std::pair{std::string_view{"SLASH"}, std::uint16_t{VK_OEM_2}},
        std::pair{std::string_view{"BACKQUOTE"}, std::uint16_t{VK_OEM_3}},
        std::pair{std::string_view{"LBRACKET"}, std::uint16_t{VK_OEM_4}},
        std::pair{std::string_view{"BACKSLASH"}, std::uint16_t{VK_OEM_5}},
        std::pair{std::string_view{"RBRACKET"}, std::uint16_t{VK_OEM_6}},
        std::pair{std::string_view{"APOSTROPHE"}, std::uint16_t{VK_OEM_7}},
        std::pair{std::string_view{"EQUAL"}, std::uint16_t{VK_OEM_PLUS}},
        std::pair{std::string_view{"COMMA"}, std::uint16_t{VK_OEM_COMMA}},
        std::pair{std::string_view{"MINUS"}, std::uint16_t{VK_OEM_MINUS}},
        std::pair{std::string_view{"PERIOD"}, std::uint16_t{VK_OEM_PERIOD}},
        std::pair{std::string_view{"KP_0"}, std::uint16_t{VK_NUMPAD0}},
        std::pair{std::string_view{"KP_1"}, std::uint16_t{VK_NUMPAD1}},
        std::pair{std::string_view{"KP_2"}, std::uint16_t{VK_NUMPAD2}},
        std::pair{std::string_view{"KP_3"}, std::uint16_t{VK_NUMPAD3}},
        std::pair{std::string_view{"KP_4"}, std::uint16_t{VK_NUMPAD4}},
        std::pair{std::string_view{"KP_5"}, std::uint16_t{VK_NUMPAD5}},
        std::pair{std::string_view{"KP_6"}, std::uint16_t{VK_NUMPAD6}},
        std::pair{std::string_view{"KP_7"}, std::uint16_t{VK_NUMPAD7}},
        std::pair{std::string_view{"KP_8"}, std::uint16_t{VK_NUMPAD8}},
        std::pair{std::string_view{"KP_9"}, std::uint16_t{VK_NUMPAD9}},
        std::pair{std::string_view{"KP_DEL"}, std::uint16_t{VK_DECIMAL}},
        std::pair{std::string_view{"KP_DIVIDE"}, std::uint16_t{VK_DIVIDE}},
        std::pair{std::string_view{"KP_MULTIPLY"}, std::uint16_t{VK_MULTIPLY}},
        std::pair{std::string_view{"KP_MINUS"}, std::uint16_t{VK_SUBTRACT}},
        std::pair{std::string_view{"KP_PLUS"}, std::uint16_t{VK_ADD}},
    };
    if (!virtual_key)
    {
        for (const auto &[source, key] : named_keys)
        {
            if (normalized == source)
            {
                virtual_key = key;
                break;
            }
        }
    }
    return virtual_key ? input_binding{input_device::keyboard, virtual_key, normalized} : input_binding{};
}
} // namespace game::binding_detail
