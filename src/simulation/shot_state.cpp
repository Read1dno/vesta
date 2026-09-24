#include <stdafx.hpp>
#include <simulation/shot_state.hpp>
#include <simulation/seed_timing.hpp>

namespace simulation {
std::optional<float> read_seed_fraction()
{
    auto& process=app::context().process;
    std::uintptr_t globals{}; float fraction{};
    if (!process.copy(app::context().addresses.global_vars,&globals,sizeof(globals)) || !globals
        || !process.copy(globals+0x50,&fraction,sizeof(fraction))
        || !std::isfinite(fraction) || fraction<0 || fraction>=1) return {};
    return fraction;
}

int read_seed_tick(std::uintptr_t controller, std::uintptr_t pawn)
{
    if (!controller || !pawn) return -1;
    auto& process = app::context().process;
    int ping{};
    if (!process.copy(controller + SCHEMA("CCSPlayerController","m_iPing"_id),
        &ping, sizeof(ping))) return -1;
    const bool host = ping == 0;
    int host_tick{-1}, simulation_tick{-1}, tick_base{-1};
    if (host) {
        std::uintptr_t globals{};
        if (!process.copy(app::context().addresses.global_vars, &globals, sizeof(globals))
            || !globals || !process.copy(globals + 0x44, &host_tick, sizeof(host_tick))) return -1;
    } else if (!process.copy(pawn + SCHEMA("C_BaseEntity","m_nSimulationTick"_id),
        &simulation_tick, sizeof(simulation_tick))) return -1;
    if (host_tick <= 0 && simulation_tick < 0
        && !process.copy(controller + SCHEMA("CBasePlayerController","m_nTickBase"_id),
            &tick_base, sizeof(tick_base))) return -1;
    return seed_timing::select_tick(host, host_tick, simulation_tick, tick_base);
}

std::optional<seed_window::recoil_pair> read_recoil_state(std::uintptr_t pawn)
{
    auto& process=app::context().process;
    std::uintptr_t services{};
    if (!pawn || !process.copy(pawn+SCHEMA("C_CSPlayerPawn","m_pAimPunchServices"_id),
        &services,sizeof(services)) || !services) return std::nullopt;
    const std::array<int,6> fields{
        SCHEMA("CCSPlayer_AimPunchServices","m_predictableBaseTick"_id),
        SCHEMA("CCSPlayer_AimPunchServices","m_predictableBaseTickInterpAmount"_id),
        SCHEMA("CCSPlayer_AimPunchServices","m_predictableBaseAngle"_id),
        SCHEMA("CCSPlayer_AimPunchServices","m_predictableBaseAngleVel"_id),
        SCHEMA("CCSPlayer_AimPunchServices","m_unpredictableBaseTick"_id),
        SCHEMA("CCSPlayer_AimPunchServices","m_unpredictableBaseAngle"_id)};
    const std::array<int,6> sizes{4,4,12,12,4,12};
    int first=fields[0], end=first;
    for (std::size_t i=0;i<fields.size();++i) {
        if(fields[i]<=0 || fields[i]>0x4000) return std::nullopt;
        first=std::min(first,fields[i]);end=std::max(end,fields[i]+sizes[i]);
    }
    if(end-first>256) return std::nullopt;
    std::array<std::byte,256> before{},after{};
    if(!process.copy(services+first,before.data(),end-first)
        || !process.copy(services+first,after.data(),end-first)) return std::nullopt;
    for(std::size_t i=0;i<fields.size();++i)
        if(std::memcmp(before.data()+fields[i]-first,after.data()+fields[i]-first,sizes[i]))
            return std::nullopt;
    std::uintptr_t confirmed_services{};
    if (!process.copy(pawn+SCHEMA("C_CSPlayerPawn","m_pAimPunchServices"_id),
        &confirmed_services,sizeof(confirmed_services)) || confirmed_services != services) return std::nullopt;
    seed_window::recoil_pair state{};
    auto extract=[&](int field,auto& value){std::memcpy(&value,after.data()+field-first,sizeof(value));};
    extract(fields[0],state.predictable.base.tick);extract(fields[1],state.predictable.base.fraction);
    extract(fields[2],state.predictable.angle);extract(fields[3],state.predictable.velocity);
    extract(fields[4],state.unpredictable.base.tick);extract(fields[5],state.unpredictable.angle);
    return state;
}

std::optional<foundation::vec3> read_shot_punch(std::uintptr_t pawn, shot_model::shot_time target)
{
    const auto state=read_recoil_state(pawn);
    if(!state) return std::nullopt;
    static thread_local std::array<shot_model::recoil_cache,2> cache{};
    foundation::vec3 a{},b{};
    if(!cache[0].sample(state->predictable,target,a) || !cache[1].sample(state->unpredictable,target,b))
        return std::nullopt;
    return a+b;
}
}
