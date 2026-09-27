#include "test_support.hpp"
#include <features/visuals/radar_geometry.hpp>
#include <render/chams/effect_bounds.hpp>
#include <render/depth_bounds.hpp>
#include <array>
#include <cmath>
#include <random>

struct vec { float x{}, y{}, z{}; };
struct bounds { vec mins, maxs; };
using matrix = std::array<std::array<float, 4>, 4>;

int main()
{
    using features::visuals::detail::square_radar_layout;
    for (float sx : {0.5f, 1.0f, 4.0f/3.0f, 2.0f})
        for (float sy : {0.5f, 1.0f, 1.5f, 2.0f}) {
            const float width=300*sx, height=300*sy;
            VESTA_CHECK(square_radar_layout(width/sx,height/sy));
        }
    VESTA_CHECK(!square_radar_layout(300*4.0f/3.0f,300));
    VESTA_CHECK(!square_radar_layout(600,300));
    VESTA_CHECK(!square_radar_layout(300,0));
    VESTA_CHECK(!square_radar_layout(NAN,300));
    VESTA_CHECK(!square_radar_layout(300,INFINITY));

    std::mt19937 random(0x7133);
    std::uniform_real_distribution<float> unit(0,1), position(-1000,1000);
    for (int i=0;i<100000;++i) {
        const bounds initial{{position(random),position(random),position(random)}, {}};
        auto original=initial;
        original.maxs={initial.mins.x+90,initial.mins.y+90,initial.mins.z+100};
        const float p=unit(random), t=p*p, radial=24+52*unit(random);
        const float angle=6.2831853f*unit(random), vertical=0.25f+0.85f*unit(random);
        const auto expanded=chams::detail::death_effect_bounds(original,p);
        std::array<vec,3> triangle{};
        vec center{};
        for(auto& vertex:triangle) {
            vertex={original.mins.x+90*unit(random),original.mins.y+90*unit(random),original.mins.z+100*unit(random)};
            center.x+=vertex.x/3;center.y+=vertex.y/3;center.z+=vertex.z/3;
        }
        for(auto vertex:triangle) {
            const vec moved{
                center.x+(vertex.x-center.x)*(1-0.82f*p)+std::cos(angle)*radial*t,
                center.y+(vertex.y-center.y)*(1-0.82f*p)+std::sin(angle)*radial*t,
                center.z+(vertex.z-center.z)*(1-0.82f*p)+(vertical*radial-58)*t};
            VESTA_CHECK(moved.x>=expanded.mins.x-0.001f && moved.x<=expanded.maxs.x+0.001f);
            VESTA_CHECK(moved.y>=expanded.mins.y-0.001f && moved.y<=expanded.maxs.y+0.001f);
            VESTA_CHECK(moved.z>=expanded.mins.z-0.001f && moved.z<=expanded.maxs.z+0.001f);
        }
    }
    matrix m{};
    m[0][0]=m[1][1]=m[2][2]=1;m[3][2]=1;
    const bounds crossing{{-2,-2,-1},{2,2,1}};
    const auto projected=render::project_depth_bounds(crossing,m);
    VESTA_CHECK(projected.valid && projected.crosses_near);
    VESTA_CHECK(projected.min_x==-1 && projected.min_y==-1 && projected.max_x==1 && projected.max_y==1);
    VESTA_CHECK(!render::project_depth_bounds(bounds{{-2,-2,-4},{2,2,-2}},m).valid);
    const auto independent_effect=render::project_depth_bounds(bounds{{20,20,40},{40,40,60}},m);
    const auto live_player=render::project_depth_bounds(bounds{{-20,-20,40},{-10,-10,60}},m);
    VESTA_CHECK(independent_effect.min_x>live_player.max_x);
    VESTA_CHECK(independent_effect.min_y>live_player.max_y);
    std::cout << "visual_geometry: anisotropic radar layout, 300000 shatter vertices, near-plane and independent effect bounds PASS\n";
}
