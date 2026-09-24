#include <core/input/keyboard_binding.hpp>
#include <array>
#include <cstdio>

int main()
{
	using game::input_binding;
	using game::input_device;
	const std::array mixed{input_binding{input_device::mouse_primary, 0, "MOUSE1"},
	                       input_binding{input_device::keyboard, 0x86, "F23"},
	                       input_binding{input_device::keyboard, 'J', "J"},
	                       input_binding{input_device::keyboard, 'K', "K"}};
	if (game::keyboard_binding(mixed, 0x20).virtual_key != 'J')
		return 1;
	if (game::keyboard_binding(mixed, 'K').virtual_key != 'K')
		return 2;
	if (game::keyboard_binding(mixed, 0x86).virtual_key != 0x86)
		return 3;
	if (game::keyboard_binding({mixed.data(), 1}))
		return 4;
	if (game::keyboard_binding({}))
		return 5;
	const std::array normal{input_binding{input_device::keyboard, 0x20, "SPACE"}};
	if (game::keyboard_binding(normal).virtual_key != 0x20)
		return 6;
	const std::array auxiliary{input_binding{input_device::keyboard, 0x87, "F24"}};
	if (game::keyboard_binding(auxiliary).virtual_key != 0x87)
		return 7;
	std::puts(
	    "PASS keyboard_binding mixed devices, preferred, custom jump, reserved keys, empty and default");
}
