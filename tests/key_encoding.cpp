#include "test_support.hpp"
#include <core/input/key_encoding.hpp>
int main()
{
    using platform::windows::encode_key;
    const auto space = encode_key(VK_SPACE, true);
    VESTA_CHECK(space.wVk == VK_SPACE && space.wScan != 0 && space.dwFlags == 0);
    for (auto key : {VK_RCONTROL, VK_RMENU, VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_UP, VK_DOWN, VK_LEFT,
                     VK_RIGHT, VK_PRIOR, VK_NEXT, VK_DIVIDE, VK_NUMLOCK})
    {
        const auto down = encode_key(static_cast<std::uint16_t>(key), true);
        const auto up = encode_key(static_cast<std::uint16_t>(key), false);
        VESTA_CHECK((down.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0);
        VESTA_CHECK(up.dwFlags == (down.dwFlags | KEYEVENTF_KEYUP));
        VESTA_CHECK(up.wScan == down.wScan);
    }
}
