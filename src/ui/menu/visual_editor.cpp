#include <stdafx.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <render/presentation.hpp>
#include <render/text/localization.hpp>
#include <features/visuals/visuals.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <ui/presentation.hpp>
#include <ui/menu/menu.hpp>
#include <ui/menu/internal.hpp>
#include <ui/overlay/overlay.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>

// The ESP editor beside the Visuals > Players page: a preview of the player model with every enabled ESP element drawn
// over it. Text elements and bars can be dragged, scaled with the mouse wheel and configured from a popup; bars snap
// to the four sides of the box. With GPU effects on the model is the live chams preview and the overlays follow its
// projected bones and hitboxes; otherwise a stock image with fixed bone positions stands in.

namespace ui
{
using namespace ui::menu::detail;

namespace
{
using player_profile = config::visual_profile::player;

constexpr auto k_panel_size = ImVec2{320.0f, 650.0f};
constexpr auto k_image_size = ImVec2{280.0f, 420.0f};
constexpr int k_head_bone = 7;
constexpr std::size_t k_preview_bone_count = 24;

struct preview_hitbox_geometry
{
    int bone{};
    ImVec2 from{};
    ImVec2 to{};
    float radius{};
};

// Bone positions and hitbox capsules of the stock preview image, normalized to the image size.
constexpr std::array<int, 19> k_preview_skeleton_bones{1,  2,  3,  4,  6,  7,  9,  10, 11, 13,
                                                       14, 15, 17, 18, 19, 20, 21, 22, 23};

constexpr auto k_preview_bones = []
{
    std::array<ImVec2, k_preview_bone_count> result{};
    result[1] = {0.485714f, 0.380952f};
    result[2] = {0.489286f, 0.338095f};
    result[3] = {0.489286f, 0.302381f};
    result[4] = {0.489286f, 0.259524f};
    result[6] = {0.496429f, 0.147619f};
    result[7] = {0.482143f, 0.097619f};
    result[9] = {0.335714f, 0.185714f};
    result[10] = {0.282143f, 0.280952f};
    result[11] = {0.267857f, 0.404762f};
    result[13] = {0.650000f, 0.190476f};
    result[14] = {0.717857f, 0.273810f};
    result[15] = {0.739286f, 0.397619f};
    result[17] = {0.400000f, 0.407143f};
    result[18] = {0.389286f, 0.566667f};
    result[19] = {0.432143f, 0.735714f};
    result[20] = {0.582143f, 0.409524f};
    result[21] = {0.639286f, 0.573810f};
    result[22] = {0.689286f, 0.759524f};
    result[23] = {0.492857f, 0.211905f};
    return result;
}();

constexpr std::array<preview_hitbox_geometry, 21> k_preview_hitboxes{{
    {1, {0.414286f, 0.419048f}, {0.575000f, 0.419048f}, 24.0f},
    {2, {0.432143f, 0.340476f}, {0.567857f, 0.342857f}, 27.0f},
    {3, {0.428571f, 0.290476f}, {0.571429f, 0.290476f}, 27.0f},
    {4, {0.407143f, 0.214286f}, {0.571429f, 0.211905f}, 25.0f},
    {6, {0.457143f, 0.169048f}, {0.528571f, 0.166667f}, 13.8f},
    {7, {0.478571f, 0.083333f}, {0.492857f, 0.121429f}, 17.5f},
    {8, {0.307143f, 0.285714f}, {0.357143f, 0.200000f}, 13.8f},
    {9, {0.282143f, 0.297619f}, {0.264286f, 0.390476f}, 9.4f},
    {10, {0.267857f, 0.397619f}, {0.257143f, 0.442857f}, 7.3f},
    {11, {0.264286f, 0.428571f}, {0.267857f, 0.445238f}, 5.0f},
    {12, {0.642857f, 0.180952f}, {0.717857f, 0.276190f}, 10.1f},
    {13, {0.717857f, 0.278571f}, {0.732143f, 0.390476f}, 9.4f},
    {14, {0.735714f, 0.404762f}, {0.750000f, 0.438095f}, 5.5f},
    {15, {0.735714f, 0.421429f}, {0.732143f, 0.435714f}, 5.0f},
    {17, {0.414286f, 0.397619f}, {0.400000f, 0.557143f}, 17.1f},
    {18, {0.403571f, 0.576190f}, {0.432143f, 0.719048f}, 15.3f},
    {19, {0.453571f, 0.740476f}, {0.385714f, 0.764286f}, 8.7f},
    {20, {0.585714f, 0.426190f}, {0.628571f, 0.566667f}, 14.2f},
    {21, {0.639286f, 0.576190f}, {0.682143f, 0.738095f}, 12.5f},
    {22, {0.678571f, 0.752381f}, {0.696429f, 0.778571f}, 10.3f},
    {23, {0.490f, 0.255f}, {0.490f, 0.195f}, 18.0f},
}};

// Shown until a match has resolved an agent model.
constexpr std::string_view k_fallback_model{"agents/models/ctm_sas/ctm_sas.vmdl_c"};

// One frame of the preview: where the model image sits and the player box the ESP elements are laid out against.
struct preview_stage
{
    ImDrawList* draw{};
    ImVec2 min{};
    ImVec2 max{};
    ImVec2 image_min{};
    ImVec2 unit_min{};
    ImVec2 unit_max{};
    // The chams preview rendered the model this frame, so bones and hitboxes come from its projection.
    bool live_model{};
    game::hitbox_catalog::set live_hitboxes{};

    [[nodiscard]] ImVec2 image_max() const { return image_min + k_image_size; }
    [[nodiscard]] ImVec2 unit_size() const { return unit_max - unit_min; }
    [[nodiscard]] ImVec2 point(const ImVec2 normalized) const
    {
        return image_min + ImVec2{k_image_size.x * normalized.x, k_image_size.y * normalized.y};
    }
    [[nodiscard]] ImVec2 anchor(const player_profile::layout_element& element) const
    {
        return ImVec2{player_profile::resolve_layout_axis(unit_min.x, unit_max.x, element.x,
                                                          player_profile::layout_reference_width),
                      player_profile::resolve_layout_axis(unit_min.y, unit_max.y, element.y,
                                                          player_profile::layout_reference_height)};
    }
};

struct preview_font
{
    ImFont* font{};
    float size{};
};

struct preview_fonts
{
    preview_font regular{};
    preview_font semibold{};
    preview_font weapon{};
};

[[nodiscard]] preview_font resolve_font(const render::draw::font* loaded)
{
    return {loaded && loaded->im_font ? loaded->im_font : ImGui::GetFont(),
            loaded ? loaded->font_size : ImGui::GetFontSize()};
}

[[nodiscard]] preview_fonts resolve_fonts()
{
    const auto& fonts = render::fonts();
    return {resolve_font(fonts.menu_regular_12), resolve_font(fonts.menu_semibold_13), resolve_font(fonts.weapons_15)};
}

// Opens the editor quickly and closes it a little faster; returns the eased reveal, or 0 once fully closed.
[[nodiscard]] float advance_reveal(float& animation, const bool requested)
{
    const auto delta_time = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    animation =
        requested ? std::min(1.0f, animation + delta_time / 0.12f) : std::max(0.0f, animation - delta_time / 0.07f);
    if (animation <= 0.0f)
        return 0.0f;
    const auto progress = std::clamp(animation, 0.0f, 1.0f);
    return requested ? 1.0f - std::pow(1.0f - progress, 3.0f) : progress;
}

void draw_panel_frame(ImDrawList* draw, const ImVec2 panel_min, const ImVec2 panel_max, const float reveal)
{
    draw->PushClipRectFullScreen();
    soft_shadow(draw, panel_min, panel_max, 20.0f, {0.0f, 14.0f}, 30.0f, {0, 0, 0, 1}, reveal * 0.85f);
    draw->PopClipRect();
    draw->AddRectFilled(panel_min, panel_max, packed(k_bg_base), 20.0f);
    draw->AddRect(panel_min, panel_max, packed(k_border), 20.0f);
    draw->AddLine({panel_min.x + 20.0f, panel_min.y + 1.0f}, {panel_max.x - 20.0f, panel_min.y + 1.0f},
                  IM_COL32(255, 255, 255, 20));
    draw_section_title(draw, panel_min + ImVec2{20.0f, 20.0f}, panel_max.x - 20.0f,
                       render::localization::tr("ESP EDITOR"));
}

// Renders the agent last seen in a match through the chams preview, with the configured chams material; null when GPU
// effects are off or the game archive is not available.
[[nodiscard]] ID3D11ShaderResourceView* render_live_model()
{
    if (!ui::bound_overlay().gpu_effects_enabled())
        return nullptr;

    const auto& chams = config::visual_settings.m_chams;
    const config::visual_profile::chams::material* material{};
    if (chams.enabled)
    {
        if (chams.visible.enabled)
            material = &chams.visible;
        else if (chams.invisible.enabled)
            material = &chams.invisible;
    }

    auto& renderer = features::chams::g_renderer;
    const auto& seen_model = renderer.diag().last_model_path;
    const auto model_path = seen_model.empty() ? std::string{k_fallback_model} : seen_model;
    static_cast<void>(renderer.ensure_vpk());
    if (!renderer.vpk_ready())
        return nullptr;
    return features::chams::g_preview.render(renderer.vpk(), model_path, static_cast<std::uint32_t>(k_image_size.x),
                                             static_cast<std::uint32_t>(k_image_size.y), material);
}

void draw_model_image(const preview_stage& stage, ID3D11ShaderResourceView* texture)
{
    if (texture)
    {
        const auto texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
        stage.draw->AddImage(ImTextureRef{texture_id}, stage.image_min, stage.image_max(), {0, 0}, {1, 1},
                             IM_COL32_WHITE);
    }
    else
    {
        stage.draw->AddText(stage.image_min + ImVec2{72.0f, 190.0f}, packed(k_text_muted),
                            render::localization::tr("PREVIEW UNAVAILABLE"));
    }
}

void draw_box(const preview_stage& stage, const player_profile::box& box)
{
    render::draw::draw_list box_draw{stage.draw};
    const auto x = stage.unit_min.x;
    const auto y = stage.unit_min.y;
    const auto width = stage.unit_size().x;
    const auto height = stage.unit_size().y;
    if (box.fill)
        box_draw.add_rect_filled(x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, {60, 200, 100, 80});
    if (box.style == player_profile::box::style_type::cornered)
    {
        const auto corner = std::min(box.corner_length, std::min(width, height) * 0.4f);
        if (box.outline)
        {
            box_draw.add_rect_cornered(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, {0, 0, 0, 180}, corner + 1.0f,
                                       1.0f);
            box_draw.add_rect_cornered(x, y, width, height, {0, 0, 0, 200}, corner, 2.0f);
        }
        box_draw.add_rect_cornered(x, y, width, height, box.visible_color, corner, 1.0f);
    }
    else
    {
        if (box.outline)
        {
            box_draw.add_rect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, {0, 0, 0, 180}, 1.0f);
            box_draw.add_rect(x, y, width, height, {0, 0, 0, 200}, 2.0f);
        }
        box_draw.add_rect(x, y, width, height, box.visible_color, 1.0f);
    }
}

// The hitboxes of the model the preview draws, from its own file, so the capsules and the head circle sit on that model
// (also in the main menu, before any match). The hitboxes last seen in a match only when the file has none.
[[nodiscard]] game::hitbox_catalog::set preview_hitboxes()
{
    game::hitbox_catalog::set result{};
    for (const auto& hitbox : features::chams::g_preview.hitboxes())
    {
        if (result.count >= static_cast<int>(result.entries.size()))
            break;
        auto& entry = result.entries[static_cast<std::size_t>(result.count++)];
        entry.index = hitbox.index;
        entry.bone = hitbox.bone;
        const auto length = std::min(hitbox.name.size(), entry.name.size() - 1);
        std::copy_n(hitbox.name.data(), length, entry.name.data());
        entry.mins = hitbox.mins;
        entry.maxs = hitbox.maxs;
        entry.radius = hitbox.radius;
    }
    return result.count > 0 ? result : game::hitbox_data().snapshot();
}

// Screen positions of the skeleton bones: projected from the live model where possible, the stock layout otherwise.
[[nodiscard]] std::array<ImVec2, k_preview_bone_count> skeleton_points(const preview_stage& stage)
{
    std::array<ImVec2, k_preview_bone_count> points{};
    for (const auto bone : k_preview_skeleton_bones)
    {
        foundation::vec3 world{};
        float x{}, y{};
        if (stage.live_model && features::chams::g_preview.bone_position(static_cast<std::uint32_t>(bone), world) &&
            features::chams::g_preview.project(world, x, y))
            points[bone] = stage.image_min + ImVec2{x, y};
        else
            points[bone] = stage.point(k_preview_bones[bone]);
    }
    return points;
}

// On-screen radius of a sphere of `radius` around `center`, measured perpendicular to `axis` and the view direction.
[[nodiscard]] float projected_radius(const foundation::vec3& center, const foundation::vec3& axis, const float radius,
                                     const float center_x, const float center_y)
{
    const auto view_direction = (center - features::chams::g_preview.eye()).normalized();
    auto perpendicular = axis.cross(view_direction);
    if (perpendicular.length() < 0.001f)
    {
        perpendicular = foundation::vec3{0.0f, 0.0f, 1.0f}.cross(view_direction);
        if (perpendicular.length() < 0.001f)
            perpendicular = foundation::vec3{1.0f, 0.0f, 0.0f};
    }
    perpendicular.normalize();

    float edge_x{}, edge_y{};
    if (!features::chams::g_preview.project(center + perpendicular * radius, edge_x, edge_y))
        return 0.0f;
    return std::sqrt((edge_x - center_x) * (edge_x - center_x) + (edge_y - center_y) * (edge_y - center_y));
}

[[nodiscard]] ImU32 with_alpha(render::draw::rgba color, const float alpha)
{
    color.a = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f));
    return packed(to_imvec(color));
}

void fill_capsule(ImDrawList* draw, const ImVec2 from, const ImVec2 to, const float radius, const ImU32 color)
{
    const auto delta = to - from;
    const auto axis_length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (axis_length < 1.0f)
    {
        draw->AddCircleFilled(from, radius, color, 24);
        return;
    }

    constexpr auto segments = 12;
    constexpr auto pi = std::numbers::pi_v<float>;
    const auto phi = std::atan2(delta.y, delta.x);
    std::array<ImVec2, (segments + 1) * 2> points{};
    int count{};
    for (int segment = 0; segment <= segments; ++segment)
    {
        const auto angle = phi + pi * 0.5f + pi * static_cast<float>(segment) / segments;
        points[count++] = from + ImVec2{std::cos(angle), std::sin(angle)} * radius;
    }
    for (int segment = 0; segment <= segments; ++segment)
    {
        const auto angle = phi - pi * 0.5f + pi * static_cast<float>(segment) / segments;
        points[count++] = to + ImVec2{std::cos(angle), std::sin(angle)} * radius;
    }
    draw->AddConvexPolyFilled(points.data(), count, color);
}

struct capsule
{
    ImVec2 from{};
    ImVec2 to{};
    float radius{};
};

// The capsule of one bone's hitbox on screen. With a live model only its real hitboxes count (no capsule when the bone
// has none or it does not project); otherwise the stock layout.
[[nodiscard]] capsule hitbox_capsule(const preview_stage& stage, const int bone)
{
    if (stage.live_model && stage.live_hitboxes.count > 0)
    {
        for (const auto& hitbox : stage.live_hitboxes)
        {
            if (hitbox.bone != bone)
                continue;

            const auto& preview = features::chams::g_preview;
            foundation::vec3 from_world{}, to_world{};
            if (!preview.bone_transform(static_cast<std::uint32_t>(bone), hitbox.mins, from_world) ||
                !preview.bone_transform(static_cast<std::uint32_t>(bone), hitbox.maxs, to_world))
                return {};

            float ax{}, ay{}, bx{}, by{}, cx{}, cy{};
            const auto center_world = (from_world + to_world) * 0.5f;
            if (!preview.project(from_world, ax, ay) || !preview.project(to_world, bx, by) ||
                !preview.project(center_world, cx, cy))
                return {};

            return {stage.image_min + ImVec2{ax, ay}, stage.image_min + ImVec2{bx, by},
                    projected_radius(center_world, to_world - from_world, hitbox.radius, cx, cy)};
        }
        return {};
    }

    const auto it = std::ranges::find(k_preview_hitboxes, bone, &preview_hitbox_geometry::bone);
    if (it == k_preview_hitboxes.end())
        return {};
    return {stage.point(it->from), stage.point(it->to), it->radius};
}

void draw_threat_hitboxes(const preview_stage& stage, const player_profile::threat_module& threat)
{
    const auto draw_group = [&](std::span<const int> bones, const bool enabled, const render::draw::rgba& color)
    {
        if (!enabled)
            return;
        for (const auto bone : bones)
        {
            const auto shape = hitbox_capsule(stage, bone);
            if (shape.radius <= 0.0f)
                continue;
            if (threat.outline_alpha > 0.0f)
                fill_capsule(stage.draw, shape.from, shape.to, shape.radius + std::max(1.0f, threat.outline_thickness),
                             with_alpha(color, threat.outline_alpha));
            if (threat.fill_alpha > 0.0f)
                fill_capsule(stage.draw, shape.from, shape.to, shape.radius, with_alpha(color, threat.fill_alpha));
        }
    };

    // Same layer order as the in-game threat highlights: head, body, then limbs.
    draw_group(features::visuals::player_t::threat_head_bones, threat.head_hitbox, threat.head_color);
    draw_group(features::visuals::player_t::threat_body_bones, threat.body_hitbox, threat.body_color);
    draw_group(features::visuals::player_t::threat_limb_bones, threat.limb_hitbox, threat.limb_color);
}

void draw_skeleton(const preview_stage& stage, const std::array<ImVec2, k_preview_bone_count>& points,
                   const player_profile::skeleton& skeleton)
{
    const auto color = packed(to_imvec(skeleton.visible_color));
    for (const auto& [from, to] : features::visuals::player_t::skeleton_connections)
        stage.draw->AddLine(points[from], points[to], color, std::max(1.0f, skeleton.thickness));
}

void draw_head_circle(const preview_stage& stage, const std::array<ImVec2, k_preview_bone_count>& points,
                      const player_profile::head_circle& head_circle)
{
    // The stock picture's head; on the live model the circle is the in-game one: around the centre of the head
    // hitbox with its radius, both projected through the preview camera.
    auto center = points[k_head_bone];
    auto radius = 19.5f;
    if (stage.live_model)
    {
        const auto& preview = features::chams::g_preview;
        const auto* head = std::ranges::find_if(
            stage.live_hitboxes, [](const auto& hitbox)
            { return game::hitbox_data().hitgroup_from_hitbox(hitbox.index) == game::rules::hitgroup::head; });
        foundation::vec3 center_world{};
        float x{}, y{};
        radius = 0.0f;
        if (head != stage.live_hitboxes.end() &&
            preview.bone_transform(static_cast<std::uint32_t>(head->bone), (head->mins + head->maxs) * 0.5f,
                                   center_world) &&
            preview.project(center_world, x, y))
        {
            center = stage.image_min + ImVec2{x, y};
            radius = projected_radius(center_world, foundation::vec3{}, head->radius, x, y);
        }
        else if (preview.bone_position(k_head_bone, center_world) && preview.project(center_world, x, y))
        {
            // No hitbox data at all: a head-sized circle on the head bone rather than the stock picture's size.
            constexpr auto k_head_radius = 4.5f;
            center = stage.image_min + ImVec2{x, y};
            radius = projected_radius(center_world, foundation::vec3{}, k_head_radius, x, y);
        }
    }
    if (radius > 0.0f)
        stage.draw->AddCircle(center, radius, packed(to_imvec(head_circle.color)), 40,
                              std::max(1.0f, head_circle.thickness));
}

void draw_view_line(const preview_stage& stage, const std::array<ImVec2, k_preview_bone_count>& points,
                    const player_profile::view_line& view_line)
{
    auto end = points[k_head_bone] + ImVec2{-32.0f, -7.0f};
    if (stage.live_model)
    {
        foundation::vec3 head{}, forward{};
        float x{}, y{};
        const auto& preview = features::chams::g_preview;
        if (preview.bone_position(k_head_bone, head) &&
            preview.bone_direction(k_head_bone, foundation::vec3{1.0f, 0.0f, 0.0f}, forward) &&
            preview.project(head + forward * std::max(1.0f, view_line.length), x, y))
            end = stage.image_min + ImVec2{x, y};
    }
    stage.draw->AddLine(points[k_head_bone], end, packed(to_imvec(view_line.color)),
                        std::max(1.0f, view_line.thickness));
}

struct screen_rect
{
    ImVec2 min{};
    ImVec2 max{};
};

// Text centered on `center` with a one-pixel black outline, as the in-game ESP draws it.
screen_rect draw_outlined_text(ImDrawList* draw, const std::string_view text, const preview_font& font,
                               const float size, const ImVec2 center, const ImU32 color)
{
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto measured = font.font->CalcTextSizeA(size, FLT_MAX, 0.0f, first, last);
    const auto position = center - measured * 0.5f;
    constexpr auto outline = IM_COL32(0, 0, 0, 225);
    draw->AddText(font.font, size, position + ImVec2{-1, 0}, outline, first, last);
    draw->AddText(font.font, size, position + ImVec2{1, 0}, outline, first, last);
    draw->AddText(font.font, size, position + ImVec2{0, -1}, outline, first, last);
    draw->AddText(font.font, size, position + ImVec2{0, 1}, outline, first, last);
    draw->AddText(font.font, size, position, color, first, last);
    return {position, position + measured};
}

constexpr int k_no_dock = -1;

// The four dock positions around the box, indexed like the bars' position_type (left, top, bottom, right).
[[nodiscard]] std::array<ImVec2, 4> dock_points(const preview_stage& stage)
{
    const auto center = (stage.unit_min + stage.unit_max) * 0.5f;
    return {ImVec2{stage.unit_min.x - 12.0f, center.y}, ImVec2{center.x, stage.unit_min.y - 12.0f},
            ImVec2{center.x, stage.unit_max.y + 12.0f}, ImVec2{stage.unit_max.x + 12.0f, center.y}};
}

// Shows the dock points while dragging and returns the one the element was dropped on, or k_no_dock.
[[nodiscard]] int track_docking(const preview_stage& stage, const bool dragging)
{
    const auto points = dock_points(stage);
    if (dragging)
    {
        for (const auto& point : points)
        {
            stage.draw->AddCircleFilled(point, 4.5f, IM_COL32(26, 25, 34, 235), 18);
            stage.draw->AddCircle(point, 5.0f, IM_COL32(255, 255, 255, 92), 18, 1.2f);
        }
    }

    constexpr auto snap_distance = 28.0f;
    auto nearest_distance = snap_distance;
    auto target = k_no_dock;
    const auto mouse = ImGui::GetMousePos();
    for (int point = 0; point < static_cast<int>(points.size()); ++point)
    {
        const auto delta = mouse - points[point];
        const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            target = point;
        }
    }
    if (dragging && target != k_no_dock)
    {
        stage.draw->AddCircleFilled(points[target], 8.0f, packed(ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.32f}),
                                    24);
        stage.draw->AddCircle(points[target], 7.0f, packed(k_accent), 24, 2.0f);
    }
    return ImGui::IsItemDeactivated() ? target : k_no_dock;
}

// Makes one drawn element editable: keeps it inside the stage, drags it, scales it with the wheel and opens its
// settings popup. Returns the dock point it was dropped on (only when docking is allowed), or k_no_dock.
int edit_element(const preview_stage& stage, const int id, player_profile::layout_element& element,
                 const screen_rect bounds, const bool allow_docking, const int settings_rows, callback_ref settings)
{
    const auto unit_size = stage.unit_size();
    ImVec2 correction{};
    if (bounds.min.x < stage.min.x)
        correction.x = stage.min.x - bounds.min.x;
    else if (bounds.max.x > stage.max.x)
        correction.x = stage.max.x - bounds.max.x;
    if (bounds.min.y < stage.min.y)
        correction.y = stage.min.y - bounds.min.y;
    else if (bounds.max.y > stage.max.y)
        correction.y = stage.max.y - bounds.max.y;
    element.x += correction.x / unit_size.x;
    element.y += correction.y / unit_size.y;

    constexpr auto hit_padding = ImVec2{5.0f, 5.0f};
    const auto hit_min = bounds.min - hit_padding;
    const auto hit_max = bounds.max + hit_padding;
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(hit_min);
    ImGui::InvisibleButton("##preview_element", hit_max - hit_min);
    const auto hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    const auto dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (dragging)
    {
        auto delta = ImGui::GetIO().MouseDelta;
        delta.x = std::clamp(delta.x, stage.min.x - bounds.min.x, stage.max.x - bounds.max.x);
        delta.y = std::clamp(delta.y, stage.min.y - bounds.min.y, stage.max.y - bounds.max.y);
        element.x += delta.x / unit_size.x;
        element.y += delta.y / unit_size.y;
    }
    const auto dock =
        allow_docking && (dragging || ImGui::IsItemDeactivated()) ? track_docking(stage, dragging) : k_no_dock;
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        element.scale = std::clamp(element.scale + ImGui::GetIO().MouseWheel * 0.08f, 0.55f, 2.0f);
    if (hovered || ImGui::IsItemActive())
        stage.draw->AddRect(hit_min, hit_max, IM_COL32(255, 255, 255, 150), 5.0f, 0, 1.0f);
    visual_editor_settings_popup(settings_rows, settings);
    ImGui::PopID();
    return dock;
}

enum element_id : int
{
    name_element,
    weapon_element,
    health_element,
    armor_element,
    flags_element = 5
};

void edit_name(const preview_stage& stage, const preview_fonts& fonts, player_profile& player)
{
    const auto bounds =
        draw_outlined_text(stage.draw, "PLAYER", fonts.semibold, fonts.semibold.size * player.m_layout.name.scale,
                           stage.anchor(player.m_layout.name), packed(to_imvec(player.m_name.color)));
    edit_element(stage, name_element, player.m_layout.name, bounds, false, 1,
                 [&] { color_row("Color", player.m_name.color); });
}

void include_rect(screen_rect& total, const screen_rect& part)
{
    total.min = {std::min(total.min.x, part.min.x), std::min(total.min.y, part.min.y)};
    total.max = {std::max(total.max.x, part.max.x), std::max(total.max.y, part.max.y)};
}

void edit_weapon(const preview_stage& stage, const preview_fonts& fonts, player_profile& player)
{
    using display_type = player_profile::weapon::display_type;
    const auto& weapon = player.m_weapon;
    const auto show_icon = weapon.display == display_type::icon || weapon.display == display_type::text_and_icon;
    const auto show_text = weapon.display == display_type::text || weapon.display == display_type::text_and_icon;
    if (!show_icon && !show_text)
        return;

    const auto scale = player.m_layout.weapon.scale;
    const auto icon = features::visuals::player_t::weapon_glyph("m4a1_silencer");
    constexpr std::string_view weapon_name{"M4A1-S"};
    const auto center = stage.anchor(player.m_layout.weapon);
    const auto icon_size = fonts.weapon.size * scale;
    const auto text_size = fonts.regular.size * scale;
    const auto icon_extent =
        show_icon ? fonts.weapon.font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon.data(), icon.data() + icon.size())
                  : ImVec2{};
    const auto text_extent = show_text ? fonts.regular.font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, weapon_name.data(),
                                                                           weapon_name.data() + weapon_name.size())
                                       : ImVec2{};
    const auto gap = show_icon && show_text ? 2.0f * scale : 0.0f;
    auto current_y = center.y - (icon_extent.y + text_extent.y + gap) * 0.5f;
    screen_rect bounds{{FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX}};
    if (show_icon)
    {
        const auto icon_center = ImVec2{center.x, current_y + icon_extent.y * 0.5f};
        const auto icon_bounds =
            draw_outlined_text(stage.draw, icon, fonts.weapon, icon_size, icon_center,
                               packed(to_imvec(weapon.ammo.enabled ? weapon.ammo.empty_color : weapon.icon_color)));
        if (weapon.ammo.enabled)
        {
            // Two thirds of the magazine left: the right part of the icon in the full color.
            constexpr auto preview_ammo_fraction = 20.0f / 30.0f;
            const auto split_x =
                icon_bounds.min.x + (icon_bounds.max.x - icon_bounds.min.x) * (1.0f - preview_ammo_fraction);
            stage.draw->PushClipRect({split_x, icon_bounds.min.y - 2.0f}, icon_bounds.max + ImVec2{2.0f, 2.0f}, true);
            draw_outlined_text(stage.draw, icon, fonts.weapon, icon_size, icon_center,
                               packed(to_imvec(weapon.icon_color)));
            stage.draw->PopClipRect();
        }
        include_rect(bounds, icon_bounds);
        current_y += icon_extent.y + gap;
    }
    if (show_text)
        include_rect(bounds, draw_outlined_text(stage.draw, weapon_name, fonts.regular, text_size,
                                                {center.x, current_y + text_extent.y * 0.5f},
                                                packed(to_imvec(weapon.text_color))));
    edit_element(stage, weapon_element, player.m_layout.weapon, bounds, false, k_weapon_settings_rows,
                 [&] { player_weapon_settings_rows(player.m_weapon); });
}

// Draws a health or armor bar at `fraction` fill with the configured gradient and segments; returns its bounds
// including the outline.
template<typename Bar>
screen_rect draw_bar(const preview_stage& stage, const Bar& bar, const player_profile::layout_element& layout,
                     const float fraction)
{
    auto* draw = stage.draw;
    const auto center = stage.anchor(layout);
    const auto scale = layout.scale;
    const auto position = static_cast<int>(bar.position);
    const auto vertical = position == 0 || position == 3;
    const auto thickness = std::clamp(bar.thickness, 1.0f, 12.0f) * scale;
    const auto size =
        vertical ? ImVec2{thickness, stage.unit_size().y * scale} : ImVec2{stage.unit_size().x * scale, thickness};
    const auto bar_min = center - size * 0.5f;
    const auto bar_max = center + size * 0.5f;
    const auto outline = std::clamp(bar.outline_thickness, 0.5f, 4.0f) * scale;
    if (bar.outline)
        draw->AddRectFilled(bar_min - ImVec2{outline, outline}, bar_max + ImVec2{outline, outline},
                            packed(to_imvec(bar.outline_color)));
    draw->AddRectFilled(bar_min, bar_max, packed(to_imvec(bar.background_color)));

    const auto clamped = std::clamp(fraction, 0.0f, 1.0f);
    const auto fill_min = vertical ? ImVec2{bar_min.x, bar_max.y - size.y * clamped} : bar_min;
    const auto fill_max = vertical ? bar_max : ImVec2{bar_min.x + size.x * clamped, bar_max.y};
    const auto full = packed(to_imvec(bar.full_color));
    const auto low = packed(to_imvec(bar.low_color));
    if (!bar.gradient)
        draw->AddRectFilled(fill_min, fill_max, full);
    else if (vertical)
        draw->AddRectFilledMultiColor(fill_min, fill_max, full, full, low, low);
    else
        draw->AddRectFilledMultiColor(fill_min, fill_max, low, full, full, low);

    const auto segments = std::clamp(bar.segments, 1, 10);
    const auto gap = std::clamp(bar.segment_gap, 0.0f, 4.0f) * scale;
    const auto separator = packed(to_imvec(bar.outline ? bar.outline_color : bar.background_color));
    for (int segment = 1; segment < segments && gap > 0.0f; ++segment)
    {
        const auto ratio = static_cast<float>(segment) / segments;
        if (vertical)
        {
            const auto y = bar_min.y + size.y * ratio;
            draw->AddRectFilled({bar_min.x, y - gap * 0.5f}, {bar_max.x, y + gap * 0.5f}, separator);
        }
        else
        {
            const auto x = bar_min.x + size.x * ratio;
            draw->AddRectFilled({x - gap * 0.5f, bar_min.y}, {x + gap * 0.5f, bar_max.y}, separator);
        }
    }
    return {bar_min - ImVec2{outline, outline}, bar_max + ImVec2{outline, outline}};
}

template<typename Bar>
void edit_bar(const preview_stage& stage, const int id, Bar& bar, player_profile::layout_element& layout,
              const float fraction)
{
    const auto bounds = draw_bar(stage, bar, layout, fraction);
    const auto dock = edit_element(stage, id, layout, bounds, true, k_bar_settings_rows,
                                   [&] { player_bar_settings_rows(bar, layout); });
    if (dock == k_no_dock)
        return;
    dock_player_bar(layout, dock);
    bar.position = static_cast<typename Bar::position_type>(dock);
}

void edit_info_flags(const preview_stage& stage, const preview_fonts& fonts, player_profile& player)
{
    const auto& info = player.m_info_flags;
    if (!info.enabled)
        return;
    using flag = player_profile::info_flags::flag;
    struct sample
    {
        flag kind{};
        std::string_view text;
        const player_profile::info_flags::style* style{};
    };
    // Example values, top to bottom in the in-game order. Kit and flashed are weapon-font glyphs.
    const std::array samples{sample{flag::money, "$4200", &info.money_style},
                             sample{flag::armor, "100 HK", &info.armor_style},
                             sample{flag::scoped, "ZOOM", &info.scoped_style},
                             sample{flag::defusing, "DEFUSING", &info.defusing_style},
                             sample{flag::ping, "42MS", &info.ping_style},
                             sample{flag::distance, "18M", &info.distance_style},
                             sample{flag::bomb_damage, "-48 HP", &info.bomb_damage_style},
                             sample{flag::kit, "r", &info.kit_style},
                             sample{flag::flashed, "i", &info.flashed_style}};

    const auto scale = player.m_layout.flags.scale;
    const auto start = stage.anchor(player.m_layout.flags);
    auto current_y = start.y;
    auto max_width = 0.0f;
    auto any_shown = false;
    for (const auto& [kind, text, style] : samples)
    {
        if (!info.has(kind))
            continue;
        any_shown = true;
        const auto glyph = kind == flag::kit || kind == flag::flashed;
        const auto& font = glyph ? fonts.weapon : fonts.regular;
        const auto size =
            glyph ? fonts.weapon.size * scale * style->scale : fonts.regular.size * 0.72f * scale * style->scale;
        const auto extent = font.font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
        stage.draw->AddText(font.font, size, {start.x, current_y}, packed(to_imvec(style->color)), text.data(),
                            text.data() + text.size());
        max_width = std::max(max_width, extent.x);
        current_y += extent.y;
    }
    if (any_shown)
        edit_element(stage, flags_element, player.m_layout.flags, {start, {start.x + max_width, current_y}}, false,
                     k_info_flag_settings_rows, [&] { player_info_flag_settings_rows(player.m_info_flags); });
}

// Dragging on the model (not on an element) turns the live preview around its vertical axis.
void orbit_model(bool& orbiting, const preview_stage& stage)
{
    const auto& io = ImGui::GetIO();
    const auto image_max = stage.image_max();
    const auto inside = io.MousePos.x >= stage.image_min.x && io.MousePos.x <= image_max.x &&
                        io.MousePos.y >= stage.image_min.y && io.MousePos.y <= image_max.y;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        orbiting = false;
    else if (!orbiting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inside && !ImGui::IsAnyItemActive())
        orbiting = true;

    if (orbiting)
    {
        features::chams::g_preview.orbit(io.MouseDelta.x);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
}
} // namespace

void menu_t::draw_visual_editor()
{
    const auto reveal = advance_reveal(m_visual_editor_animation, m_page == 2 && m_visual_group == 0);
    if (reveal <= 0.0f)
        return;

    auto display = ImGui::GetIO().DisplaySize;
    map_pointer_to_layout(display.x, display.y, display.x, display.y);
    auto panel_x = g_menu_max.x + 12.0f;
    if (panel_x + k_panel_size.x > display.x - 8.0f)
        panel_x = std::max(8.0f, display.x - k_panel_size.x - 8.0f);

    ImGui::SetNextWindowPos({panel_x + (1.0f - reveal) * 18.0f, g_menu_min.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(k_panel_size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##esp_visual_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

    const auto panel_min = ImGui::GetWindowPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw_panel_frame(draw, panel_min, panel_min + k_panel_size, reveal);

    // The model and the player box sit centered in the stage; the composition spans from slightly above the image top
    // to the feet.
    preview_stage stage{
        .draw = draw, .min = panel_min + ImVec2{16.0f, 54.0f}, .max = panel_min + ImVec2{304.0f, 634.0f}};
    constexpr auto composition_top = -16.0f;
    constexpr auto composition_bottom = 380.0f;
    stage.image_min = {stage.min.x + (stage.max.x - stage.min.x - k_image_size.x) * 0.5f,
                       stage.min.y + (stage.max.y - stage.min.y - (composition_bottom - composition_top)) * 0.5f -
                           composition_top};
    stage.unit_min = stage.point({0.185f, 0.035f});
    stage.unit_max = stage.point({0.800f, 0.828f});

    auto* live_texture = render_live_model();
    stage.live_model = live_texture != nullptr;
    stage.live_hitboxes = preview_hitboxes();

    draw->PushClipRect(stage.min + ImVec2{1.0f, 1.0f}, stage.max - ImVec2{1.0f, 1.0f}, true);
    draw_model_image(stage, live_texture ? live_texture : ui::bound_overlay().ct_preview_texture());

    auto& player = config::visual_settings.m_player;
    if (player.m_box.enabled)
        draw_box(stage, player.m_box);
    const auto points = skeleton_points(stage);
    if (player.m_threat_module.enabled)
        draw_threat_hitboxes(stage, player.m_threat_module);
    if (player.m_skeleton.enabled)
        draw_skeleton(stage, points, player.m_skeleton);
    if (player.m_head_circle.enabled)
        draw_head_circle(stage, points, player.m_head_circle);
    if (player.m_view_line.enabled)
        draw_view_line(stage, points, player.m_view_line);

    const auto fonts = resolve_fonts();
    if (player.m_name.enabled)
        edit_name(stage, fonts, player);
    if (player.m_weapon.enabled)
        edit_weapon(stage, fonts, player);
    if (player.m_health_bar.enabled)
        edit_bar(stage, health_element, player.m_health_bar, player.m_layout.health, 0.76f);
    if (player.m_armor_bar.enabled)
        edit_bar(stage, armor_element, player.m_armor_bar, player.m_layout.armor, 0.84f);
    edit_info_flags(stage, fonts, player);
    draw->PopClipRect();

    if (stage.live_model)
        orbit_model(m_preview_orbiting, stage);

    ImGui::End();
    ImGui::PopStyleVar(3);
}
} // namespace ui
