#include <numbers>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <simulation/inaccuracy_model.hpp>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 std::ifstream in(argv[1]); unsigned cases{},bad{}; float worst{};
 for(;;){simulation::shot_model::accuracy_input p;float expected;int walk,ground,no,strafe;
 if(!(in>>p.velocity.x>>p.velocity.y>>p.velocity.z>>p.view_angles.x>>p.view_angles.y>>p.view_angles.z>>p.max_speed>>p.move>>p.penalty>>p.turning>>p.jump_initial>>p.jump_apex>>p.air_scale>>p.jump_impulse>>p.forcespread>>p.strafe_bias>>p.strafe_scale>>walk>>ground>>no>>strafe>>expected)){if(!in.eof()){std::puts("Malformed native fixture");return 2;}break;}
 p.walking=walk;p.grounded=ground;p.nospread=no;p.strafing=strafe;
 simulation::shot_model::accuracy_result result;bool valid=simulation::shot_model::accuracy(p,result);++cases;
 float delta=std::abs(result.total-expected);worst=std::max(worst,delta);
 if(!valid||delta>2e-6f){if(bad<12)std::printf("FAIL case=%u expected=%.9g actual=%.9g delta=%.9g\n",cases,expected,result.total,delta);++bad;}
 }
 std::printf("NATIVE_ACCURACY_COMPARE cases=%u failed=%u max_abs_error=%.9g\n",cases,bad,worst);return cases!=8192||bad?1:0;
}
