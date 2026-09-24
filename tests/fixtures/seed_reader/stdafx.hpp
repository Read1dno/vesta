#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>
constexpr std::string_view operator ""_id(const char* value,std::size_t size){return {value,size};}
inline int schema(std::string_view name){
    const std::array names{"m_hPlayerPawn","m_bPawnHasHelmet","m_iHealth","m_iTeamNum","m_pGameSceneNode",
        "m_bGunGameImmunity","m_ArmorValue","m_vecAbsOrigin","m_modelState","m_vecAbsVelocity",
        "m_nSimulationTick","m_flSimulationTime"};
    for(std::size_t i=0;i<names.size();++i)if(name==names[i])return int(32+i*32);
    return 0;
}
#define SCHEMA(c,n) schema(n)
namespace fixture {
inline std::unordered_map<std::uintptr_t,std::byte> memory;
inline bool sampled{};
inline std::uintptr_t failure{}, mutation{};
inline bool fail_after{};
inline std::vector<std::byte> replacement;
template<class T> void put(std::uintptr_t address,T value){
    const auto* bytes=reinterpret_cast<const std::byte*>(&value);
    for(std::size_t i=0;i<sizeof(T);++i)memory[address+i]=bytes[i];
}
template<class T> void mutate(std::uintptr_t address,T value){
    mutation=address;replacement.resize(sizeof(T));std::memcpy(replacement.data(),&value,sizeof(T));
}
}
namespace foundation {struct vec3 {float x{},y{},z{};};}
namespace app {
struct reader {
    bool copy(std::uintptr_t address,void* output,std::size_t size) const {
        if(address==fixture::failure && (!fixture::fail_after || fixture::sampled))return false;
        if(fixture::sampled && address==fixture::mutation && size==fixture::replacement.size()){
            std::memcpy(output,fixture::replacement.data(),size);return true;
        }
        for(std::size_t i=0;i<size;++i)if(!fixture::memory.contains(address+i))return false;
        for(std::size_t i=0;i<size;++i)static_cast<std::byte*>(output)[i]=fixture::memory.at(address+i);
        return true;
    }
    template<class T>T load(std::uintptr_t address,T fallback={}) const {
        T value{};return copy(address,&value,sizeof(value))?value:fallback;
    }
};
struct context_t {reader process;};
inline context_t& context(){static context_t value;return value;}
}
namespace game {
struct bones_t {bool is_valid()const{return true;}};
struct hitboxes_t {int count{3};};
struct player_snapshot {
    std::uintptr_t controller{},pawn{},game_scene_node{},bone_cache{};
    int health{},team{},armor{},simulation_tick{};bool invulnerable{},has_helmet{};
    foundation::vec3 origin{},velocity{};float simulation_time{};
    bones_t bones;hitboxes_t hitboxes;
};
struct directory {
    std::uintptr_t lookup_index(std::uint32_t index)const{return index==2?0x20000:0;}
    std::uintptr_t lookup(std::uint32_t handle)const{return handle==123?0x30000:0;}
};
struct skeleton_reader {bones_t get(std::uintptr_t)const{fixture::sampled=true;return {};}};
struct hitbox_reader {hitboxes_t query(std::uintptr_t,bool)const{return {};}};
inline directory& entity_index(){static directory value;return value;}
inline skeleton_reader& skeletons(){static skeleton_reader value;return value;}
inline hitbox_reader& hitbox_data(){static hitbox_reader value;return value;}
class world_sampler {
public:
 void seed_players_into(std::vector<player_snapshot>&,std::uintptr_t,std::uintptr_t,int,bool,std::uintptr_t=0)const;
};
}
