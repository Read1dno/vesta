#include <core/state/snapshot_epoch.hpp>
#include "test_support.hpp"
#include <array>
#include <cstring>
#include <limits>
int main()
{
    using namespace game::detail;
    unsigned cases{};
    auto check=[&](bool value){++cases;VESTA_CHECK(value);};
    const snapshot_epoch remote{0x20000,0x30000,-1,436.375f};
    check(same_epoch(remote,remote));
    auto changed=remote;changed.client_tick=120;
    check(same_epoch(changed,changed));
    check(!same_epoch(remote,changed));
    changed=remote;changed.simulation_time+=1.0f/64;check(!same_epoch(remote,changed));
    changed=remote;changed.scene+=8;check(!same_epoch(remote,changed));
    changed=remote;changed.bones+=8;check(!same_epoch(remote,changed));
    changed=remote;changed.client_tick=-2;check(!same_epoch(changed,changed));
    changed=remote;changed.simulation_time=std::numeric_limits<float>::quiet_NaN();check(!same_epoch(changed,changed));
    changed=remote;changed.simulation_time=-1;check(!same_epoch(changed,changed));
    changed=remote;changed.bones=0;check(!same_epoch(changed,changed));
    changed=remote;changed.scene=0;check(!same_epoch(changed,changed));
    const snapshot_epoch_offsets offsets{8,16,24,28};
    for(int failure=-1;failure<4;++failure) {
        int calls{};
        const auto reader=[&](std::uintptr_t address,void* output,std::size_t size){
            if(calls++==failure)return false;
            if(address==0x10000+8 && size==8)std::memcpy(output,&remote.scene,size);
            else if(address==0x10000+24 && size==4)std::memcpy(output,&remote.client_tick,size);
            else if(address==0x10000+28 && size==4)std::memcpy(output,&remote.simulation_time,size);
            else if(address==remote.scene+16 && size==8)std::memcpy(output,&remote.bones,size);
            else return false;
            return true;
        };
        const auto value=read_epoch(0x10000,offsets,reader);
        check(failure<0 ? value && same_epoch(remote,*value) : !value);
    }
    const auto forbidden=[](auto,auto*,auto){VESTA_CHECK(false);return false;};
    check(!read_epoch(0,offsets,forbidden));
    check(!read_epoch(0x10000,{},forbidden));
    std::cout<<"snapshot_epoch cases="<<cases<<" PASS\n";
}
