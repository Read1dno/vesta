#include <simulation/recoil_model.hpp>
#include <simulation/spread_sampler.hpp>
#include <external/json.hpp>
#include <fstream>
#include <iostream>
using nlohmann::json;using foundation::vec3;
vec3 vec(const json& j){return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};}
int main(int argc,char**argv){
 unsigned rays{},ray_bad{};float ray_max{};
 unsigned hashes{},hash_bad{},spreads{},spread_bad{},punches{},punch_bad{},tails{},tail_bad{},tail_seed_bad{};float spread_max{},punch_max{},tail_max{};
 for(int i=1;i<argc;++i){std::ifstream in(argv[i]);if(!in)return 2;
 for(std::string line;std::getline(in,line);){auto j=json::parse(line);const auto event=j.at("event").get<std::string>();
 if(event=="ray"){
 const auto angle=vec(j.at("angles"));vec3 forward{},right{},up{};angle.to_directions(&forward,&right,&up);
 const auto actual=forward+right*j.at("spread_xy")[0].get<float>()+up*j.at("spread_xy")[1].get<float>();
 const auto expected=vec(j.at("direction_raw"));const float error=simulation::shot_model::length(actual.normalized()-expected.normalized());
 ray_max=std::max(ray_max,error);++rays;if(error>2e-6f)++ray_bad;
 }
 if(event=="seed"){++hashes;if(simulation::shot_model::seed(vec(j.at("angles")),j.at("tick").get<int>())!=j.at("seed").get<std::uint32_t>())++hash_bad;}
 if(event=="spread" && j.contains("patterns"))for(int pellet=0;pellet<j.at("bullets").get<int>();++pellet){
 simulation::detail::spread_parameters p{static_cast<int>(j.at("seed").get<std::uint32_t>()),j.at("inaccuracy"),j.at("spread"),j.at("recoil"),j.at("item"),j.at("mode"),j.at("bullets"),bool(j.at("patterns").get<int>()),bool(j.at("force_radius").get<int>()),bool(j.at("force_angle").get<int>())};
 if(p.bullets>1&&p.patterns)continue;
 foundation::vec2 actual{};auto random=[](int,int,float,int,int,foundation::vec2&){return simulation::detail::pattern_result::random;};
 const bool okay=simulation::detail::sample_spread(p,pellet,random,actual);const auto& xy=j.at("xy")[pellet];float error=std::max(std::abs(actual.x-xy[0].get<float>()),std::abs(actual.y-xy[1].get<float>()));spread_max=std::max(spread_max,error);++spreads;if(!okay||error>1e-7f)++spread_bad;
 }
 if(event=="punch"&&j.contains("state")){
 vec3 actual{};for(const auto& state:j.at("state")){simulation::shot_model::recoil_cache cache;vec3 v{};simulation::shot_model::recoil_state s{{state.at("tick"),state.at("fraction")},vec(state.at("angle")),vec(state.at("velocity"))};if(!cache.sample(s,{j.at("time").at("tick"),j.at("time").at("fraction")},v))return 3;actual+=v;}
 float error=simulation::shot_model::length(actual-vec(j.at("value")));punch_max=std::max(punch_max,error);++punches;if(error>1e-4f){++punch_bad;std::cout<<"PUNCH_FAIL "<<line<<"\n";}
 }
 if(event=="shot_state"&&j.at("before").contains("punch_cache")){
 vec3 tail{};for(const auto& track:j.at("before").at("punch_cache"))tail+=vec(track.at("count").get<int>()?track.at("tail"):track.at("base"));tail*=2;
 float error=simulation::shot_model::length(tail-vec(j.at("punch")));tail_max=std::max(tail_max,error);++tails;if(error>1e-4f)++tail_bad;
 if(simulation::shot_model::seed(vec(j.at("before").at("view"))+tail,j.at("tick"))!=simulation::shot_model::seed(vec(j.at("angles")),j.at("tick")))++tail_seed_bad;
 }
 }}
 std::cout<<"LIVE_RAYS samples="<<rays<<" failed="<<ray_bad<<" max_direction_error="<<ray_max<<"\n";
 std::cout<<"LIVE_REPLAY hashes="<<hashes<<" hash_failed="<<hash_bad<<" spreads="<<spreads<<" spread_failed="<<spread_bad<<" spread_max="<<spread_max<<" punches="<<punches<<" punch_failed="<<punch_bad<<" punch_max="<<punch_max<<"\n";
 std::cout<<"BASELINE_CACHE_TAIL shots="<<tails<<" punch_mismatch="<<tail_bad<<" seed_mismatch="<<tail_seed_bad<<" max_angle_error="<<tail_max<<"\n";
 return hashes==0||spreads==0||punches==0||rays==0||ray_bad||hash_bad||spread_bad||punch_bad?1:0;
}
