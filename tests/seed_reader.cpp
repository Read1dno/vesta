#include <stdafx.hpp>
#include <cstdio>
#include <limits>
namespace {
constexpr std::uintptr_t controller=0x20000,pawn=0x30000,scene=0x40000,cache=0x50000;
void setup(int tick=-1){
    fixture::memory.clear();fixture::sampled=false;fixture::failure=0;fixture::mutation=0;fixture::fail_after=false;
    fixture::put(controller+schema("m_hPlayerPawn"),std::uint32_t{123});
    fixture::put(controller+schema("m_bPawnHasHelmet"),false);
    fixture::put(pawn+schema("m_iHealth"),100);
    fixture::put(pawn+schema("m_iTeamNum"),3);
    fixture::put(pawn+schema("m_bGunGameImmunity"),false);
    fixture::put(pawn+schema("m_ArmorValue"),0);
    fixture::put(pawn+schema("m_pGameSceneNode"),scene);
    fixture::put(pawn+schema("m_vecAbsVelocity"),foundation::vec3{});
    fixture::put(pawn+schema("m_nSimulationTick"),tick);
    fixture::put(pawn+schema("m_flSimulationTime"),436.375f);
    fixture::put(scene+schema("m_vecAbsOrigin"),foundation::vec3{10,20,30});
    fixture::put(scene+schema("m_modelState")+0x80,cache);
}
}
int main(){
    unsigned cases{},failed{};
    const auto check=[&](const char* name,bool expected){
        ++cases;game::world_sampler world;std::vector<game::player_snapshot> targets;
        world.seed_players_into(targets,0x10000,0x11000,2,false);
        if((targets.size()==1)!=expected){++failed;std::printf("FAIL %s targets=%zu\n",name,targets.size());}
    };
    setup();check("remote_tick_minus_one",true);
    setup(120);check("predicted_tick_positive",true);
    setup();fixture::failure=pawn+schema("m_nSimulationTick");check("initial_tick_read_failure",false);
    setup();fixture::failure=pawn+schema("m_nSimulationTick");fixture::fail_after=true;check("final_tick_read_failure",false);
    setup(120);fixture::failure=pawn+schema("m_flSimulationTime");check("initial_time_read_failure",false);
    setup(120);fixture::failure=pawn+schema("m_flSimulationTime");fixture::fail_after=true;check("final_time_read_failure",false);
    setup(120);fixture::mutate(pawn+schema("m_nSimulationTick"),121);check("tick_changed",false);
    setup(120);fixture::mutate(pawn+schema("m_flSimulationTime"),436.390625f);check("time_changed",false);
    setup(120);fixture::mutate(pawn+schema("m_pGameSceneNode"),std::uintptr_t{0x41000});check("scene_changed",false);
    setup(120);fixture::mutate(scene+schema("m_modelState")+0x80,std::uintptr_t{0x51000});check("cache_changed",false);
    setup(120);fixture::put(pawn+schema("m_flSimulationTime"),std::numeric_limits<float>::quiet_NaN());check("invalid_time",false);
    setup();fixture::put(pawn+schema("m_bGunGameImmunity"),true);check("immunity",false);
    setup();fixture::put(pawn+schema("m_iTeamNum"),2);check("teammate",false);
    setup();fixture::put(pawn+schema("m_iHealth"),0);check("dead",false);
    std::printf("seed_reader cases=%u failed=%u\n",cases,failed);
    return failed?1:0;
}
