#pragma once

#include <simulation/collision.hpp>
#include <algorithm>
#include <cmath>

namespace game::collision_detail {

[[nodiscard]] inline collision_world::segment_build_result build_segments(
    std::vector<collision_world::hit_entry> hits, float ray_length)
{
    using record = collision_world::penetration_record;
    collision_world::segment_build_result result{};
    if (!std::isfinite(ray_length) || ray_length < 0.0f) {
        result.unresolved_distance = 0.0f;
        return result;
    }
    if (std::ranges::any_of(hits, [](const auto& h) { return !std::isfinite(h.distance); })) {
        result.unresolved_distance = 0.0f;
        return result;
    }
    std::erase_if(hits, [=](const auto& h) { return h.distance < 0.0f || h.distance > ray_length; });
    result.had_contacts = !hits.empty();
    // First contact per solid determines whether the ray starts inside it.
    std::ranges::sort(hits, [](const auto& a, const auto& b) {
        if (a.solid_id != b.solid_id) return a.solid_id < b.solid_id;
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.is_enter > b.is_enter;
    });
    std::size_t depth{};
    for (std::size_t i = 0; i < hits.size(); ++i)
        if ((i == 0 || hits[i].solid_id != hits[i-1].solid_id) && !hits[i].is_enter) ++depth;
    std::ranges::sort(hits, [](const auto& a, const auto& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.is_enter != b.is_enter) return a.is_enter;
        if (a.solid_id != b.solid_id) return a.solid_id < b.solid_id;
        return a.surface.surface_type < b.surface.surface_type;
    });
    auto& contacts = result.contacts;
    contacts.reserve(hits.size());
    for (const auto& hit : hits) {
        auto duplicate = contacts.end();
        for (auto it = contacts.end(); it != contacts.begin();) {
            --it;
            if (hit.distance - it->distance > 0.00001f) break;
            if (hit.solid_id == it->solid_id && hit.is_enter == it->is_enter) {
                duplicate = it;
                break;
            }
        }
        if (duplicate == contacts.end()) contacts.push_back(hit);
        else {
            const auto flags = duplicate->surface.interacts_as & hit.surface.interacts_as;
            if (hit.surface.penetration < duplicate->surface.penetration)
                duplicate->surface = hit.surface;
            duplicate->surface.interacts_as = flags;
        }
    }

    std::size_t anchor{};
    auto start_inside = depth > 0;
    std::vector<record> material;
    material.reserve(contacts.size() / 2 + 1);
    for (std::size_t current = 0; current < contacts.size(); ++current) {
        if (contacts[current].is_enter) {
            if (depth == 0) anchor = current;
            ++depth;
        } else if (depth == 0) {
            result.unresolved_distance = 0.0f;
        } else if (--depth == 0) {
            const auto start = start_inside ? 0.0f : contacts[anchor].distance;
            const auto end = contacts[current].distance;
            // Native contact ordering closes sub-1/512 gaps before record construction.
            if (!material.empty() && start - material.back().end_distance <= 1.0f / 512.0f) {
                material.back().last_contact = current;
                material.back().end_distance = end;
            } else material.push_back({anchor, current, false, end, start});
            start_inside = false;
        }
    }
    if (depth > 0) result.unresolved_distance = std::min(result.unresolved_distance,
        start_inside ? 0.0f : contacts[anchor].distance);

    auto cursor = 0.0f;
    result.records.reserve(material.size() * 2 + 1);
    for (const auto& r : material) {
        if (r.start_distance > cursor)
            result.records.push_back({r.first_contact, r.first_contact, true, r.start_distance, cursor});
        result.records.push_back(r);
        cursor = r.end_distance;
        const auto& first = contacts[r.first_contact];
        const auto& last = contacts[r.last_contact];
        auto minimum = first.surface.penetration;
        auto density = first.surface.density;
        for (auto i = r.first_contact; i <= r.last_contact; ++i) {
            minimum = std::min(minimum, contacts[i].surface.penetration);
            density = std::max(density, contacts[i].surface.density);
        }
        result.segments.push_back({
            .enter_fraction = ray_length > 0.0f ? r.start_distance / ray_length : 0.0f,
            .exit_fraction = ray_length > 0.0f ? r.end_distance / ray_length : 0.0f,
            .enter_distance = r.start_distance, .exit_distance = r.end_distance,
            .enter_pos = first.position, .exit_pos = last.position,
            .enter_surface = first.surface, .exit_surface = last.surface,
            .thickness = r.end_distance - r.start_distance,
            .min_pen_mod = minimum, .max_density = density,
            .first_contact = r.first_contact, .last_contact = r.last_contact});
    }
    if (cursor < ray_length)
        result.records.push_back({0, 0, true, ray_length, cursor});
    return result;
}

} // namespace game::collision_detail
