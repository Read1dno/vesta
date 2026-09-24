#include "test_support.hpp"
#include <simulation/spread_sampler.hpp>
using namespace simulation::detail;
int main()
{
    spread_parameters p{1337,.01f,.02f,0,7,0,1,true};
    foundation::vec2 actual{};
    const auto unavailable=[](int,int,float,int,int,foundation::vec2&) {
        VESTA_CHECK(false);return pattern_result::unavailable;
    };
    VESTA_CHECK(sample_spread(p,0,unavailable,actual));
    foundation::source_random rng;rng.seed(p.seed);
    const auto ir=rng.uniform(),ia=rng.uniform(0,2*std::numbers::pi_v<float>);
    const auto sa=rng.uniform(0,2*std::numbers::pi_v<float>),sr=rng.uniform();
    VESTA_CHECK(std::abs(actual.x-(std::cos(ia)*ir*p.inaccuracy+std::cos(sa)*sr*p.spread))<1e-7f);
    VESTA_CHECK(make_shotgun_pattern(0,1).random);
    for(int seed:{0,1,123,1337,31415}) for(int count:{2,6,8,12,32,64}) {
        const auto table=make_shotgun_pattern(seed,count);
        VESTA_CHECK(!table.random);
        foundation::source_random reference;reference.seed(seed);
        for(int i=0;i<64;++i) {
            const auto angle=reference.uniform(0,2*std::numbers::pi_v<float>);
            const auto step=1.0f/float(count);
            const auto lo=float(i%count)*step,hi=float(i%count+1)*step;
            const auto radius=reference.uniform(lo,hi);
            VESTA_CHECK(table.angle_radius[i].x==angle && table.angle_radius[i].y==radius);
            VESTA_CHECK(radius>=lo && radius<=hi);
        }
        foundation::vec2 value{};
        VESTA_CHECK(table.sample(0,count,0,value)==pattern_result::value);
        VESTA_CHECK(table.sample(64,count,0,value)==pattern_result::random);
    }
    std::cout<<"spread_cache: single-bullet cold start and 1920 generated pattern entries PASS\n";
}
