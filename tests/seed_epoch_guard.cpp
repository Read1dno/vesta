#include <simulation/seed_weapon_guard.hpp>
#include <cstdio>
#include <cmath>
struct context {
    bool valid{true}, is_reloading{}, on_ground{true}, is_walking{};
    unsigned long long weapon{1}, weapon_vdata{2};
    int item_def_idx{9}, fire_mode{}, clip{4}, num_bullets{1}, pattern_seed{};
    int next_primary_attack_tick{14240}, player_tick{14278};
    unsigned ground_entity{};
    struct vector { float x{},y{},z{}; } velocity;
    float last_shot_time{223.09058f}, next_primary_attack_ratio{}, recoil_index{}, spread{.002f}, inaccuracy{.1f};
};
int main() {
    unsigned cases{}, failed{};
    auto check=[&](const char* name, bool value) { ++cases; if (!value) {++failed;std::printf("FAIL %s\n",name);} };
    const context a;
    check("unchanged epoch accepted",simulation::seed_weapon_consistent(a,a));
    auto reject=[&](const char* name, const context& b) {
        check(name,!simulation::seed_weapon_consistent(a,b) && !simulation::seed_weapon_consistent(b,a));
    };
    auto b=a;b.player_tick=14276;reject("mixed prediction epochs",b);
    b=a;b.player_tick=0;reject("invalid prediction tick",b);
    b=a;b.last_shot_time=224;reject("shot changed",b);
    b=a;b.ground_entity=1;reject("ground identity changed",b);
    b=a;b.velocity.x=10;reject("velocity x changed",b);
    b=a;b.velocity.y=10;reject("velocity y changed",b);
    b=a;b.velocity.z=10;reject("velocity z changed",b);
    b=a;b.last_shot_time=NAN;reject("invalid shot marker",b);
    b=a;b.player_tick=14279;b.velocity.x=20;b.last_shot_time=224;
    check("fresh internally consistent epoch accepted",simulation::seed_weapon_consistent(b,b));
    std::printf("seed_epoch_guard cases=%u failed=%u\n",cases,failed);
    return failed ? 1 : 0;
}
