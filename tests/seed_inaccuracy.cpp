#include <stdafx.hpp>
#include <simulation/ballistics.hpp>
#include "test_support.hpp"

void simulation::ballistics_t::penetration::prepare(std::uintptr_t, std::uintptr_t) {
    m_weapon_data={35.0f,2.0f,.98f,8192.0f,1.5f,4.0f};
}
namespace {
constexpr std::uintptr_t pawn=0x10000,controller=0x20000,weapon=0x30000,vdata=0x40000,services=0x50000;
template<class T> void field(std::uintptr_t p,std::string_view name,T value) {fixture::put(p+schema(name),value);}
void setup() {
    fixture::memory.clear();fixture::clear_reads();
    for(auto p:{pawn,controller,weapon,vdata,services})
        for(unsigned i=0;i<0x4000;++i)fixture::memory[p+i]=std::byte{};
    field(controller,"m_nTickBase",100);
    field(pawn,"m_vecAbsVelocity",foundation::vec3{100,20,0});
    field(pawn,"m_pWeaponServices",services);
    field(services,"m_hActiveWeapon",std::uint32_t{17});
    field(weapon,"m_fLastShotTime",1.0f);
    fixture::put(weapon+schema("m_nSubclassID")+8,vdata);
    fixture::put(weapon+schema("m_AttributeManager")+schema("m_Item")+schema("m_iItemDefinitionIndex"),std::uint16_t{7});
    field(weapon,"m_weaponMode",0);
    field(vdata,"m_nNumBullets",1);
    field(vdata,"m_WeaponType",3u);
    field(vdata,"m_flSpread",std::pair{.003f,.004f});
    field(weapon,"m_flRecoilIndex",0.0f);
    field(vdata,"m_flInaccuracyMove",std::pair{.1f,.2f});
    field(vdata,"m_flMaxSpeed",std::pair{250.0f,220.0f});
    field(vdata,"m_flInaccuracyJumpInitial",.4f);
    field(vdata,"m_flInaccuracyJumpApex",.2f);
    field(weapon,"m_flTurningInaccuracy",.01f);
    field(weapon,"m_fAccuracyPenalty",.02f);
    field(pawn,"m_bIsWalking",false);
    field(pawn,"m_hGroundEntity",0u);
    field(pawn,"v_angle",foundation::vec3{0,30,0});
    field(weapon,"m_bInReload",false);
    field(weapon,"m_iClip1",30);
    field(weapon,"m_nPostponeFireReadyTicks",0);
    field(weapon,"m_flPostponeFireReadyFrac",0.0f);
    field(weapon,"m_nNextPrimaryAttackTick",99);
    field(weapon,"m_flNextPrimaryAttackTickRatio",0.0f);
}
}
int main() {
    simulation::ballistics_t b;
    simulation::ballistics_t::context out{};
    unsigned cases{},failed{};
    const auto sample=[&](const char* name,foundation::vec3 velocity,float expected,bool valid=true) {
        ++cases;
        const bool actual=b.seed_weapon(pawn,controller,velocity,out);
        if(actual!=valid || out.valid!=valid || (valid && (!std::isfinite(out.inaccuracy)
            || std::abs(out.inaccuracy-expected)>2e-6f))) {
            ++failed;std::printf("FAIL %s valid=%d inacc=%.9g expected=%.9g\n",name,actual,out.inaccuracy,expected);
        }
    };
    setup();sample("stationary",{},.03f);
    setup();sample("running",{250,0,0},.13f);
    setup();field(pawn,"m_bIsWalking",true);sample("walking_midpoint",{161.25f,0,0},.08f);
    setup();field(weapon,"m_fAccuracyPenalty",.5f);sample("postshot_penalty",{},.51f);
    setup();field(weapon,"m_flTurningInaccuracy",.21f);sample("turning",{},.23f);
    setup();fixture::variables["weapon_accuracy_forcespread"]=.37;sample("forcespread",{},.37f);
    setup();fixture::variables["weapon_accuracy_forcespread"]=2;sample("forcespread_clamp",{},1);
    setup();fixture::variables["weapon_accuracy_nospread"]=1;sample("nospread",{250,0,0},0);
    setup();fixture::variables["weapon_accuracy_forcespread"]=.25;fixture::variables["weapon_accuracy_nospread"]=1;
    sample("override_precedence",{},.25f);
    setup();field(pawn,"m_hGroundEntity",0xffffffffu);fixture::variables["sv_jump_impulse"]=400;
    fixture::variables["weapon_air_spread_scale"]=2;sample("jump_impulse_and_air_scale",{0,0,400},.83f);
    setup();field(pawn,"m_hGroundEntity",0xffffffffu);fixture::variables["sv_jump_impulse"]=900;
    sample("changed_jump_impulse",{0,0,225},.296666667f);
    setup();field(pawn,"m_hGroundEntity",0xffffffffu);fixture::variables["weapon_air_spread_scale"]=0;
    sample("zero_air_scale",{0,0,300},.03f);
    setup();field(pawn,"m_hGroundEntity",0xfffffffeu);fixture::variables["sv_jump_impulse"]=400;
    sample("invalid_ground_handle",{0,0,400},.43f);
    setup();field(pawn,"m_hGroundEntity",123u);fixture::variables["sv_jump_impulse"]=400;
    sample("unresolved_ground_handle",{0,0,400},.43f);
    setup();field(pawn,"m_hGroundEntity",0xffffffffu);field(pawn,"m_MoveType",std::uint8_t{9});
    fixture::variables["sv_jump_impulse"]=400;sample("ground_query_not_move_type",{0,0,400},.43f);
    setup();field(pawn,"v_angle",foundation::vec3{});fixture::variables["sv_strafing_inaccuracy_enabled"]=1;
    sample("strafe_front",{150,200,0},.17f);
    setup();field(pawn,"v_angle",foundation::vec3{});fixture::variables["sv_strafing_inaccuracy_enabled"]=1;
    fixture::variables["sv_strafing_inaccuracy_bias"]=.25;sample("strafe_bias",{150,200,0},.148181818f);
    setup();field(pawn,"v_angle",foundation::vec3{0,90,0});fixture::variables["sv_strafing_inaccuracy_enabled"]=1;
    sample("rotated_strafe",{150,200,0},.15f);
    setup();field(weapon,"m_weaponMode",1);sample("secondary_mode",{250,0,0},.23f);
    setup();field(weapon,"m_weaponMode",2);sample("nonsecondary_mode",{250,0,0},.13f);
    setup();field(weapon,"m_fAccuracyPenalty",.95f);sample("total_clamp",{250,0,0},1);
    for(auto name:{"weapon_accuracy_forcespread","weapon_accuracy_nospread","sv_strafing_inaccuracy_enabled"}) {
        setup();fixture::failed_variable=name;sample(name,{},0,false);
    }
    for(auto name:{"sv_jump_impulse","weapon_air_spread_scale"}) {
        setup();field(pawn,"m_hGroundEntity",0xffffffffu);fixture::failed_variable=name;sample(name,{0,0,300},0,false);
    }
    for(auto name:{"sv_strafing_inaccuracy_bias","sv_strafing_inaccuracy_scale"}) {
        setup();fixture::variables["sv_strafing_inaccuracy_enabled"]=1;fixture::failed_variable=name;sample(name,{250,0,0},0,false);
    }
    setup();fixture::failed_variable="sv_jump_impulse";sample("unused_air_parameter",{},.03f);
    setup();fixture::failed_variable="sv_strafing_inaccuracy_bias";sample("unused_strafe_parameter",{},.03f);
    setup();fixture::failed_variable="weapon_accuracy_nospread";fixture::variables["weapon_accuracy_forcespread"]=.3;
    sample("forced_early_return",{},.3f);
    setup();fixture::failed_variable="sv_strafing_inaccuracy_enabled";fixture::variables["weapon_accuracy_nospread"]=1;
    sample("nospread_early_return",{},0);
    setup();sample("nonfinite_velocity",{NAN,0,0},0,false);
    setup();field(pawn,"v_angle",foundation::vec3{0,NAN,0});sample("nonfinite_strafe_angle",{},0,false);
    setup();fixture::variables["weapon_air_spread_scale"]=NAN;field(pawn,"m_hGroundEntity",0xffffffffu);
    sample("nonfinite_air_scale",{0,0,300},0,false);
    setup();fixture::variables["weapon_accuracy_forcespread"]=NAN;sample("nonfinite_override",{},0,false);
    std::printf("seed_inaccuracy cases=%u failed=%u\n",cases,failed);
    return failed?1:0;
}
