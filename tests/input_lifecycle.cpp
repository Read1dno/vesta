#include "test_support.hpp"
#include <features/misc/input_lifecycle.hpp>
#include <array>
#include <iostream>

int main()
{
    using namespace features::misc::input_lifecycle;
    using game::input_device;
    owned_control jump{{input_device::keyboard, 0x20, "SPACE"}, false};
    int sends{};
    bool accept = true;
    const auto send = [&](const game::input_binding& binding, const bool down) {
        ++sends;
        VESTA_CHECK(binding.virtual_key == 0x20);
        return accept;
    };
    VESTA_CHECK(transition(jump, true, send));
    VESTA_CHECK(jump.pressed && sends == 1);
    VESTA_CHECK(transition(jump, true, send) && sends == 1);
    accept = false;
    VESTA_CHECK(!transition(jump, false, send));
    VESTA_CHECK(jump.pressed && sends == 2);
    owned_control idle{{input_device::keyboard, 'W', "W"}, false};
    const std::array controls{&jump, &idle};
    VESTA_CHECK(!discard_released(controls));
    VESTA_CHECK(jump.pressed && jump.binding.virtual_key == 0x20);
    VESTA_CHECK(!idle.binding && !idle.pressed);
    accept = true;
    VESTA_CHECK(transition(jump, false, send));
    VESTA_CHECK(!jump.pressed && sends == 3);
    VESTA_CHECK(discard_released(controls));
    VESTA_CHECK(!jump.binding && !idle.binding);
    VESTA_CHECK(!transition(jump, true, send) && sends == 3);
    VESTA_CHECK(transition(jump, false, send) && sends == 3);

    owned_control mouse{{input_device::mouse_secondary, 0, "MOUSE2"}, false};
    const auto pointer = [](const game::input_binding& binding, bool) {
        return binding.device == input_device::mouse_secondary;
    };
    VESTA_CHECK(transition(mouse, true, pointer) && mouse.pressed);
    VESTA_CHECK(transition(mouse, false, pointer) && !mouse.pressed);

    std::array<bool, 256> physical{};
    physical['W'] = true;
    int reads{};
    const auto physical_state = [&](const std::uint16_t key) {
        ++reads;
        return physical[key];
    };
    VESTA_CHECK(activation_held('W', physical_state));
    VESTA_CHECK(!activation_held('J', physical_state));
    physical['W'] = false;
    VESTA_CHECK(!activation_held('W', physical_state));
    for (const int invalid : {-65536, -1, 0, 256, 65536})
        VESTA_CHECK(!activation_held(invalid, physical_state));
    VESTA_CHECK(reads == 3);
    std::cout << "input_lifecycle: release retry, ownership retention, idempotence, physical activation and invalid keys PASS\n";
}
