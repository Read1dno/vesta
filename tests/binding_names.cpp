#include "test_support.hpp"
#include <core/input/binding_names.hpp>
int main()
{
    using namespace game::binding_detail;
    VESTA_CHECK(contains_command("+jump; +duck", "+jump"));
    VESTA_CHECK(contains_command("  +JUMP", "+jump"));
    VESTA_CHECK(contains_command("echo x; +jump", "+jump"));
    VESTA_CHECK(!contains_command("say \"+jump; +duck\"", "+jump"));
    VESTA_CHECK(!contains_command("+jumpbug", "+jump"));
    VESTA_CHECK(!contains_command("alias jump +jump", "+jump"));
    VESTA_CHECK(source_key_to_binding("space").virtual_key == VK_SPACE);
    VESTA_CHECK(source_key_to_binding("RCTRL").virtual_key == VK_RCONTROL);
    VESTA_CHECK(source_key_to_binding("UPARROW").virtual_key == VK_UP);
    VESTA_CHECK(source_key_to_binding("F24").virtual_key == VK_F24);
    VESTA_CHECK(!source_key_to_binding("F25"));
    VESTA_CHECK(!source_key_to_binding("MWHEELUP"));
    VESTA_CHECK(source_key_to_binding("MOUSE1").device == game::input_device::mouse_primary);
}
