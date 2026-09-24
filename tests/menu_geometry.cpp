#include "test_support.hpp"
#include <render/menu/geometry.hpp>

int main()
{
    using namespace render::menu;
    const auto closed = chevron(0), opened = chevron(1);
    VESTA_CHECK(closed[1].y > closed[0].y);
    VESTA_CHECK(opened[1].y < opened[0].y);
    for (int i = 0; i <= 100; ++i)
    {
        const auto p = chevron(i / 100.0f);
        const auto dx = p[1].x - p[0].x, dy = p[1].y - p[0].y;
        VESTA_CHECK(std::abs(dx * dx + dy * dy - 32.0f) < 0.0001f);
    }
    VESTA_CHECK(fit_popup(200, 8, -40) == 8);
    VESTA_CHECK(fit_popup(200, 8, 100) == 100);
}
