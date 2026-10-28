#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <game/state/services.hpp>
#include <render/fonts.hpp>
#include <simulation/collision.hpp>

#include <algorithm>
#include <cmath>

namespace features::misc
{
using namespace walk_detail;

void walk_bot_t::on_render(render::draw::draw_list& draw_list)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!cfg.enabled)
        return;

    // Recording: a red dot and REC in the top-left corner.
    bool recording{};
    {
        std::scoped_lock lock(m_route_mutex);
        recording = m_recording_active;
    }
    if (recording)
    {
        draw_list.add_circle_filled(24.0f, 24.0f, 6.0f, render::draw::rgba{235, 60, 60, 230}, 16);
        draw_list.add_text(36.0f, 16.0f, "REC", render::fonts().menu_regular_12,
                           render::draw::rgba{235, 235, 240, 230});
    }

    std::shared_ptr<const simulation::nav_graph> graph;
    std::shared_ptr<const path> route;
    std::shared_ptr<const std::vector<walk_logic::recording>> recordings;
    foundation::vec3 destination{};
    bool has_destination{};
    {
        std::scoped_lock lock(m_route_mutex);
        graph = m_graph;
        route = m_path;
        recordings = m_recordings_shown;
        destination = m_destination;
        has_destination = m_has_destination;
    }
    const auto progress = m_progress.load(std::memory_order_acquire);
    const auto& camera = game::camera();
    const auto eye = camera.origin();
    const auto with_alpha = [](render::draw::rgba color, const float factor)
    {
        color.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(color.a) * factor, 0.0f, 255.0f));
        return color;
    };

    // Recorded routes of this map and side, in amber, each as its own line (jumps and crouches are on the points).
    if (cfg.show_recordings && recordings)
    {
        constexpr render::draw::rgba recorded_color{255, 196, 90, 170};
        std::vector<float> line;
        for (const auto& recording : *recordings)
        {
            line.clear();
            for (const auto& point : recording.points)
            {
                const auto projected = camera.project(point.position + foundation::vec3{0.0f, 0.0f, 2.0f});
                if (!camera.projection_valid(projected))
                {
                    draw_list.add_polyline(line, recorded_color, false, 1.5f);
                    line.clear();
                    continue;
                }
                line.push_back(projected.x);
                line.push_back(projected.y);
            }
            draw_list.add_polyline(line, recorded_color, false, 1.5f);
        }
    }

    // The navigation mesh around the player: polygon edges the eye can see, fading with distance.
    if (cfg.show_mesh && graph)
    {
        const auto& edges = graph->edges();
        if (m_edge_graph != graph.get() || m_edge_visible.size() != edges.size())
        {
            m_edge_graph = graph.get();
            m_edge_visible.assign(edges.size(), 0);
            m_edge_cursor = 0;
        }
        const auto range = cfg.mesh_distance;
        const auto lift = foundation::vec3{0.0f, 0.0f, 1.5f};
        // Line of sight is refreshed for a slice of the edges every frame.
        if (simulation::collision().valid() && !edges.empty())
        {
            for (int checked = 0, scanned = 0; checked < 320 && scanned < static_cast<int>(edges.size()); ++scanned)
            {
                const auto index = m_edge_cursor;
                m_edge_cursor = (m_edge_cursor + 1) % edges.size();
                const auto mid = (edges[index].first + edges[index].second) * 0.5f + foundation::vec3{0, 0, 4.0f};
                const auto length = mid.distance(eye);
                if (length > range)
                {
                    m_edge_visible[index] = 0;
                    continue;
                }
                ++checked;
                const auto hit = simulation::collision().trace_ray(eye, mid);
                m_edge_visible[index] = !hit.hit || hit.distance >= length - 8.0f ? 1 : 0;
            }
        }
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            if (!m_edge_visible[i])
                continue;
            const auto& [a, b] = edges[i];
            const auto distance = ((a + b) * 0.5f).distance(eye);
            if (distance > range)
                continue;
            const auto sa = camera.project(a + lift);
            const auto sb = camera.project(b + lift);
            if (!camera.projection_valid(sa) || !camera.projection_valid(sb))
                continue;
            draw_list.add_line(sa.x, sa.y, sb.x, sb.y, with_alpha(cfg.mesh_color, 1.0f - distance / range), 1.0f);
        }
    }

    if (!cfg.show_path || !route || route->empty())
        return;
    // A door or breakable the bot is dealing with.
    {
        const auto mark = m_render_obstacle.load(std::memory_order_acquire);
        if (mark.x != 0.0f || mark.y != 0.0f)
        {
            const auto projected = camera.project(mark);
            if (camera.projection_valid(projected))
                draw_list.add_circle(projected.x, projected.y, 9.0f, render::draw::rgba{255, 190, 70, 220}, 20, 2.0f);
        }
    }
    const auto total = route->back().distance;
    const auto lift = foundation::vec3{0.0f, 0.0f, 2.0f};
    // The line ahead, as one stroke per stretch of samples so its alpha can fade along the way.
    std::vector<float> stroke;
    float stroke_alpha = 1.0f;
    const auto flush = [&]
    {
        if (stroke.size() >= 4)
            draw_list.add_polyline(stroke, with_alpha(cfg.path_color, stroke_alpha), false, cfg.path_thickness);
        stroke.clear();
    };
    float next_marker = std::ceil(progress / 64.0f) * 64.0f;
    int samples_in_stroke = 0;
    for (std::size_t i = 0; i < route->size(); ++i)
    {
        const auto& point = (*route)[i];
        if (point.distance + k_sample_spacing < progress)
            continue;
        const auto along = std::clamp((point.distance - progress) / std::max(total - progress, 1.0f), 0.0f, 1.0f);
        const auto projected = camera.project(point.position + lift);
        if (!camera.projection_valid(projected))
        {
            flush();
            samples_in_stroke = 0;
            continue;
        }
        stroke.push_back(projected.x);
        stroke.push_back(projected.y);
        if (++samples_in_stroke >= 8)
        {
            stroke_alpha = 1.0f - along * 0.7f;
            flush();
            stroke.push_back(projected.x);
            stroke.push_back(projected.y);
            samples_in_stroke = 1;
        }
        const auto marker_color = with_alpha(cfg.path_color, 1.0f - along * 0.6f);
        if (point.distance >= next_marker)
        {
            draw_list.add_circle_filled(projected.x, projected.y, point.duck ? 3.5f : 2.2f, marker_color, 10);
            next_marker += 64.0f;
        }
        switch (point.act)
        {
            case action::jump:
            case action::crouch_jump:
            {
                draw_list.add_circle(projected.x, projected.y, 6.0f, marker_color, 16, 1.5f);
                const auto top = camera.project(
                    point.position + foundation::vec3{0.0f, 0.0f, point.act == action::jump ? 24.0f : 40.0f});
                if (camera.projection_valid(top))
                    draw_list.add_line(projected.x, projected.y, top.x, top.y, marker_color, 1.5f);
                break;
            }
            case action::ladder_up:
            case action::ladder_down:
            {
                const auto end = camera.project(point.goal + lift);
                if (camera.projection_valid(end))
                    draw_list.add_line(projected.x, projected.y, end.x, end.y, marker_color, cfg.path_thickness + 1.0f);
                break;
            }
            default:
                break;
        }
    }
    flush();

    if (has_destination)
    {
        constexpr int k_segments = 24;
        std::vector<float> ring;
        for (int i = 0; i < k_segments; ++i)
        {
            const auto theta = static_cast<float>(i) / static_cast<float>(k_segments) * 6.2831853f;
            const auto projected =
                camera.project(destination + foundation::vec3{std::cos(theta) * 16.0f, std::sin(theta) * 16.0f, 2.0f});
            if (!camera.projection_valid(projected))
            {
                ring.clear();
                break;
            }
            ring.push_back(projected.x);
            ring.push_back(projected.y);
        }
        if (!ring.empty())
            draw_list.add_polyline(ring, cfg.path_color, true, cfg.path_thickness);
    }
}
} // namespace features::misc
