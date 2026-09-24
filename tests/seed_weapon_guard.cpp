#include <simulation/seed_weapon_guard.hpp>
#include "test_support.hpp"
#include <limits>
struct context {
    bool valid{true},is_reloading{},on_ground{true},is_walking{};
    unsigned long long weapon{1},weapon_vdata{2};
    int item_def_idx{7},fire_mode{},clip{30},num_bullets{1},pattern_seed{},next_primary_attack_tick{100};
    int player_tick{100}; unsigned ground_entity{};
    struct vector { float x{},y{},z{}; } velocity;
    float last_shot_time{};
    float next_primary_attack_ratio{},recoil_index{},spread{0.001f},inaccuracy{0.1f};
};
int main() {
    const context a;
    VESTA_CHECK(simulation::seed_weapon_consistent(a,a));
    unsigned changes{};
    const auto reject=[&](auto member,auto value) {
        auto b=a;b.*member=value;
        VESTA_CHECK(!simulation::seed_weapon_consistent(a,b));
        VESTA_CHECK(!simulation::seed_weapon_consistent(b,a));
        ++changes;
    };
    reject(&context::player_tick,99); reject(&context::last_shot_time,1.0f);
    reject(&context::ground_entity,1u);
    reject(&context::valid,false);reject(&context::is_reloading,true);
    reject(&context::on_ground,false);reject(&context::is_walking,true);
    reject(&context::weapon,3ull);reject(&context::weapon_vdata,4ull);
    reject(&context::item_def_idx,9);reject(&context::fire_mode,1);
    reject(&context::clip,29);reject(&context::clip,0);
    reject(&context::num_bullets,8);reject(&context::pattern_seed,1);
    reject(&context::next_primary_attack_tick,105);
    reject(&context::next_primary_attack_ratio,0.5f);
    reject(&context::recoil_index,1.0f);reject(&context::spread,0.02f);
    reject(&context::inaccuracy,0.2f);
    reject(&context::inaccuracy,std::numeric_limits<float>::quiet_NaN());
    std::cout<<"seed_weapon_guard: unchanged accepted, changed fields="<<changes<<" rejected PASS\n";
}
