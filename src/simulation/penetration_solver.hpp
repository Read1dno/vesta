#pragma once

#include <simulation/collision.hpp>
#include <simulation/collision_layers.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

namespace simulation::detail {

struct passage_result
{
    float damage{};
    int penetrations{};
};

[[nodiscard]] inline std::optional<passage_result> pass_through_world(
    const game::collision_world::segment_build_result& collision,
    float target_distance, float weapon_penetration, float initial_damage,
    float range_modifier, bool allow_penetration)
{
    if (!std::isfinite(target_distance) || target_distance < 0.0f
        || !std::isfinite(initial_damage) || initial_damage < 1.0f
        || !std::isfinite(weapon_penetration) || weapon_penetration < 0.0f
        || !std::isfinite(range_modifier) || range_modifier <= 0.0f || range_modifier > 1.0f
        || collision.unresolved_before(target_distance)) return std::nullopt;

    passage_result state{initial_damage, 0};
    auto traveled = 0.0f;
    for (const auto& record : collision.records) {
        if (!std::isfinite(record.start_distance) || !std::isfinite(record.end_distance)
            || record.start_distance != traveled || record.end_distance < record.start_distance)
            return std::nullopt;
        if (record.start_distance >= target_distance) break;
        const auto end = std::min(record.end_distance, target_distance);
        if (record.range_loss) {
            state.damage *= std::pow(range_modifier, (end - record.start_distance) / 500.0f);
        } else {
            if (record.first_contact > record.last_contact
                || record.last_contact >= collision.contacts.size()
                || record.end_distance > target_distance || !allow_penetration
                || weapon_penetration <= 0.0f || state.penetrations >= 4
                || record.end_distance > 3000.0f) return std::nullopt;
            const auto& entrance = collision.contacts[record.first_contact].surface;
            const auto& exit = collision.contacts[record.last_contact].surface;
            if (!std::isfinite(entrance.penetration) || entrance.penetration < 0.1f)
                return std::nullopt;
            auto factor = entrance.penetration;
            for (auto index = record.first_contact; index <= record.last_contact; ++index) {
                const auto modifier = collision.contacts[index].surface.penetration;
                if (!std::isfinite(modifier) || modifier <= 0.0f) return std::nullopt;
                factor = std::min(factor, modifier);
            }
            const auto thickness = record.end_distance - record.start_distance;
            auto damage_fraction = 0.16f;
            if (factor >= 0.1f && entrance.surface_type == exit.surface_type) {
                const auto material = entrance.surface_type;
                if (material == 'W' || material == 'U') factor = 3.0f;
                else if (material == 'L') factor = 2.0f;
                if (thickness < 6.0f && (material == 'G' || material == 'Y')) {
                    factor = 3.0f;
                    damage_fraction = 0.05f;
                }
                if ((entrance.interacts_as & exit.interacts_as & game::collision_detail::pass_bullets_layer) != 0) {
                    factor = thickness < 6.0f ? 32.0f : 3.0f;
                    if (thickness < 6.0f) damage_fraction = 0.00001f;
                }
            }
            const auto resistance = std::max(1.0f / factor, 0.0f);
            const auto weapon_loss = std::max((3.0f / weapon_penetration) * 1.25f, 0.0f);
            const auto loss = thickness * thickness * resistance / 24.0f
                + (weapon_loss * resistance * 3.0f + damage_fraction * state.damage);
            state.damage -= std::max(loss, 0.0f);
            ++state.penetrations;
        }
        traveled = end;
        if (!std::isfinite(state.damage) || state.damage < 1.0f) return std::nullopt;
    }
    // World tracing does not include the independently clipped player contact.
    if (traveled < target_distance)
        state.damage *= std::pow(range_modifier, (target_distance - traveled) / 500.0f);
    if (!std::isfinite(state.damage) || state.damage < 1.0f) return std::nullopt;
    return state;
}

} // namespace simulation::detail
