#include "test_support.hpp"
#include <core/input/manual_bindings.hpp>
#include <features/misc/input_lifecycle.hpp>

int main()
{
    using namespace game;
    config::game_controls controls{};
    VESTA_CHECK(configured_binding(input_action::forward, controls).virtual_key == 'W');
    VESTA_CHECK(configured_binding(input_action::back, controls).virtual_key == 'S');
    VESTA_CHECK(configured_binding(input_action::left, controls).virtual_key == 'A');
    VESTA_CHECK(configured_binding(input_action::right, controls).virtual_key == 'D');
    VESTA_CHECK(configured_binding(input_action::jump, controls).virtual_key == VK_SPACE);
    const int activation = 'C';
    const auto physical = [](std::uint16_t key) { return key == 'C'; };
    VESTA_CHECK(features::misc::input_lifecycle::activation_held(activation, physical));
    VESTA_CHECK(configured_binding(input_action::jump, controls).virtual_key != activation);
    VESTA_CHECK(configured_binding(input_action::attack, controls).device
        == input_device::mouse_primary);
    VESTA_CHECK(configured_binding(input_action::attack2, controls).device
        == input_device::mouse_secondary);
    controls.jump = 'J';
    controls.attack = 'Q';
    VESTA_CHECK(configured_binding(input_action::jump, controls).virtual_key == 'J');
    VESTA_CHECK(configured_binding(input_action::attack, controls).virtual_key == 'Q');
    controls.jump = VK_XBUTTON1;
    VESTA_CHECK(!configured_binding(input_action::jump, controls));
    controls.jump = 0;
    VESTA_CHECK(!configured_binding(input_action::jump, controls));
    controls.jump = 300;
    VESTA_CHECK(!configured_binding(input_action::jump, controls));
    VESTA_CHECK(!configured_binding(input_action::count, controls));
}
