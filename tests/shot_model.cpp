#include "test_support.hpp"
#include "fixtures/shooting_native.hpp"
#include <simulation/inaccuracy_model.hpp>
using namespace simulation::shot_model;
int main() {
    for(const auto& f:native_fixture::hashes) VESTA_CHECK(seed(f.angle,f.tick)==f.seed);
    VESTA_CHECK(quantize(180.0f)==180.0f);
    VESTA_CHECK(quantize(-180.0f)==-180.0f);
    VESTA_CHECK(quantize(540.0f)==-180.0f);
    VESTA_CHECK(seed(angles({0,27,0},{1.3f,.6f,0}),12345)==3840088470u);
    recoil_cache cache; float maximum_error{};
    for(const auto& f:native_fixture::recoils) {
        const int whole=static_cast<int>(f.offset);
        shot_time target{1000+whole,.25f+f.offset-float(whole)};
        if(target.fraction>=1){++target.tick;--target.fraction;}
        vec3 value{};
        VESTA_CHECK(cache.sample({{1000,.25f},f.angle,f.velocity},target,value));
        const float error=length(value-f.expected);
        maximum_error=std::max(maximum_error,error);
        if(error>0.0001f) std::cerr<<"recoil mismatch offset="<<f.offset<<" got="<<value.x<<","<<value.y<<","<<value.z<<" expected="<<f.expected.x<<","<<f.expected.y<<","<<f.expected.z<<" error="<<error<<"\n";
        VESTA_CHECK(error<=0.0001f);
    }
    vec3 result{};
    VESTA_CHECK(!cache.sample({{1,0},{NAN,0,0},{}},{2,0},result));
    VESTA_CHECK(!cache.sample({}, {2,NAN},result));
    accuracy_input input{}; input.max_speed=250;input.grounded=true;input.penalty=.01f;
    accuracy_result accuracy_out{};
    VESTA_CHECK(accuracy(input,accuracy_out) && accuracy_out.total==.01f);
    input.nospread=true;VESTA_CHECK(accuracy(input,accuracy_out)&&accuracy_out.total==0);
    input.forcespread=2;VESTA_CHECK(accuracy(input,accuracy_out)&&accuracy_out.total==1);
    input.forcespread=0;input.nospread=false;
    input.strafing=true;input.velocity={150,200,0};
    VESTA_CHECK(accuracy(input,accuracy_out));
    VESTA_CHECK(std::abs(accuracy_out.strafe-.04f)<1e-7f);
    input.strafe_bias=.25f;VESTA_CHECK(accuracy(input,accuracy_out));
    VESTA_CHECK(std::abs(accuracy_out.strafe-(.4f/2.2f)*.1f)<1e-7f);
    input.strafing=false;input.grounded=false;input.jump_initial=.2f;input.jump_apex=.1f;input.air_scale=2;
    input.jump_impulse=400;input.velocity={0,0,400};
    VESTA_CHECK(accuracy(input,accuracy_out)&&std::abs(accuracy_out.air-.4f)<1e-6f);
    input.air_scale=NAN;VESTA_CHECK(!accuracy(input,accuracy_out));
    std::cout<<"shot_model: native hash=256 recoil=936 max_angle_error="<<maximum_error<<" accuracy contracts PASS\n";
}
