#include "test_support.hpp"
#include <core/input/binding_layout.hpp>
#include <array>
int main()
{
    using game::binding_detail::select_layout_bias;
    std::array<std::uint16_t, 17> matches{};
    VESTA_CHECK(!select_layout_bias(matches, -8));
    for (int duplicate = 0; duplicate < 11; ++duplicate) matches[8] |= 1;
    VESTA_CHECK(!select_layout_bias(matches, -8));
    matches[6] = 0x0f;
    VESTA_CHECK(select_layout_bias(matches, -8) == -2);
    matches[7] = 0x0f;
    VESTA_CHECK(!select_layout_bias(matches, -8));
    matches[7] = 0x1f;
    VESTA_CHECK(select_layout_bias(matches, -8) == -1);
    matches.fill(0); matches[8] = 0x07;
    VESTA_CHECK(!select_layout_bias(matches, -8));
}
