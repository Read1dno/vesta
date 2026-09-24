#pragma once
#include <cmath>

namespace simulation {
template<class Context>
[[nodiscard]] bool seed_weapon_consistent(const Context& a, const Context& b) noexcept
{
    const auto same=[](float x,float y) { return std::isfinite(x) && std::isfinite(y)
        && std::abs(x-y)<=0.000001f; };
    return a.valid && b.valid && a.weapon==b.weapon && a.weapon_vdata==b.weapon_vdata
        && a.item_def_idx==b.item_def_idx && a.fire_mode==b.fire_mode
        && a.player_tick == b.player_tick && a.player_tick > 0
        && same(a.last_shot_time, b.last_shot_time) && a.last_shot_time >= 0
        && a.ground_entity == b.ground_entity
        && same(a.velocity.x, b.velocity.x) && same(a.velocity.y, b.velocity.y)
        && same(a.velocity.z, b.velocity.z)
        && a.clip==b.clip && a.clip>0 && !a.is_reloading && !b.is_reloading
        && a.on_ground==b.on_ground && a.is_walking==b.is_walking
        && a.num_bullets==b.num_bullets && a.pattern_seed==b.pattern_seed
        && a.next_primary_attack_tick==b.next_primary_attack_tick
        && same(a.next_primary_attack_ratio,b.next_primary_attack_ratio)
        && same(a.recoil_index,b.recoil_index) && same(a.spread,b.spread)
        && same(a.inaccuracy,b.inaccuracy);
}
}
