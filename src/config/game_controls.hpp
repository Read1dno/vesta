#pragma once
#include <windows.h>

namespace config
{

struct game_controls
{
    int forward{'W'};
    int back{'S'};
    int left{'A'};
    int right{'D'};
    int walk{VK_SHIFT};
    int duck{VK_CONTROL};
    int jump{VK_SPACE};
    int attack{VK_LBUTTON};
    int attack2{VK_RBUTTON};
    int use{'E'}; // doors
    int reload{'R'};
    int buy_menu{'B'};
};

} // namespace config
