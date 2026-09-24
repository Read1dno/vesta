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
    simulation::ballistics_t::context output{};
    unsigned cases{};
    auto expect=[&](bool expected) {
        ++cases;VESTA_CHECK(b.seed_weapon(pawn,controller,output)==expected);
        VESTA_CHECK(output.valid==expected);
    };
    setup();expect(true);
    const auto required_reads=fixture::calls;
    VESTA_CHECK(output.player_tick==100 && output.last_shot_time==1.0f);
    VESTA_CHECK(output.velocity.x==100 && output.velocity.y==20 && output.on_ground);
    VESTA_CHECK(output.weapon_ready && output.clip==30 && output.item_def_idx==7);
    VESTA_CHECK(output.inaccuracy>.03f && output.inaccuracy<1.0f);
    for(unsigned i=1;i<=required_reads;++i) {
        setup();fixture::failure_call=i;expect(false);
    }
    setup();fixture::change(controller+schema("m_nTickBase"),101,2);expect(false);
    setup();fixture::change(controller+schema("m_nTickBase"),101,3);expect(false);
    setup();fixture::change(services+schema("m_hActiveWeapon"),18u,2);expect(false);
    setup();fixture::change(pawn+schema("m_vecAbsVelocity"),foundation::vec3{101,20,0},2);expect(false);
    setup();fixture::change(weapon+schema("m_fLastShotTime"),1.1f,2);expect(false);
    setup();field(pawn,"m_vecAbsVelocity",foundation::vec3{NAN,0,0});expect(false);
    setup();field(weapon,"m_fLastShotTime",NAN);expect(false);
    setup();field(weapon,"m_flRecoilIndex",NAN);expect(false);
    setup();field(vdata,"m_flSpread",std::pair{NAN,.004f});expect(false);
    setup();fixture::failed_variable="weapon_accuracy_nospread";expect(false);
    setup();field(pawn,"m_vecAbsVelocity",foundation::vec3{200,40,300});
    field(pawn,"m_hGroundEntity",0xffffffffu);expect(true);VESTA_CHECK(!output.on_ground);
    VESTA_CHECK(output.debug.air_inaccuracy>0);
    setup();field(pawn,"m_bIsWalking",true);expect(true);VESTA_CHECK(output.is_walking);
    setup();field(weapon,"m_flRecoilIndex",8.0f);field(weapon,"m_fAccuracyPenalty",.5f);
    expect(true);VESTA_CHECK(output.inaccuracy>=.5f);
    setup();field(weapon,"m_iClip1",0);expect(true);VESTA_CHECK(!output.weapon_ready);
    setup();field(weapon,"m_bInReload",true);expect(true);VESTA_CHECK(!output.weapon_ready);
    setup();field(weapon,"m_nNextPrimaryAttackTick",102);expect(true);VESTA_CHECK(!output.weapon_ready);
    setup();field(controller,"m_nTickBase",101);expect(true);VESTA_CHECK(output.player_tick==101);
    std::cout<<"seed_snapshot cases="<<cases<<" required_reads="<<required_reads<<" failed=0\n";
}
