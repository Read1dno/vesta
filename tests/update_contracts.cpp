#include "test_support.hpp"
#include <core/memory/compatibility_contracts.hpp>
#include <limits>
using namespace game::compatibility;
using namespace game::compatibility::detail;
template<class T, std::size_t N> void put(std::array<std::byte,N>& b,std::size_t at,T v)
{std::memcpy(b.data()+at,&v,sizeof(v));}
int main()
{
    std::array<std::byte,32> punch_code{};
    put(punch_code,18,0x8e110ff2u);put(punch_code,26,std::uint16_t{0x8689});
    for(auto offset:{0x1b1cu,0x1d4cu,0x2010u}) {
        put(punch_code,22,offset);put(punch_code,28,offset+8);
        VESTA_CHECK(decode_shot_punch(punch_code)==offset);
    }
    put(punch_code,28,0x2020u);VESTA_CHECK(!decode_shot_punch(punch_code));
    VESTA_CHECK(!decode_shot_punch(std::span<const std::byte>(punch_code).first(31)));
    punch_code[18]=std::byte{0};VESTA_CHECK(!decode_shot_punch(punch_code));
    std::array<std::byte,0x5c> cvar{};
    bool boolean{};float scalar{};int integer{};
    put(cvar,0x28,0u);put(cvar,0x58,0x0add0600u);
    VESTA_CHECK(convar_value(cvar,boolean) && !boolean);
    VESTA_CHECK(!convar_value(cvar,scalar));
    put(cvar,0x58,0x0add0601u);VESTA_CHECK(convar_value(cvar,boolean) && boolean);
    put(cvar,0x58,2u);VESTA_CHECK(!convar_value(cvar,boolean));
    put(cvar,0x28,7u);put(cvar,0x58,600.0f);
    VESTA_CHECK(convar_value(cvar,scalar) && scalar==600.0f);
    VESTA_CHECK(!convar_value(cvar,integer));
    put(cvar,0x58,std::numeric_limits<float>::quiet_NaN());
    VESTA_CHECK(!convar_value(cvar,scalar) && scalar==0);
    put(cvar,0x28,3u);put(cvar,0x58,-5);
    VESTA_CHECK(convar_value(cvar,integer) && integer==-5);
    VESTA_CHECK(!convar_value(std::span<const std::byte>(cvar).first(0x5b),integer));
    std::array<std::byte,60> code{};
    for(auto origin:{0x190u,0x1f0u,0x2f0u}) {
        put(code,56,origin);put(code,47,origin+4);put(code,26,origin+12);
        put(code,18,origin+16);put(code,8,0x18034u);
        const auto layout=decode_radar(code);
        VESTA_CHECK(layout && layout->origin==origin-0x20 && layout->alternate_scale==0x18014);
    }
    put(code,47,0x500u);VESTA_CHECK(!decode_radar(code));
    VESTA_CHECK(!decode_radar(std::span<const std::byte>(code).first(59)));
    std::array<std::byte,9> getter{};
    put(getter,0,0x81100ff3u);put(getter,4,0x1b8u);getter[8]=std::byte{0xc3};
    VESTA_CHECK(float_getter(getter)==0x1b8);
    put(getter,4,0x2c0u);VESTA_CHECK(float_getter(getter)==0x2c0);
    getter[8]=std::byte{0xe9};VESTA_CHECK(!float_getter(getter));
    crosshair_frame source{},out{};
    source.count=1;source.pieces[0].geometry={959,539,959,539};
    source.pieces[0].color={0,1,0,1};
    VESTA_CHECK(valid_frame(source));
    source.count=17;VESTA_CHECK(!valid_frame(source));source.count=1;
    source.pieces[0].type=9;VESTA_CHECK(!valid_frame(source));source.pieces[0].type=0;
    source.pieces[0].geometry[0]=NAN;VESTA_CHECK(!valid_frame(source));
    source.pieces[0].geometry[0]=959;
    source.pieces[0].color[3]=2;VESTA_CHECK(!valid_frame(source));source.pieces[0].color[3]=1;
    VESTA_CHECK(std::abs(linear_to_srgb(0.21404114f)-0.5f)<1e-6f);
    VESTA_CHECK(linear_to_srgb(0)==0 && std::abs(linear_to_srgb(1)-1)<1e-6f);
    std::uint32_t sequence{},reads{},published{};
    const auto reader=[&](std::uintptr_t address,void* target,std::size_t bytes) {
        ++reads;
        if(address==0x100) {std::memcpy(target,&sequence,bytes);return true;}
        VESTA_CHECK(address==0x1000+(sequence&1)*sizeof(source));
        std::memcpy(target,&source,bytes);return true;
    };
    VESTA_CHECK(!coherent_frame(0x100,0x1000,reader,out,published));
    sequence=11;reads=0;
    VESTA_CHECK(coherent_frame(0x100,0x1000,reader,out,published));
    VESTA_CHECK(published==11 && reads==3 && out.count==1);
    int races{};
    auto raced=[&](auto address,void* target,std::size_t bytes) {
        const bool ok=reader(address,target,bytes);
        if(address!=0x100 && races++==0) ++sequence;
        return ok;
    };
    VESTA_CHECK(coherent_frame(0x100,0x1000,raced,out,published) && published==12);
    auto unstable=[&](auto address,void* target,std::size_t bytes) {
        const bool ok=reader(address,target,bytes);
        if(address!=0x100) ++sequence;
        return ok;
    };
    reads=0;VESTA_CHECK(!coherent_frame(0x100,0x1000,unstable,out,published));
    VESTA_CHECK(reads==9 && out.count==0 && published==0);
    std::cout<<"update_contracts: typed values, layout drift, invalid data and frame races PASS\n";
}
