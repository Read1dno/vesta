#pragma once

// Cleaning of screen-space polylines before a thick stroke is drawn. A thick polyline joins its segments with mitres
// computed from each segment's direction, and lines projected from the world break that at some view angles:
// neighbouring samples land a fraction of a pixel apart (the direction of a near-zero segment is noise and the outline
// wobbles like a wave) or the line folds back on screen (the mitre shoots out). Points closer than `min_step` pixels to
// the previous one are dropped and the stroke is split into runs at folds sharper than ~110 degrees.

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace render::draw
{
struct stroke_point
{
    float x{};
    float y{};
};

struct stroke_run
{
    std::size_t first{};
    std::size_t count{};
};

struct clean_stroke
{
    std::vector<stroke_point> points{};
    std::vector<stroke_run> runs{};
    bool closed{}; // still closed: only when the input was and no fold split it
};

// `points` holds x, y pairs.
inline void clean_polyline(std::span<const float> points, const bool closed, clean_stroke& out,
                           const float min_step = 1.5f)
{
    out.points.clear();
    out.runs.clear();
    out.closed = false;
    const auto min_step_sqr = min_step * min_step;
    for (std::size_t i = 0; i + 1 < points.size(); i += 2)
    {
        const stroke_point point{points[i], points[i + 1]};
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            continue;
        if (!out.points.empty())
        {
            const auto dx = point.x - out.points.back().x;
            const auto dy = point.y - out.points.back().y;
            if (dx * dx + dy * dy < min_step_sqr)
                continue;
        }
        out.points.push_back(point);
    }
    if (closed && out.points.size() > 2)
    {
        const auto dx = out.points.front().x - out.points.back().x;
        const auto dy = out.points.front().y - out.points.back().y;
        if (dx * dx + dy * dy < min_step_sqr)
            out.points.pop_back();
    }
    if (out.points.size() < 2)
        return;
    std::size_t start = 0;
    for (std::size_t i = 1; i + 1 < out.points.size(); ++i)
    {
        const auto in_x = out.points[i].x - out.points[i - 1].x;
        const auto in_y = out.points[i].y - out.points[i - 1].y;
        const auto out_x = out.points[i + 1].x - out.points[i].x;
        const auto out_y = out.points[i + 1].y - out.points[i].y;
        const auto dot = in_x * out_x + in_y * out_y;
        const auto lengths = std::sqrt((in_x * in_x + in_y * in_y) * (out_x * out_x + out_y * out_y));
        if (lengths > 0.0f && dot < -0.34f * lengths)
        {
            out.runs.push_back({start, i - start + 1});
            start = i;
        }
    }
    out.closed = closed && out.runs.empty();
    out.runs.push_back({start, out.points.size() - start});
}
} // namespace render::draw
