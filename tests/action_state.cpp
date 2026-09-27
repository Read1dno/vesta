#include "test_support.hpp"
#include <core/input/action_state.hpp>
#include <array>
int main()
{
    using namespace game;
    const std::array custom{
        input_binding{input_device::keyboard, static_cast<std::uint16_t>('Q'), "Q"},
        input_binding{input_device::mouse_auxiliary1, 0, "MOUSE4"}};
    VESTA_CHECK(!any_bound_button_down(custom, [](std::uint16_t){return false;}));
    VESTA_CHECK(any_bound_button_down(custom, [](std::uint16_t k){return k=='Q';}));
    VESTA_CHECK(any_bound_button_down(custom, [](std::uint16_t k){return k==VK_XBUTTON1;}));
    VESTA_CHECK(!any_bound_button_down(std::span<const input_binding>{},
        [](std::uint16_t){return true;}));
}
