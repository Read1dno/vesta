#include "test_support.hpp"
#include <simulation/spread_sampler.hpp>

using namespace simulation::detail;
using foundation::vec2;
vec2 reference(const spread_parameters& p, int pellet, bool fallback = false) {
    foundation::source_random rng; rng.seed(p.seed);
    float ir{},ia{},sr{},sa{};
    const auto adjust = [&](float r) {
        if (p.item==64 && p.mode==1) r=1-r*r;
        else if(p.item==28 && p.recoil_index<3) {
            for(int n=3; float(--n)>p.recoil_index;) r*=r;
            r=1-r*r;
        }
        return r;
    };
    for(int i=0;i<=pellet;++i) {
        if(i==0 || p.patterns) {ir=adjust(rng.uniform());ia=rng.uniform(0,2*std::numbers::pi_v<float>);}
        if(p.patterns && !fallback) {sr=.25f;sa=1.0f;}
        else if(fallback) {sa=rng.uniform(0,2*std::numbers::pi_v<float>);sr=rng.uniform();}
        else {sr=rng.uniform();sa=rng.uniform(0,2*std::numbers::pi_v<float>);}
        sr=adjust(sr);
    }
    return {std::cos(sa)*sr*p.spread+std::cos(ia)*ir*p.inaccuracy,
        std::sin(sa)*sr*p.spread+std::sin(ia)*ir*p.inaccuracy};
}
int main() {
    shotgun_pattern table{};
    table.angle_radius[0]={1.0928899049758911f,.14298434555530548f};
    vec2 polar{};
    VESTA_CHECK(table.sample(0,8,0,polar)==pattern_result::value);
    VESTA_CHECK(polar.x==table.angle_radius[0].y && polar.y==table.angle_radius[0].x);
    VESTA_CHECK(table.sample(8,8,0,polar)==pattern_result::random);
    table.random=true;VESTA_CHECK(table.sample(0,8,0,polar)==pattern_result::random);

    auto values=[](int,int,float,int,int,vec2& v){v={.25f,1.0f};return pattern_result::value;};
    auto random=[](int,int,float,int,int,vec2&){return pattern_result::random;};
    auto missing=[](int,int,float,int,int,vec2&){return pattern_result::unavailable;};
    vec2 out{};
    spread_parameters p{1337,.01f,.02f,5,7,0,8};
    VESTA_CHECK(sample_spread(p,0,missing,out));
    VESTA_CHECK(std::abs(out.x-.008576300f)<1e-7f && std::abs(out.y+.002788435f)<1e-7f);
    VESTA_CHECK(sample_spread(p,1,missing,out));
    VESTA_CHECK(std::abs(out.x-.002691923f)<1e-7f && std::abs(out.y-.008687822f)<1e-7f);
    for(int seed=1;seed<=256;++seed) for(int item: {7,28,64}) for(float recoil:{0.0f,1.0f,2.5f,5.0f})
        for(int branch=0;branch<3;++branch) for(int pellet=0;pellet<8;++pellet) {
            p={seed,.01f,.02f,recoil,item,1,8,branch!=0};
            VESTA_CHECK(sample_spread(p,pellet,branch==2?random:values,out));
            const auto expected=reference(p,pellet,branch==2);
            VESTA_CHECK(std::abs(out.x-expected.x)<1e-7f && std::abs(out.y-expected.y)<1e-7f);
        }
    p={1337,.01f,.02f,5,7,0,8,true};
    VESTA_CHECK(!sample_spread(p,0,missing,out));
    p.patterns=false;p.force_radius=true;p.force_angle=true;
    VESTA_CHECK(sample_spread(p,0,values,out));
    VESTA_CHECK(std::abs(out.x)<1e-7f && std::abs(out.y-.03f)<1e-7f);
    VESTA_CHECK(!sample_spread(p,-1,values,out));
    VESTA_CHECK(!sample_spread(p,8,values,out));
    p.spread=NAN;VESTA_CHECK(!sample_spread(p,0,values,out));
    std::cout << "spread_sampler: 73728 reference cases plus golden vectors/overrides/errors PASS\n";
}
