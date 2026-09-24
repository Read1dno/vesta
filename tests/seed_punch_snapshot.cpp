#include <stdafx.hpp>
#include <features/trigger/seed_state.hpp>
#include <cstdio>
namespace {
constexpr std::uintptr_t pawn=0x10000,services=0x20000,a=0x30000,b=0x40000;
struct sample_vector{std::int32_t size,padding;std::uintptr_t data;std::int32_t capacity;std::uint32_t flags;};
void setup(bool cached){
 fixture::memory.clear();fixture::calls=fixture::fail_call=fixture::mutate_call=0;
 fixture::put(pawn+0x100,services);
 for(unsigned i=0;i<0x100;++i)fixture::memory[services+i]=std::byte{};
 fixture::put(services+0x50,foundation::vec3{1,2,0});fixture::put(services+0xa4,foundation::vec3{.5f,-.25f,0});
 if(cached){
  fixture::put(services+0x88,sample_vector{1,0,a,1,0});fixture::put(services+0xd0,sample_vector{1,0,b,1,0});
  fixture::put(a,foundation::vec3{.25f,1,0});fixture::put(b,foundation::vec3{.5f,-.25f,0});
 }
}
}
int main(){
 unsigned cases{},failed{};
 auto check=[&](bool result,const char* name){++cases;if(!result){++failed;std::printf("FAIL %s\n",name);}};
 for(bool cached:{false,true}){
  setup(cached);auto result=features::aimbot::read_seed_punch(pawn);const auto reads=fixture::calls;
  const foundation::vec3 expected=cached?foundation::vec3{1.5f,1.5f,0}:foundation::vec3{3,3.5f,0};
  check(result&&*result==expected,cached?"cached_value":"empty_cache_base");
  for(unsigned i=1;i<=reads;++i){setup(cached);fixture::fail_call=i;
   check(!features::aimbot::read_seed_punch(pawn),"failed_RPM_must_not_be_zero_punch");}
 }
 setup(true);fixture::put(services+0x88,sample_vector{-1,0,a,1,0});check(!features::aimbot::read_seed_punch(pawn),"negative_size");
 setup(true);fixture::put(services+0xd0,sample_vector{2,0,b,1,0});check(!features::aimbot::read_seed_punch(pawn),"size_above_capacity");
 setup(true);fixture::put(a,foundation::vec3{NAN,0,0});check(!features::aimbot::read_seed_punch(pawn),"nonfinite");
 setup(true);check(!features::aimbot::read_seed_punch(0),"null_pawn");
 // A base epoch can change while both cache descriptors keep the same allocation and size.
 setup(true);fixture::mutate_call=3;fixture::mutate_address=services+0x48;fixture::mutate_value=std::byte{1};
 check(!features::aimbot::read_seed_punch(pawn),"mixed_base_epoch");
 setup(true);fixture::mutate_call=3;fixture::mutate_address=pawn+0x100;fixture::mutate_value=std::byte{1};
 check(!features::aimbot::read_seed_punch(pawn),"replaced_services");
 setup(true);fixture::mutate_call=5;fixture::mutate_address=a;fixture::mutate_value=std::byte{1};
 check(!features::aimbot::read_seed_punch(pawn),"rewritten_tail_same_descriptor");
 setup(true);fixture::mutate_call=4;fixture::mutate_address=services+0x9c;fixture::mutate_value=std::byte{1};
 check(!features::aimbot::read_seed_punch(pawn),"changed_cache_flags");
 setup(false);fixture::put(services+0x50,foundation::vec3{-0.0f,-0.0f,-0.0f});
 fixture::put(services+0xa4,foundation::vec3{-0.0f,-0.0f,-0.0f});
 const auto zero=features::aimbot::read_seed_punch(pawn);
 check(zero&&!std::signbit(zero->x)&&!std::signbit(zero->y)&&!std::signbit(zero->z),"preserve_signed_zero_accumulation");
 std::printf("SEED_PUNCH_SNAPSHOT cases=%u failed=%u\n",cases,failed);return failed?1:0;
}
