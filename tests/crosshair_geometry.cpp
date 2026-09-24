#include "test_support.hpp"
#include <features/visuals/crosshair_geometry.hpp>
using namespace features::visuals::detail;
int main()
{
    VESTA_CHECK(scale_crosshair_parameter(2,1080,1080)==2);
    VESTA_CHECK(scale_crosshair_parameter(2,720,1080)==1);
    VESTA_CHECK(scale_crosshair_parameter(0,1440,1080)==0);
    VESTA_CHECK(scale_crosshair_parameter(-2,1440,1080)==-2);
    VESTA_CHECK(scale_crosshair_parameter(1,1440,0)==0);
    const auto odd=native_static_crosshair(1920,1080,1,2,1,false,false);
    VESTA_CHECK(odd.count==4);
    VESTA_CHECK(odd.pieces[0].x0==957 && odd.pieces[0].x1==959);
    VESTA_CHECK(odd.pieces[0].y0==539 && odd.pieces[0].y1==540);
    VESTA_CHECK(odd.pieces[1].x0==960 && odd.pieces[1].x1==962);
    const auto even=native_static_crosshair(1920,1080,4,8,2,true,false);
    VESTA_CHECK(even.count==5);
    VESTA_CHECK(even.pieces[0].x0==959 && even.pieces[0].x1==961);
    VESTA_CHECK(native_static_crosshair(1920,1080,4,8,2,true,true).count==4);
    VESTA_CHECK(native_static_crosshair(1920,1080,4,8,2,false,false,true).count==1);
    VESTA_CHECK(native_static_crosshair(1920,1080,4,8,0,true,false).count==0);
    VESTA_CHECK(native_static_crosshair(NAN,1080,4,8,2,true,false).count==0);
    for(float width:{1280,1440,1920,2560})
        for(float height:{720,1080,1440})
            for(float thickness:{1,2,3,4,5}) {
                const auto shape=native_static_crosshair(width,height,3,7,thickness,true,false);
                VESTA_CHECK(shape.count==5);
                for(std::size_t i=0;i<shape.count;++i) {
                    const auto& p=shape.pieces[i];
                    VESTA_CHECK(p.x1>p.x0 && p.y1>p.y0);
                }
            }
    std::cout<<"crosshair_geometry: new scaling, native parity, dot/T modes, bounds PASS\n";
}
