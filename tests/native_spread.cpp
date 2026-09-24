#include <simulation/spread_sampler.hpp>
#include <cstdio>
using namespace simulation::detail;
#include "fixtures/spread_native.hpp"
using native_spread_fixture::cases;
int main(){
 int mismatches=0;float max_error=0;
 auto random=[](int,int,float,int,int,foundation::vec2&){return pattern_result::random;};
 for(const auto& r:cases) for(int n=0;n<4;++n){
  spread_parameters p{r.seed,.0808f,.0032f,r.recoil,r.item,r.mode,4,bool(r.patterns),bool(r.overrides&1),bool(r.overrides&2)};
  foundation::vec2 result{};
  if(!sample_spread(p,n,random,result) || !std::isfinite(result.x) || !std::isfinite(result.y))return 2;
  const float error=std::max(std::abs(result.x-r.xy[n*2]),std::abs(result.y-r.xy[n*2+1]));
  max_error=std::max(max_error,error);
  if(error>1e-7f){if(mismatches<5)std::printf("mismatch seed=%d item=%d mode=%d patterns=%d flags=%d recoil=%g pellet=%d expected=(%g,%g) got=(%g,%g)\n",r.seed,r.item,r.mode,r.patterns,r.overrides,r.recoil,n,r.xy[2*n],r.xy[2*n+1],result.x,result.y);++mismatches;}
 }
 std::printf("native_spread pellets=4608 mismatches=%d max_error=%g\n",mismatches,max_error);
 return mismatches?1:0;
}
