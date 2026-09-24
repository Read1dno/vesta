#include <stdafx.hpp>
#include <scripting/runtime.hpp>
#include <app/context.hpp>
#include <core/input/bindings.hpp>
#include <core/input/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/menu/localization.hpp>
#include <render/menu/menu.hpp>
#include <render/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <render/menu/internal.hpp>

using namespace render::menu::detail;

void menu_t::draw_visual_editor()
{
    const auto requested = this->m_page == 2 && this->m_visual_group == 0;
    const auto delta_time = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    if (requested)
    {
        this->m_visual_editor_animation =
            std::min(1.0f, this->m_visual_editor_animation + delta_time / 0.12f);
    }
    else
    {
        this->m_visual_editor_animation =
            std::max(0.0f, this->m_visual_editor_animation - delta_time / 0.07f);
    }
    if (this->m_visual_editor_animation <= 0.0f)
        return;

    const auto progress = std::clamp(this->m_visual_editor_animation, 0.0f, 1.0f);
    const auto reveal = requested ? 1.0f - std::pow(1.0f - progress, 3.0f) : progress;
    constexpr auto panel_size = ImVec2{320.0f, 650.0f};
    auto display = ImGui::GetIO().DisplaySize;
    this->map_pointer_to_layout(display.x, display.y, display.x, display.y);
    auto panel_x = g_menu_max.x + 12.0f;
    if (panel_x + panel_size.x > display.x - 8.0f)
        panel_x = std::max(8.0f, display.x - panel_size.x - 8.0f);
    const auto panel_position = ImVec2{panel_x + (1.0f - reveal) * 18.0f, g_menu_min.y};

    ImGui::SetNextWindowPos(panel_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##esp_visual_editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollbar);

    const auto panel_min = ImGui::GetWindowPos();
    const auto panel_max = panel_min + panel_size;
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRectFullScreen();
    soft_shadow(draw, panel_min, panel_max, 20.0f, {0.0f, 14.0f}, 30.0f, {0, 0, 0, 1}, reveal * 0.85f);
    draw->PopClipRect();
    draw->AddRectFilled(panel_min, panel_max, packed(k_bg_base), 20.0f);
    draw->AddRect(panel_min, panel_max, packed(k_border), 20.0f);
    draw->AddLine({panel_min.x + 20.0f, panel_min.y + 1.0f}, {panel_max.x - 20.0f, panel_min.y + 1.0f},
                  IM_COL32(255, 255, 255, 20));
    draw_section_title(draw, panel_min + ImVec2{20.0f, 20.0f}, panel_max.x - 20.0f,
                       render::localization::tr("ESP EDITOR"));

    const auto stage_min = panel_min + ImVec2{16.0f, 54.0f};
    const auto stage_max = panel_min + ImVec2{304.0f, 634.0f};

    const auto image_size = ImVec2{280.0f, 420.0f};

    constexpr auto composition_top = -16.0f;
    constexpr auto composition_bottom = 380.0f;
    const auto composition_height = composition_bottom - composition_top;
    const auto image_min =
        ImVec2{stage_min.x + (stage_max.x - stage_min.x - image_size.x) * 0.5f,
               stage_min.y + (stage_max.y - stage_min.y - composition_height) * 0.5f - composition_top};
    const auto image_max = image_min + image_size;

    const auto viewport_width = static_cast<std::uint32_t>(std::max(16.0f, image_size.x));
    const auto viewport_height = static_cast<std::uint32_t>(std::max(16.0f, image_size.y));

    auto &chams_cfg = config::visual_settings.m_chams;

    const config::visual_profile::chams::material *chams_material{};
    if (chams_cfg.enabled)
    {
        if (chams_cfg.visible.enabled)
            chams_material = &chams_cfg.visible;
        else if (chams_cfg.invisible.enabled)
            chams_material = &chams_cfg.invisible;
    }

    // Show whichever agent was last resolved in a match, so the editor previews
    // the model actually being drawn; before the first match, a stock CT agent.
    const auto &seen_model = chams::g_renderer.diag().last_model_path;
    const auto model_path =
        seen_model.empty() ? std::string{"agents/models/ctm_sas/ctm_sas.vmdl_c"} : seen_model;

    ID3D11ShaderResourceView *viewport_srv{};
    static_cast<void>(chams::g_renderer.ensure_vpk());
    if (chams::g_renderer.vpk_ready())
    {
        viewport_srv = chams::g_preview.render(chams::g_renderer.vpk(), model_path, viewport_width,
                                               viewport_height, chams_material);
    }

    const auto texture = viewport_srv ? viewport_srv : app::context().overlay.ct_preview_texture();
    draw->PushClipRect(stage_min + ImVec2{1.0f, 1.0f}, stage_max - ImVec2{1.0f, 1.0f}, true);
    if (texture)
    {
        const auto texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
        draw->AddImage(ImTextureRef{texture_id}, image_min, image_max, {0, 0}, {1, 1}, IM_COL32_WHITE);
    }
    else
    {
        draw->AddText(image_min + ImVec2{72.0f, 190.0f}, packed(k_text_muted),
                      render::localization::tr("PREVIEW UNAVAILABLE"));
    }

    auto &player = config::visual_settings.m_player;
    const auto point = [&](float x, float y) {
        return image_min + ImVec2{image_size.x * x, image_size.y * y};
    };
    const auto unit_min = point(0.185f, 0.035f);
    const auto unit_max = point(0.800f, 0.828f);
    const auto unit_size = unit_max - unit_min;

    if (player.m_box.enabled)
    {
        zdraw::draw_list box_draw{draw};
        const auto x = unit_min.x;
        const auto y = unit_min.y;
        const auto width = unit_size.x;
        const auto height = unit_size.y;
        if (player.m_box.fill)
        {
            box_draw.add_rect_filled(x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, {60, 200, 100, 80});
        }
        if (player.m_box.style == config::visual_profile::player::box::style_type::cornered)
        {
            const auto corner = std::min(player.m_box.corner_length, std::min(width, height) * 0.4f);
            if (player.m_box.outline)
            {
                box_draw.add_rect_cornered(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, {0, 0, 0, 180},
                                           corner + 1.0f, 1.0f);
                box_draw.add_rect_cornered(x, y, width, height, {0, 0, 0, 200}, corner, 2.0f);
            }
            box_draw.add_rect_cornered(x, y, width, height, player.m_box.visible_color, corner, 1.0f);
        }
        else
        {
            if (player.m_box.outline)
            {
                box_draw.add_rect(x - 1.0f, y - 1.0f, width + 2.0f, height + 2.0f, {0, 0, 0, 180}, 1.0f);
                box_draw.add_rect(x, y, width, height, {0, 0, 0, 200}, 2.0f);
            }
            box_draw.add_rect(x, y, width, height, player.m_box.visible_color, 1.0f);
        }
    }

    std::array<ImVec2, 24> skeleton_points{};
    for (const auto bone : k_preview_skeleton_bones)
    {
        bool projected{};
        if (viewport_srv)
        {
            foundation::vec3 world{};
            float x{}, y{};
            if (chams::g_preview.bone_position(static_cast<std::uint32_t>(bone), world) &&
                chams::g_preview.project(world, x, y))
            {
                skeleton_points[bone] = image_min + ImVec2{x, y};
                projected = true;
            }
        }

        if (!projected)
        {
            const auto normalized = k_preview_bones[bone];
            skeleton_points[bone] = point(normalized.x, normalized.y);
        }
    }

    const auto live_hitboxes = game::hitbox_data().snapshot();

    const auto project_radius = [&](const foundation::vec3 &center, const foundation::vec3 &axis,
                                    float radius, float cx, float cy) -> float {
        const auto view_dir = (center - chams::g_preview.eye()).normalized();
        auto perp = axis.cross(view_dir);
        if (perp.length() < 0.001f)
        {
            perp = foundation::vec3{0.0f, 0.0f, 1.0f}.cross(view_dir);
            if (perp.length() < 0.001f)
                perp = foundation::vec3{1.0f, 0.0f, 0.0f};
        }
        perp.normalize();

        float ex{}, ey{};
        if (!chams::g_preview.project(center + perp * radius, ex, ey))
            return 0.0f;
        return std::sqrt((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy));
    };

    if (player.m_threat_module.enabled)
    {
        const auto &threat = player.m_threat_module;
        const auto alpha_color = [&](zdraw::rgba color, float alpha) {
            color.a = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 255.0f));
            return packed(to_imvec(color));
        };
        const auto draw_capsule = [&](ImVec2 from, ImVec2 to, float radius, const zdraw::rgba &color) {
            const auto outline_radius = radius + std::max(1.0f, threat.outline_thickness);
            const auto draw_shape = [&](float shape_radius, ImU32 shape_color) {
                const auto delta = to - from;
                const auto axis_length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                if (axis_length < 1.0f)
                {
                    draw->AddCircleFilled(from, shape_radius, shape_color, 24);
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
                    points[count++] = from + ImVec2{std::cos(angle), std::sin(angle)} * shape_radius;
                }
                for (int segment = 0; segment <= segments; ++segment)
                {
                    const auto angle = phi - pi * 0.5f + pi * static_cast<float>(segment) / segments;
                    points[count++] = to + ImVec2{std::cos(angle), std::sin(angle)} * shape_radius;
                }
                draw->AddConvexPolyFilled(points.data(), count, shape_color);
            };

            if (threat.outline_alpha > 0.0f)
                draw_shape(outline_radius, alpha_color(color, threat.outline_alpha));
            if (threat.fill_alpha > 0.0f)
                draw_shape(radius, alpha_color(color, threat.fill_alpha));
        };

        const auto hitbox_axis = [&](int bone) -> std::tuple<ImVec2, ImVec2, float> {
            if (viewport_srv && live_hitboxes.count > 0)
            {
                for (const auto &hitbox : live_hitboxes)
                {
                    if (hitbox.bone != bone)
                        continue;

                    foundation::vec3 from_world{}, to_world{};
                    if (!chams::g_preview.bone_transform(static_cast<std::uint32_t>(bone), hitbox.mins,
                                                         from_world) ||
                        !chams::g_preview.bone_transform(static_cast<std::uint32_t>(bone), hitbox.maxs,
                                                         to_world))
                    {
                        break;
                    }

                    float ax{}, ay{}, bx{}, by{}, cx{}, cy{};
                    const auto center_world = (from_world + to_world) * 0.5f;
                    if (!chams::g_preview.project(from_world, ax, ay) ||
                        !chams::g_preview.project(to_world, bx, by) ||
                        !chams::g_preview.project(center_world, cx, cy))
                    {
                        break;
                    }

                    const auto radius_px =
                        project_radius(center_world, to_world - from_world, hitbox.radius, cx, cy);

                    return {image_min + ImVec2{ax, ay}, image_min + ImVec2{bx, by}, radius_px};
                }
            }

            if (viewport_srv && live_hitboxes.count > 0)
                return {{}, {}, 0.0f};

            const auto it =
                std::find_if(k_preview_hitboxes.begin(), k_preview_hitboxes.end(),
                             [&](const preview_hitbox_geometry &hitbox) { return hitbox.bone == bone; });
            if (it == k_preview_hitboxes.end())
                return {{}, {}, 0.0f};
            return {point(it->from.x, it->from.y), point(it->to.x, it->to.y), it->radius};
        };
        const auto draw_hitbox = [&](int bone, const zdraw::rgba &color) {
            const auto [from, to, radius] = hitbox_axis(bone);
            if (radius > 0.0f)
                draw_capsule(from, to, radius, color);
        };
        const auto draw_group = [&](std::span<const int> bones, bool enabled, const zdraw::rgba &color) {
            if (!enabled)
                return;
            for (const auto bone : bones)
                draw_hitbox(bone, color);
        };

        // Keep the same layer order as paint_threat_highlights(): head, body, then limbs.
        draw_group(features::visuals::player_t::threat_head_bones, threat.head_hitbox, threat.head_color);
        draw_group(features::visuals::player_t::threat_body_bones, threat.body_hitbox, threat.body_color);
        draw_group(features::visuals::player_t::threat_limb_bones, threat.limb_hitbox, threat.limb_color);
    }
    if (player.m_skeleton.enabled)
    {
        const auto skeleton_color = packed(to_imvec(player.m_skeleton.visible_color));
        for (const auto &[from, to] : features::visuals::player_t::skeleton_connections)
        {
            draw->AddLine(skeleton_points[from], skeleton_points[to], skeleton_color,
                          std::max(1.0f, player.m_skeleton.thickness));
        }
    }
    if (player.m_head_circle.enabled)
    {

        auto circle_center = skeleton_points[7];
        auto circle_radius = 19.5f;

        if (viewport_srv)
        {
            for (const auto &hitbox : live_hitboxes)
            {
                if (game::hitbox_data().hitgroup_from_hitbox(hitbox.index) != 1)
                    continue;

                foundation::vec3 center_world{};
                const auto center_local = (hitbox.mins + hitbox.maxs) * 0.5f;
                float cx{}, cy{};
                if (!chams::g_preview.bone_transform(static_cast<std::uint32_t>(hitbox.bone), center_local,
                                                     center_world) ||
                    !chams::g_preview.project(center_world, cx, cy))
                {
                    break;
                }

                circle_center = image_min + ImVec2{cx, cy};
                circle_radius =
                    project_radius(center_world, foundation::vec3{0.0f, 0.0f, 0.0f}, hitbox.radius, cx, cy);
                break;
            }
        }

        if (circle_radius > 0.0f)
        {
            draw->AddCircle(circle_center, circle_radius, packed(to_imvec(player.m_head_circle.color)), 40,
                            std::max(1.0f, player.m_head_circle.thickness));
        }
    }
    if (player.m_view_line.enabled)
    {

        auto view_line_end = skeleton_points[7] + ImVec2{-32.0f, -7.0f};
        if (viewport_srv)
        {
            foundation::vec3 head{};
            float x{}, y{};

            foundation::vec3 forward{};
            if (chams::g_preview.bone_position(7, head) &&
                chams::g_preview.bone_direction(7, foundation::vec3{1.0f, 0.0f, 0.0f}, forward) &&
                chams::g_preview.project(head + forward * std::max(1.0f, player.m_view_line.length), x, y))
            {
                view_line_end = image_min + ImVec2{x, y};
            }
        }

        draw->AddLine(skeleton_points[7], view_line_end, packed(to_imvec(player.m_view_line.color)),
                      std::max(1.0f, player.m_view_line.thickness));
    }

    const auto anchor = [&](const config::visual_profile::player::layout_element &element) {
        return ImVec2{
            config::visual_profile::player::resolve_layout_axis(
                unit_min.x, unit_max.x, element.x, config::visual_profile::player::layout_reference_width),
            config::visual_profile::player::resolve_layout_axis(
                unit_min.y, unit_max.y, element.y, config::visual_profile::player::layout_reference_height)};
    };
    const auto draw_preview_text = [&](std::string_view text, ImFont *font, float size, ImVec2 center,
                                       ImU32 color) {
        const auto measured =
            font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
        const auto position = center - measured * 0.5f;
        const auto outline = IM_COL32(0, 0, 0, 225);
        draw->AddText(font, size, position + ImVec2{-1, 0}, outline, text.data(), text.data() + text.size());
        draw->AddText(font, size, position + ImVec2{1, 0}, outline, text.data(), text.data() + text.size());
        draw->AddText(font, size, position + ImVec2{0, -1}, outline, text.data(), text.data() + text.size());
        draw->AddText(font, size, position + ImVec2{0, 1}, outline, text.data(), text.data() + text.size());
        draw->AddText(font, size, position, color, text.data(), text.data() + text.size());
        return std::pair{position, position + measured};
    };
    const std::array dock_points{ImVec2{unit_min.x - 12.0f, (unit_min.y + unit_max.y) * 0.5f},
                                 ImVec2{(unit_min.x + unit_max.x) * 0.5f, unit_min.y - 12.0f},
                                 ImVec2{(unit_min.x + unit_max.x) * 0.5f, unit_max.y + 12.0f},
                                 ImVec2{unit_max.x + 12.0f, (unit_min.y + unit_max.y) * 0.5f}};
    const auto interact = [&](int id, config::visual_profile::player::layout_element &element,
                              ImVec2 item_min, ImVec2 item_max, bool allow_docking, int settings_rows,
                              auto &&settings) {
        int dock_target = -1;
        const auto constrain = [&](ImVec2 min, ImVec2 max) {
            ImVec2 correction{};
            if (min.x < stage_min.x)
                correction.x = stage_min.x - min.x;
            else if (max.x > stage_max.x)
                correction.x = stage_max.x - max.x;
            if (min.y < stage_min.y)
                correction.y = stage_min.y - min.y;
            else if (max.y > stage_max.y)
                correction.y = stage_max.y - max.y;
            element.x += correction.x / unit_size.x;
            element.y += correction.y / unit_size.y;
        };
        constrain(item_min, item_max);
        constexpr auto hit_padding = 5.0f;
        const auto hit_min = item_min - ImVec2{hit_padding, hit_padding};
        const auto hit_max = item_max + ImVec2{hit_padding, hit_padding};
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(hit_min);
        ImGui::InvisibleButton("##preview_element", hit_max - hit_min);
        const auto hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            auto delta = ImGui::GetIO().MouseDelta;
            delta.x = std::clamp(delta.x, stage_min.x - item_min.x, stage_max.x - item_max.x);
            delta.y = std::clamp(delta.y, stage_min.y - item_min.y, stage_max.y - item_max.y);
            element.x += delta.x / unit_size.x;
            element.y += delta.y / unit_size.y;
        }
        const auto dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        if (allow_docking && (dragging || ImGui::IsItemDeactivated()))
        {
            if (dragging)
            {
                for (const auto &point : dock_points)
                {
                    draw->AddCircleFilled(point, 4.5f, IM_COL32(26, 25, 34, 235), 18);
                    draw->AddCircle(point, 5.0f, IM_COL32(255, 255, 255, 92), 18, 1.2f);
                }
            }
            const auto mouse = ImGui::GetMousePos();
            auto nearest_distance = 28.0f;
            for (int point = 0; point < static_cast<int>(dock_points.size()); ++point)
            {
                const auto delta = mouse - dock_points[point];
                const auto distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    dock_target = point;
                }
            }
            if (dragging && dock_target >= 0)
            {
                draw->AddCircleFilled(dock_points[dock_target], 8.0f,
                                      packed(ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.32f}), 24);
                draw->AddCircle(dock_points[dock_target], 7.0f, packed(k_accent), 24, 2.0f);
            }
            if (!ImGui::IsItemDeactivated())
                dock_target = -1;
        }
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            element.scale = std::clamp(element.scale + ImGui::GetIO().MouseWheel * 0.08f, 0.55f, 2.0f);
        }
        if (hovered || ImGui::IsItemActive())
        {
            draw->AddRect(hit_min, hit_max, IM_COL32(255, 255, 255, 150), 5.0f, 0, 1.0f);
        }
        visual_editor_settings_popup(settings_rows, std::forward<decltype(settings)>(settings));
        ImGui::PopID();
        return dock_target;
    };

    const auto *regular_wrapper = app::context().overlay.fonts().menu_regular_12;
    const auto *semibold_wrapper = app::context().overlay.fonts().menu_semibold_13;
    const auto *weapon_wrapper = app::context().overlay.fonts().weapons_15;
    auto *regular_font =
        regular_wrapper && regular_wrapper->im_font ? regular_wrapper->im_font : ImGui::GetFont();
    auto *semibold_font =
        semibold_wrapper && semibold_wrapper->im_font ? semibold_wrapper->im_font : ImGui::GetFont();
    auto *weapon_font =
        weapon_wrapper && weapon_wrapper->im_font ? weapon_wrapper->im_font : ImGui::GetFont();
    const auto regular_size = regular_wrapper ? regular_wrapper->font_size : ImGui::GetFontSize();
    const auto semibold_size = semibold_wrapper ? semibold_wrapper->font_size : ImGui::GetFontSize();
    const auto weapon_size = weapon_wrapper ? weapon_wrapper->font_size : ImGui::GetFontSize();

    if (player.m_name.enabled)
    {
        const auto bounds =
            draw_preview_text("PLAYER", semibold_font, semibold_size * player.m_layout.name.scale,
                              anchor(player.m_layout.name), packed(to_imvec(player.m_name.color)));
        interact(0, player.m_layout.name, bounds.first, bounds.second, false, 1,
                 [&] { color_row("Color", player.m_name.color); });
    }
    if (player.m_weapon.enabled)
    {
        const auto show_icon =
            player.m_weapon.display == config::visual_profile::player::weapon::display_type::icon ||
            player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
        const auto show_text =
            player.m_weapon.display == config::visual_profile::player::weapon::display_type::text ||
            player.m_weapon.display == config::visual_profile::player::weapon::display_type::text_and_icon;
        const auto icon = features::visuals::player_t::weapon_glyph("m4a1_silencer");
        const auto center = anchor(player.m_layout.weapon);
        const auto icon_size = weapon_size * player.m_layout.weapon.scale;
        const auto text_size = regular_size * player.m_layout.weapon.scale;
        const auto icon_extent = show_icon ? weapon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon.data(),
                                                                        icon.data() + icon.size())
                                           : ImVec2{};
        constexpr std::string_view weapon_name{"M4A1-S"};
        const auto text_extent =
            show_text ? regular_font->CalcTextSizeA(text_size, FLT_MAX, 0.0f, weapon_name.data(),
                                                    weapon_name.data() + weapon_name.size())
                      : ImVec2{};
        const auto gap = show_icon && show_text ? 2.0f * player.m_layout.weapon.scale : 0.0f;
        const auto total_height = icon_extent.y + text_extent.y + gap;
        auto current_y = center.y - total_height * 0.5f;
        auto item_min = ImVec2{FLT_MAX, FLT_MAX};
        auto item_max = ImVec2{-FLT_MAX, -FLT_MAX};
        if (show_icon)
        {
            const auto icon_center = ImVec2{center.x, current_y + icon_extent.y * 0.5f};
            auto bounds = draw_preview_text(
                icon, weapon_font, icon_size, icon_center,
                packed(to_imvec(player.m_weapon.ammo.enabled ? player.m_weapon.ammo.empty_color
                                                             : player.m_weapon.icon_color)));
            if (player.m_weapon.ammo.enabled)
            {
                constexpr auto preview_ammo_fraction = 20.0f / 30.0f;
                const auto split_x =
                    bounds.first.x + (bounds.second.x - bounds.first.x) * (1.0f - preview_ammo_fraction);
                draw->PushClipRect({split_x, bounds.first.y - 2.0f}, bounds.second + ImVec2{2.0f, 2.0f},
                                   true);
                draw_preview_text(icon, weapon_font, icon_size, icon_center,
                                  packed(to_imvec(player.m_weapon.icon_color)));
                draw->PopClipRect();
            }
            item_min = {std::min(item_min.x, bounds.first.x), std::min(item_min.y, bounds.first.y)};
            item_max = {std::max(item_max.x, bounds.second.x), std::max(item_max.y, bounds.second.y)};
            current_y += icon_extent.y + gap;
        }
        if (show_text)
        {
            const auto bounds = draw_preview_text(weapon_name, regular_font, text_size,
                                                  {center.x, current_y + text_extent.y * 0.5f},
                                                  packed(to_imvec(player.m_weapon.text_color)));
            item_min = {std::min(item_min.x, bounds.first.x), std::min(item_min.y, bounds.first.y)};
            item_max = {std::max(item_max.x, bounds.second.x), std::max(item_max.y, bounds.second.y)};
        }
        if (show_icon || show_text)
            interact(1, player.m_layout.weapon, item_min, item_max, false, k_weapon_settings_rows,
                     [&] { player_weapon_settings_rows(player.m_weapon); });
    }
    const auto draw_preview_bar =
        [&](const auto &bar, const config::visual_profile::player::layout_element &layout, float fraction) {
            const auto center = anchor(layout);
            const auto scale = layout.scale;
            const auto position = static_cast<int>(bar.position);
            const auto vertical = position == 0 || position == 3;
            const auto thickness = std::clamp(bar.thickness, 1.0f, 12.0f) * scale;
            const auto size =
                vertical ? ImVec2{thickness, unit_size.y * scale} : ImVec2{unit_size.x * scale, thickness};
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
            if (bar.gradient)
            {
                if (vertical)
                    draw->AddRectFilledMultiColor(fill_min, fill_max, packed(to_imvec(bar.full_color)),
                                                  packed(to_imvec(bar.full_color)),
                                                  packed(to_imvec(bar.low_color)),
                                                  packed(to_imvec(bar.low_color)));
                else
                    draw->AddRectFilledMultiColor(
                        fill_min, fill_max, packed(to_imvec(bar.low_color)), packed(to_imvec(bar.full_color)),
                        packed(to_imvec(bar.full_color)), packed(to_imvec(bar.low_color)));
            }
            else
                draw->AddRectFilled(fill_min, fill_max, packed(to_imvec(bar.full_color)));

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
            return std::pair{bar_min - ImVec2{outline, outline}, bar_max + ImVec2{outline, outline}};
        };
    if (player.m_health_bar.enabled)
    {
        const auto bounds = draw_preview_bar(player.m_health_bar, player.m_layout.health, 0.76f);
        const auto dock =
            interact(2, player.m_layout.health, bounds.first, bounds.second, true, k_bar_settings_rows,
                     [&] { player_bar_settings_rows(player.m_health_bar, player.m_layout.health); });
        if (dock >= 0)
        {
            dock_player_bar(player.m_layout.health, dock);
            player.m_health_bar.position =
                static_cast<config::visual_profile::player::health_bar::position_type>(dock);
        }
    }
    if (player.m_armor_bar.enabled)
    {
        const auto bounds = draw_preview_bar(player.m_armor_bar, player.m_layout.armor, 0.84f);
        const auto dock =
            interact(3, player.m_layout.armor, bounds.first, bounds.second, true, k_bar_settings_rows,
                     [&] { player_bar_settings_rows(player.m_armor_bar, player.m_layout.armor); });
        if (dock >= 0)
        {
            dock_player_bar(player.m_layout.armor, dock);
            player.m_armor_bar.position =
                static_cast<config::visual_profile::player::armor_bar::position_type>(dock);
        }
    }
    {

        const auto &info = player.m_info_flags;
        using flag = config::visual_profile::player::info_flags::flag;

        struct preview_flag
        {
            std::string_view text;
            const config::visual_profile::player::info_flags::style *style{};
            flag kind{};
        };
        std::array<preview_flag, 9> flags{};
        std::size_t flag_count{};
        const auto add_flag = [&](bool shown, std::string_view text,
                                  const config::visual_profile::player::info_flags::style &style, flag kind) {
            if (shown && flag_count < flags.size())
            {
                flags[flag_count++] = {text, &style, kind};
            }
        };

        add_flag(info.enabled && info.has(flag::money), "$4200", info.money_style, flag::money);
        add_flag(info.enabled && info.has(flag::armor), "100 HK", info.armor_style, flag::armor);
        add_flag(info.enabled && info.has(flag::scoped), "ZOOM", info.scoped_style, flag::scoped);
        add_flag(info.enabled && info.has(flag::defusing), "DEFUSING", info.defusing_style, flag::defusing);
        add_flag(info.enabled && info.has(flag::ping), "42MS", info.ping_style, flag::ping);
        add_flag(info.enabled && info.has(flag::distance), "18M", info.distance_style, flag::distance);
        add_flag(info.enabled && info.has(flag::bomb_damage), "-48 HP", info.bomb_damage_style,
                 flag::bomb_damage);
        add_flag(info.enabled && info.has(flag::kit), "KIT", info.kit_style, flag::kit);
        add_flag(info.enabled && info.has(flag::flashed), "FLASHED", info.flashed_style, flag::flashed);

        if (flag_count > 0)
        {
            const auto scale = player.m_layout.flags.scale;
            const auto start = anchor(player.m_layout.flags);
            auto current_y = start.y;
            auto max_width = 0.0f;
            for (std::size_t i = 0; i < flag_count; ++i)
            {
                const auto &[text, style, kind] = flags[i];
                const auto size = regular_size * 0.72f * scale * style->scale;
                const auto measured =
                    regular_font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size());
                if (kind == flag::kit)
                {
                    const auto glyph_size = weapon_size * scale * style->scale;
                    constexpr std::string_view glyph{"r"};
                    const auto extent = weapon_font->CalcTextSizeA(glyph_size, FLT_MAX, 0.0f, glyph.data(),
                                                                   glyph.data() + glyph.size());
                    draw->AddText(weapon_font, glyph_size, {start.x, current_y},
                                  packed(to_imvec(style->color)), glyph.data(), glyph.data() + glyph.size());
                    max_width = std::max(max_width, extent.x);
                    current_y += extent.y;
                }
                else if (kind == flag::flashed)
                {
                    const auto glyph_size = weapon_size * scale * style->scale;
                    constexpr std::string_view glyph{"i"};
                    const auto extent = weapon_font->CalcTextSizeA(glyph_size, FLT_MAX, 0.0f, glyph.data(),
                                                                   glyph.data() + glyph.size());
                    draw->AddText(weapon_font, glyph_size, {start.x, current_y},
                                  packed(to_imvec(style->color)), glyph.data(), glyph.data() + glyph.size());
                    max_width = std::max(max_width, extent.x);
                    current_y += extent.y;
                }
                else
                {
                    draw->AddText(regular_font, size, {start.x, current_y}, packed(to_imvec(style->color)),
                                  text.data(), text.data() + text.size());
                    max_width = std::max(max_width, measured.x);
                    current_y += measured.y;
                }
            }
            interact(5, player.m_layout.flags, start, {start.x + max_width, current_y}, false,
                     k_info_flag_settings_rows, [&] { player_info_flag_settings_rows(player.m_info_flags); });
        }
    }

    draw->PopClipRect();

    if (viewport_srv)
    {
        const auto &io = ImGui::GetIO();
        const auto inside = io.MousePos.x >= image_min.x && io.MousePos.x <= image_max.x &&
                            io.MousePos.y >= image_min.y && io.MousePos.y <= image_max.y;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            g_preview_orbiting = false;
        }
        else if (!g_preview_orbiting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inside &&
                 !ImGui::IsAnyItemActive())
        {
            g_preview_orbiting = true;
        }

        if (g_preview_orbiting)
        {
            chams::g_preview.orbit(io.MouseDelta.x);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}
