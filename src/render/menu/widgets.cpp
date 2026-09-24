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

namespace render::menu::detail
{
void from_imvec(const ImVec4 &value, zdraw::rgba &color)
{
    color.r = static_cast<std::uint8_t>(std::clamp(value.x, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.g = static_cast<std::uint8_t>(std::clamp(value.y, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.b = static_cast<std::uint8_t>(std::clamp(value.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    color.a = static_cast<std::uint8_t>(std::clamp(value.w, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void toggle_control(bool &value)
{
    constexpr auto switch_size = ImVec2{46.0f, 24.0f};
    const auto id = ImGui::GetID("##toggle");
    if (ImGui::InvisibleButton("##toggle", switch_size))
    {
        value = !value;
    }

    auto &animation = g_toggle_animations[id];
    spring_to(animation.position, animation.velocity, value ? 1.0f : 0.0f);
    animation.reveal = approach(animation.reveal, 1.0f, 0.22f);

    const auto reveal = 1.0f - std::pow(1.0f - std::clamp(animation.reveal, 0.0f, 1.0f), 3.0f);
    const auto raw_min = ImGui::GetItemRectMin();
    const auto raw_max = ImGui::GetItemRectMax();
    const auto center = (raw_min + raw_max) * 0.5f;
    const auto half_size =
        ImVec2{switch_size.x * 0.5f * reveal, switch_size.y * 0.5f * (0.82f + reveal * 0.18f)};
    const auto min = center - half_size;
    const auto max = center + half_size;
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    const auto hover = animate_state(g_hover_animations, id, hovered, 0.16f);
    const auto position = std::clamp(animation.position, 0.0f, 1.0f);
    auto *draw = ImGui::GetWindowDrawList();
    const auto off_track = mix(ImVec4{38.0f / 255.0f, 38.0f / 255.0f, 48.0f / 255.0f, 0.88f},
                               ImVec4{50.0f / 255.0f, 50.0f / 255.0f, 62.0f / 255.0f, 0.96f}, hover);
    auto track = mix(off_track, k_accent, position);
    track.w *= reveal;

    draw->AddRectFilled(min, max, packed(track), 12.0f);

    const auto knob_target_x = raw_min.x + 12.0f + 22.0f * position;
    const auto knob_center = ImVec2{center.x + (knob_target_x - center.x) * reveal, center.y};
    const auto motion = std::min(std::abs(animation.velocity) * 0.018f, 0.16f);
    const auto knob_radius = (9.0f + hover * 0.35f) * reveal;
    draw->AddCircleFilled(knob_center + ImVec2{0.0f, 1.5f}, knob_radius + 1.0f,
                          IM_COL32(0, 0, 0, static_cast<int>(55.0f * reveal)), 28);
    draw->AddEllipseFilled(knob_center,
                           {knob_radius * (1.0f + motion), knob_radius * (1.0f - motion * 0.45f)},
                           IM_COL32(250, 250, 252, static_cast<int>(255.0f * reveal)), 0.0f, 28);
    draw->AddCircle(knob_center, knob_radius, IM_COL32(255, 255, 255, static_cast<int>(110.0f * reveal)), 28,
                    1.0f);
}

void toggle_row(const char *label, bool &value)
{
    begin_row(label, 46.0f);
    toggle_control(value);
    end_row();
}

void toggle_color_row(const char *label, bool &value, zdraw::rgba &color)
{
    constexpr auto options_width = 28.0f;
    constexpr auto spacing = 8.0f;
    constexpr auto switch_width = 46.0f;
    begin_row(label, options_width + spacing + switch_width);

    const auto picker_id = ImGui::GetID("##picker");
    ImGui::InvisibleButton("##color_options", {options_width, 24.0f});
    const auto options_min = ImGui::GetItemRectMin();
    const auto options_max = ImGui::GetItemRectMax();
    pointer_cursor_if_hovered();
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##picker");

    auto *draw = ImGui::GetWindowDrawList();
    const auto dot_y = (options_min.y + options_max.y) * 0.5f;
    for (int dot = 0; dot < 3; ++dot)
    {
        draw->AddCircleFilled({options_min.x + 5.0f + dot * 9.0f, dot_y}, 1.08f, IM_COL32_WHITE, 12);
    }

    ImGui::SetCursorScreenPos({options_max.x + spacing, options_min.y});
    toggle_control(value);
    color_picker_popup(color, options_min, options_max, picker_id);
    end_row();
}

void slider_row(const char *label, int &value, int minimum, int maximum, const char *suffix)
{
    slider_row_impl(label, value, minimum, maximum, suffix, 1);
}

void slider_row(const char *label, float &value, float minimum, float maximum, const char *suffix, float step)
{
    slider_row_impl(label, value, minimum, maximum, suffix, step);
}

void slider_percent_row(const char *label, float &value)
{
    auto percent = std::clamp(value, 0.0f, 1.0f) * 100.0f;
    slider_row_impl(label, percent, 0.0f, 100.0f, "%", 1.0f);
    value = percent * 0.01f;
}

void draw_checkmark(ImDrawList *draw, ImVec2 center, ImU32 color, float amount, float thickness)
{
    if (amount <= 0.01f)
        return;

    const auto a = ImVec2{center.x - 4.0f, center.y};
    const auto b = ImVec2{center.x - 1.0f, center.y + 3.0f};
    const auto c = ImVec2{center.x + 5.0f, center.y - 4.0f};
    if (amount < 0.42f)
    {
        const auto t = amount / 0.42f;
        draw->AddLine(a, a + (b - a) * t, color, thickness);
        return;
    }

    draw->AddLine(a, b, color, thickness);
    const auto t = (amount - 0.42f) / 0.58f;
    draw->AddLine(b, b + (c - b) * std::clamp(t, 0.0f, 1.0f), color, thickness);
}

void draw_dropdown_chevron(ImDrawList *draw, ImVec2 center, ImU32 color, float open_amount)
{
    const auto points = render::menu::chevron(open_amount);
    const auto left = center + ImVec2{points[0].x, points[0].y};
    const auto middle = center + ImVec2{points[1].x, points[1].y};
    const auto right = center + ImVec2{points[2].x, points[2].y};
    draw->AddLine(left, middle, color, 1.55f);
    draw->AddLine(middle, right, color, 1.55f);
}

void select_row(const char *label, int &value, std::span<const char *const> options)
{
    if (options.empty())
        return;

    constexpr auto control_width = 140.0f;
    constexpr auto control_height = 30.0f;
    constexpr auto option_height = 34.0f;

    auto popup_width = control_width;
    for (const auto *option : options)
    {
        popup_width = std::max(popup_width, ImGui::CalcTextSize(render::localization::tr(option)).x + 48.0f);
    }

    begin_row(label, control_width);
    const auto index = std::clamp(value, 0, static_cast<int>(options.size()) - 1);
    const auto button_id = ImGui::GetID("##select_button");
    ImGui::InvisibleButton("##select_button", {control_width, control_height});
    if (ImGui::IsItemClicked())
    {
        ImGui::OpenPopup("##options");
    }
    pointer_cursor_if_hovered();
    const auto button_hovered = ImGui::IsItemHovered();
    const auto popup_open = ImGui::IsPopupOpen("##options");
    const auto hover_t = animate_state(g_hover_animations, button_id, button_hovered, 0.14f);
    const auto open_t = animate_state(g_active_animations, button_id, popup_open, 0.16f);
    const auto button_min = ImGui::GetItemRectMin();
    const auto button_max = ImGui::GetItemRectMax();

    auto *button_draw = ImGui::GetWindowDrawList();
    const auto control_bg =
        mix(ImVec4{27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.94f},
            ImVec4{38.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 0.98f}, std::max(hover_t, open_t));
    button_draw->AddRectFilled(button_min, button_max, packed(control_bg), 9.0f);
    auto control_border = mix(k_border, k_border_light, hover_t);
    control_border = mix(control_border, k_accent, open_t * 0.65f);
    control_border.w = 0.08f + hover_t * 0.08f + open_t * 0.20f;
    button_draw->AddRect(button_min, button_max, packed(control_border), 9.0f, 0, 1.0f);
    const auto *selected_text = render::localization::tr(options[index]);
    const auto label_size = ImGui::CalcTextSize(selected_text);
    // Clipped short of the arrow so an over-long value cannot spill past the
    // button's own background and collide with the chevron.
    button_draw->PushClipRect(button_min, {button_max.x - 28.0f, button_max.y}, true);
    button_draw->AddText({button_min.x + 12.0f, button_min.y + (control_height - label_size.y) * 0.5f},
                         packed(k_text_main), selected_text);
    button_draw->PopClipRect();

    draw_dropdown_chevron(button_draw, {button_max.x - 15.0f, button_min.y + control_height * 0.5f},
                          packed(mix(k_text_muted, k_text_main, std::max(hover_t, open_t))), open_t);

    const auto popup_height = 8.0f + options.size() * option_height;
    auto popup_position = ImVec2{button_min.x, button_max.y + 4.0f};
    if (popup_position.y + popup_height > settings_bounds_max().y - 8.0f)
        popup_position.y = button_min.y - popup_height - 4.0f;
    // Right-aligned with the button, so a popup wider than the control grows
    // back into the card instead of off its right edge.
    popup_position.x = std::max(button_max.x - popup_width, settings_bounds_min().x + 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize({popup_width, popup_height}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    if (ImGui::BeginPopup("##options", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                           ImGuiWindowFlags_NoBackground))
    {
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max =
            ImVec2{popup_min.x + ImGui::GetWindowSize().x, popup_min.y + ImGui::GetWindowSize().y};
        auto *popup_draw = ImGui::GetWindowDrawList();

        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        soft_shadow(popup_draw, popup_min, popup_max, 10.0f, {0.0f, 6.0f}, 12.0f, {0.0f, 0.0f, 0.0f, 1.0f},
                    0.72f);
        draw_popup_surface(popup_draw, popup_min, popup_max, 10.0f);
        popup_draw->PopClipRect();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(popup_min + ImVec2{4.0f, 4.0f + option_height * i});
            const auto option_id = ImGui::GetID("##option");
            ImGui::InvisibleButton("##option", {popup_width - 8.0f, option_height});
            const auto option_hovered = ImGui::IsItemHovered();
            pointer_cursor_if_hovered();
            const auto option_min = ImGui::GetItemRectMin();
            const auto option_max = ImGui::GetItemRectMax();
            const auto selected = value == i;
            const auto option_hover_t = animate_state(g_hover_animations, option_id, option_hovered, 0.11f);
            const auto selected_t = animate_state(g_active_animations, option_id, selected, 0.14f);
            auto option_bg = mix(ImVec4{1, 1, 1, 0}, k_bg_hover, option_hover_t);
            option_bg = mix(option_bg, ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.13f}, selected_t);
            const auto option_visual_min = option_min + ImVec2{0.0f, 2.0f};
            const auto option_visual_max = option_max - ImVec2{0.0f, 2.0f};
            popup_draw->AddRectFilled(option_visual_min, option_visual_max, packed(option_bg), 7.0f);
            if (selected_t > 0.01f)
            {
                popup_draw->AddRectFilled({option_min.x + 3.0f, option_visual_min.y + 6.0f},
                                          {option_min.x + 5.0f, option_visual_max.y - 6.0f},
                                          packed(ImVec4{k_accent.x, k_accent.y, k_accent.z, selected_t}),
                                          1.0f);
            }
            popup_draw->AddText({option_min.x + 12.0f + option_hover_t * 2.0f,
                                 option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f},
                                packed(mix(k_text_muted, k_text_main, std::max(option_hover_t, selected_t))),
                                render::localization::tr(options[i]));
            draw_checkmark(popup_draw, {option_max.x - 16.0f, option_min.y + option_height * 0.5f},
                           packed(ImVec4{k_accent.x, k_accent.y, k_accent.z, selected_t}), selected_t);
            if (ImGui::IsItemClicked())
            {
                value = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    end_row();
}

void multiselect_row(const char *label, int &mask, std::span<const std::pair<const char *, int>> options,
                     int all_mask)
{
    if (options.empty())
        return;

    constexpr auto control_width = 140.0f;
    constexpr auto control_height = 30.0f;
    constexpr auto option_height = 34.0f;
    begin_row(label, control_width);

    std::string summary{};
    if ((mask & all_mask) == all_mask)
        summary = render::localization::tr("All");
    else
    {
        for (const auto &[name, bit] : options)
            if (mask & bit)
                summary += (summary.empty() ? "" : ", ") + std::string(render::localization::tr(name));
        if (summary.empty())
            summary = render::localization::tr("None");
    }

    const auto button_id = ImGui::GetID("##multiselect_button");
    ImGui::InvisibleButton("##multiselect_button", {control_width, control_height});
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##multi_options");
    pointer_cursor_if_hovered();
    const auto button_hovered = ImGui::IsItemHovered();
    const auto popup_open = ImGui::IsPopupOpen("##multi_options");
    const auto hover_t = animate_state(g_hover_animations, button_id, button_hovered, 0.14f);
    const auto open_t = animate_state(g_active_animations, button_id, popup_open, 0.16f);
    const auto button_min = ImGui::GetItemRectMin();
    const auto button_max = ImGui::GetItemRectMax();

    auto *button_draw = ImGui::GetWindowDrawList();
    const auto control_bg =
        mix(ImVec4{27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.94f},
            ImVec4{38.0f / 255.0f, 36.0f / 255.0f, 48.0f / 255.0f, 0.98f}, std::max(hover_t, open_t));
    button_draw->AddRectFilled(button_min, button_max, packed(control_bg), 9.0f);
    auto control_border = mix(k_border, k_border_light, hover_t);
    control_border = mix(control_border, k_accent, open_t * 0.65f);
    control_border.w = 0.08f + hover_t * 0.08f + open_t * 0.20f;
    button_draw->AddRect(button_min, button_max, packed(control_border), 9.0f);
    const auto label_size = ImGui::CalcTextSize(summary.c_str());
    button_draw->PushClipRect(button_min, {button_max.x - 28.0f, button_max.y}, true);
    button_draw->AddText({button_min.x + 12.0f, button_min.y + (control_height - label_size.y) * 0.5f},
                         packed(k_text_main), summary.c_str());
    button_draw->PopClipRect();

    draw_dropdown_chevron(button_draw, {button_max.x - 15.0f, button_min.y + control_height * 0.5f},
                          packed(mix(k_text_muted, k_text_main, std::max(hover_t, open_t))), open_t);

    auto popup_width = control_width;
    for (const auto &[name, bit] : options)
    {
        (void)bit;
        popup_width = std::max(popup_width, ImGui::CalcTextSize(render::localization::tr(name)).x + 58.0f);
    }
    const auto popup_height = 8.0f + options.size() * option_height;
    auto popup_position = ImVec2{button_min.x, button_max.y + 4.0f};
    if (popup_position.y + popup_height > settings_bounds_max().y - 8.0f)
        popup_position.y = button_min.y - popup_height - 4.0f;
    popup_position.x = std::max(button_max.x - popup_width, settings_bounds_min().x + 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize({popup_width, popup_height}, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    if (ImGui::BeginPopup("##multi_options", ImGuiWindowFlags_NoScrollbar |
                                                 ImGuiWindowFlags_NoScrollWithMouse |
                                                 ImGuiWindowFlags_NoBackground))
    {
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max =
            ImVec2{popup_min.x + ImGui::GetWindowSize().x, popup_min.y + ImGui::GetWindowSize().y};
        auto *popup_draw = ImGui::GetWindowDrawList();
        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        soft_shadow(popup_draw, popup_min, popup_max, 10.0f, {0.0f, 6.0f}, 12.0f, {0.0f, 0.0f, 0.0f, 1.0f},
                    0.72f);
        draw_popup_surface(popup_draw, popup_min, popup_max, 10.0f);
        popup_draw->PopClipRect();
        for (int i = 0; i < static_cast<int>(options.size()); ++i)
        {
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(popup_min + ImVec2{4.0f, 4.0f + option_height * i});
            const auto option_id = ImGui::GetID("##multi_option");
            ImGui::InvisibleButton("##multi_option", {popup_width - 8.0f, option_height});
            const auto option_hovered = ImGui::IsItemHovered();
            pointer_cursor_if_hovered();
            const auto option_min = ImGui::GetItemRectMin();
            const auto option_max = ImGui::GetItemRectMax();
            const auto bit = options[i].second;
            const auto selected = (mask & bit) != 0;
            const auto option_hover_t = animate_state(g_hover_animations, option_id, option_hovered, 0.11f);
            const auto selected_t = animate_state(g_active_animations, option_id, selected, 0.14f);
            auto option_bg = mix(ImVec4{1, 1, 1, 0}, k_bg_hover, option_hover_t);
            option_bg = mix(option_bg, ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.11f}, selected_t);
            const auto option_visual_min = option_min + ImVec2{0.0f, 2.0f};
            const auto option_visual_max = option_max - ImVec2{0.0f, 2.0f};
            popup_draw->AddRectFilled(option_visual_min, option_visual_max, packed(option_bg), 7.0f);

            const auto check_min = ImVec2{option_min.x + 10.0f, option_min.y + 9.0f};
            const auto check_max = check_min + ImVec2{16.0f, 16.0f};
            const auto check_bg = mix(ImVec4{1.0f, 1.0f, 1.0f, 0.035f},
                                      ImVec4{k_accent.x, k_accent.y, k_accent.z, 1.0f}, selected_t);
            popup_draw->AddRectFilled(check_min, check_max, packed(check_bg), 5.0f);
            popup_draw->AddRect(check_min, check_max, packed(mix(k_border_light, k_accent, selected_t)),
                                5.0f);
            draw_checkmark(popup_draw, (check_min + check_max) * 0.5f,
                           IM_COL32(255, 255, 255, static_cast<int>(selected_t * 255.0f)), selected_t, 1.8f);

            popup_draw->AddText({option_min.x + 36.0f + option_hover_t * 2.0f,
                                 option_min.y + (option_height - ImGui::GetTextLineHeight()) * 0.5f},
                                packed(mix(k_text_muted, k_text_main, std::max(option_hover_t, selected_t))),
                                render::localization::tr(options[i].first));
            if (ImGui::IsItemClicked())
                mask ^= bit; // toggle, keep popup open
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();
    end_row();
}

void aim_parts_row(int &mask)
{
    static constexpr std::pair<const char *, int> opts[]{
        {"Head", config::combat_profile::aim_part::head},
        {"Body", config::combat_profile::aim_part::body},
        {"Arms", config::combat_profile::aim_part::arms},
        {"Legs", config::combat_profile::aim_part::legs},
    };
    multiselect_row("Hitboxes", mask, opts, config::combat_profile::aim_part::all);
}

void draw_action_icon(ImDrawList *draw, row_action_icon icon, ImVec2 center, ImU32 color)
{
    switch (icon)
    {
    case row_action_icon::copy:
        draw->AddRect(center + ImVec2{-5.0f, -6.0f}, center + ImVec2{4.0f, 4.0f}, color, 2.0f, 0, 1.35f);
        draw->AddRect(center + ImVec2{-2.0f, -3.0f}, center + ImVec2{7.0f, 7.0f}, color, 2.0f, 0, 1.35f);
        break;
    case row_action_icon::check:
        draw_checkmark(draw, center, color, 1.0f, 1.8f);
        break;
    case row_action_icon::save:
        draw->AddLine(center + ImVec2{0.0f, -6.0f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
        draw->AddLine(center + ImVec2{-3.5f, -0.5f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
        draw->AddLine(center + ImVec2{3.5f, -0.5f}, center + ImVec2{0.0f, 3.0f}, color, 1.5f);
        draw->AddLine(center + ImVec2{-5.0f, 6.0f}, center + ImVec2{5.0f, 6.0f}, color, 1.5f);
        break;
    case row_action_icon::folder:
        draw->AddRect(center + ImVec2{-7.0f, -3.5f}, center + ImVec2{7.0f, 6.0f}, color, 2.0f, 0, 1.45f);
        draw->AddLine(center + ImVec2{-6.0f, -3.5f}, center + ImVec2{-3.0f, -6.0f}, color, 1.45f);
        draw->AddLine(center + ImVec2{-3.0f, -6.0f}, center + ImVec2{1.0f, -6.0f}, color, 1.45f);
        draw->AddLine(center + ImVec2{1.0f, -6.0f}, center + ImVec2{3.0f, -3.5f}, color, 1.45f);
        break;
    default:
        break;
    }
}

bool button_row(const char *label, const char *text, row_action_icon icon)
{
    constexpr auto control_width = 140.0f;
    constexpr auto control_height = 30.0f;
    begin_row(label, control_width);

    const auto id = ImGui::GetID("##button_row");
    ImGui::InvisibleButton("##button_row", {control_width, control_height});
    const auto clicked = ImGui::IsItemClicked();
    pointer_cursor_if_hovered();
    const auto hovered = ImGui::IsItemHovered();
    const auto held = ImGui::IsItemActive();
    const auto hover_t = animate_state(g_hover_animations, id, hovered, 0.13f);
    const auto held_t = animate_state(g_active_animations, id, held, 0.08f);
    const auto bmin = ImGui::GetItemRectMin();
    const auto bmax = ImGui::GetItemRectMax();

    auto *dl = ImGui::GetWindowDrawList();
    constexpr auto rounding = 9.0f;
    if (hover_t > 0.01f)
    {
        soft_shadow(dl, bmin, bmax, rounding, {0.0f, 3.0f}, 7.0f, {k_accent.x, k_accent.y, k_accent.z, 1.0f},
                    hover_t * 0.24f);
    }
    auto bg = mix(ImVec4{27.0f / 255.0f, 27.0f / 255.0f, 35.0f / 255.0f, 0.88f},
                  ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.78f}, hover_t * 0.72f);
    bg = mix(bg, ImVec4{k_accent.x * 0.72f, k_accent.y * 0.72f, k_accent.z * 0.72f, 0.92f}, held_t);
    dl->AddRectFilled(bmin, bmax, packed(bg), rounding);
    auto border = mix(k_border, ImVec4{k_accent.x, k_accent.y, k_accent.z, 0.55f}, hover_t);
    border.w += held_t * 0.18f;
    dl->AddRect(bmin, bmax, packed(border), rounding, 0, 1.0f);

    const auto *button_text = render::localization::tr(text);
    const auto ts = ImGui::CalcTextSize(button_text);
    const auto has_icon = icon != row_action_icon::none;
    const auto content_width = ts.x + (has_icon ? 21.0f : 0.0f);
    const auto content_x = bmin.x + (control_width - content_width) * 0.5f;
    const auto press_offset = ImVec2{0.0f, held_t * 1.0f};
    const auto content_color = packed(mix(k_text_muted, k_text_main, std::max(hover_t, held_t)));
    if (has_icon)
    {
        const auto icon_center = ImVec2{content_x + 7.0f, bmin.y + control_height * 0.5f} + press_offset;
        draw_action_icon(dl, icon, icon_center, content_color);
    }
    const auto text_position =
        ImVec2{content_x + (has_icon ? 21.0f : 0.0f), bmin.y + (control_height - ts.y) * 0.5f} + press_offset;
    dl->AddText(text_position, content_color, button_text);

    end_row();
    return clicked;
}

int filter_filename_char(ImGuiInputTextCallbackData *data)
{
    const auto c = data->EventChar;
    if (c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|')
        return 1; // discard
    return 0;
}

void text_input_row(const char *label, char *buffer, std::size_t size)
{
    constexpr auto control_width = 190.0f;
    begin_row(label, control_width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, k_bg_panel);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_bg_hover);
    ImGui::PushStyleColor(ImGuiCol_Border, k_border_light);
    ImGui::PushStyleColor(ImGuiCol_Text, k_text_main);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10.0f, 6.0f});
    ImGui::SetNextItemWidth(control_width);
    ImGui::InputText("##text_input", buffer, size, ImGuiInputTextFlags_CallbackCharFilter,
                     filter_filename_char);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    end_row();
}

void keybind_row(const char *label, int &value)
{
    begin_row(label, 130.0f);
    const auto listening = g_listening_key == &value;
    const auto pulse =
        listening ? 0.8f + std::sin(static_cast<float>(ImGui::GetTime()) * 6.28318f) * 0.2f : 1.0f;
    ImGui::PushStyleColor(ImGuiCol_Button,
                          listening ? ImVec4{k_accent.x, k_accent.y, k_accent.z, pulse} : k_bg_panel);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, listening ? k_accent : k_bg_hover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    const auto text = listening ? std::string("...") : std::format("[ {} ]", key_name(value));

    if (ImGui::Button(text.c_str(), {130.0f, 28.0f}) && !listening)
    {
        g_listening_key = &value;
        g_listening_start_frame = ImGui::GetFrameCount();
        g_listening_armed = false;
    }
    pointer_cursor_if_hovered();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (g_listening_key == &value && ImGui::GetFrameCount() > g_listening_start_frame)
    {
        if (!g_listening_armed)
        {

            if (bind_input_is_down())
            {
                end_row();
                return;
            }
            g_listening_armed = true;
            end_row();
            return;
        }

        if (const int key = pressed_bind_key())
        {
            value = key == VK_ESCAPE ? 0 : key;
            g_listening_key = nullptr;
            g_listening_armed = false;
        }
    }
    end_row();
}

void color_row(const char *label, zdraw::rgba &color)
{
    begin_row(label, 36.0f);
    const auto picker_id = ImGui::GetID("##picker");
    ImGui::InvisibleButton("##preview", {36.0f, 22.0f});
    pointer_cursor_if_hovered();
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##picker");

    const auto hovered = ImGui::IsItemHovered();
    const auto item_min = ImGui::GetItemRectMin();
    const auto item_max = ImGui::GetItemRectMax();
    const auto expand = hovered ? 1.0f : 0.0f;
    const auto preview_min = ImVec2{item_min.x - expand, item_min.y - expand * 0.5f};
    const auto preview_max = ImVec2{item_max.x + expand, item_max.y + expand * 0.5f};
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(preview_min, preview_max, IM_COL32(142, 142, 148, 255), 8.0f);
    const auto checker_min = preview_min + ImVec2{4.0f, 4.0f};
    const auto checker_max = preview_max - ImVec2{4.0f, 4.0f};
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            const auto a = ImVec2{checker_min.x + x * 7.0f, checker_min.y + y * 7.0f};
            const auto b = ImVec2{std::min(a.x + 7.0f, checker_max.x), std::min(a.y + 7.0f, checker_max.y)};
            if (a.x >= checker_max.x || a.y >= checker_max.y)
                continue;
            draw->AddRectFilled(a, b,
                                (x + y) % 2 ? IM_COL32(120, 120, 120, 255) : IM_COL32(185, 185, 185, 255));
        }
    }
    draw->AddRectFilled(preview_min, preview_max, packed(to_imvec(color)), 8.0f);
    draw->AddRect(preview_min, preview_max, hovered ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 51), 8.0f);

    color_picker_popup(color, item_min, item_max, picker_id);
    end_row();
}

void humanizer_preview(int amount, int smoothing, const config::combat_profile::humanizer_settings &settings)
{
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    constexpr auto height = 76.0f;
    ImGui::InvisibleButton("##humanizer_preview", {width, height});
    auto *draw = ImGui::GetWindowDrawList();
    const auto left = origin + ImVec2{5.0f, height * 0.68f};
    const auto right = origin + ImVec2{width - 8.0f, height * 0.38f};
    const auto h = std::clamp(amount / 100.0f, 0.0f, 1.0f);
    const auto step_speed = std::clamp(settings.max_step / 15.0f, 0.25f, 4.0f);
    const auto duration = std::clamp((0.34f + smoothing * 0.032f) / std::sqrt(step_speed), 0.28f, 2.5f);
    const auto reaction = (settings.reaction_min_ms + settings.reaction_max_ms) * 0.0005f;
    const auto cycle = reaction + duration + 0.45f;
    const auto elapsed = static_cast<float>(ImGui::GetTime());
    const auto local = std::fmod(elapsed, cycle);
    const auto cycle_index = static_cast<int>(elapsed / cycle);
    const auto progress = local <= reaction ? 0.0f : std::clamp((local - reaction) / duration, 0.0f, 1.0f);
    const auto gravity = std::clamp(settings.gravity / 9.0f, 0.15f, 2.25f);
    const auto eased = 1.0f - std::pow(1.0f - progress, gravity);
    const auto bend = (settings.curve - 0.5f) * 38.0f * h;
    const auto damping = std::clamp(settings.damping, 0.0f, 1.0f);
    const auto wind = settings.wind * 0.85f * h;
    const auto overshoot_cycle = (cycle_index * 37) % 100 < static_cast<int>(settings.overshoot_chance);
    const auto overshoot = overshoot_cycle ? settings.overshoot_amount * 34.0f * h : 0.0f;
    const auto path_point = [&](float t) {
        const auto settle = std::sin(t * 3.14159265f);
        const auto overshoot_shape =
            t < 0.78f ? std::sin(t / 0.78f * 1.5707963f) : 1.0f - (t - 0.78f) / 0.22f;
        const auto x = std::lerp(left.x, right.x, t) + overshoot * overshoot_shape;
        const auto arc = settle * bend;
        const auto oscillation =
            std::sin(t * 18.0f + 0.7f) * wind * std::pow(1.0f - t, 0.35f + damping * 2.5f);
        const auto micro = std::sin(t * 71.0f + 1.9f) * settings.jitter * 1.4f * h * settle;
        return ImVec2{x, std::lerp(left.y, right.y, t) - arc + oscillation + micro};
    };
    ImVec2 previous = path_point(0.0f);
    for (int segment = 1; segment <= 48; ++segment)
    {
        const auto t = segment / 48.0f;
        const auto next = path_point(t);
        draw->AddLine(previous, next, IM_COL32(255, 255, 255, 31), 1.0f);
        previous = next;
    }
    const auto deadzone = 2.5f + settings.deadzone * 3.0f;
    draw->AddCircle(right, deadzone, IM_COL32(255, 255, 255, 48), 20, 1.0f);
    draw->AddCircleFilled(right, 1.7f, IM_COL32(232, 235, 244, 180), 16);
    for (int tail = 8; tail >= 0; --tail)
    {
        const auto t = std::max(0.0f, eased - tail * (0.012f + damping * 0.01f));
        const auto alpha = static_cast<int>(28 + (8 - tail) * 18);
        draw->AddCircleFilled(path_point(t), tail == 0 ? 2.8f : 1.4f, IM_COL32(214, 226, 255, alpha), 16);
    }
}

void dock_player_bar(config::visual_profile::player::layout_element &layout, int dock)
{
    const auto scale = layout.scale;
    switch (dock)
    {
    case 0:
        layout = {-0.07f, 0.50f, scale};
        break;
    case 1:
        layout = {0.50f, -0.04f, scale};
        break;
    case 2:
        layout = {0.50f, 1.012f, scale};
        break;
    case 3:
        layout = {1.04f, 0.50f, scale};
        break;
    default:
        break;
    }
}

void player_weapon_settings_rows(config::visual_profile::player::weapon &weapon)
{
    int display = static_cast<int>(weapon.display);
    static constexpr const char *displays[]{"Text", "Icon", "Text + Icon"};
    select_row("Display", display, displays);
    weapon.display = static_cast<config::visual_profile::player::weapon::display_type>(display);
    color_row("Text Color", weapon.text_color);
    color_row("Icon Color", weapon.icon_color);
    toggle_popup_row("Ammo Indicator", weapon.ammo.enabled, 2, [&] {
        toggle_row("Exact Ammo Count", weapon.ammo.show_count);
        color_row("Empty Color", weapon.ammo.empty_color);
    });
}

void player_info_flag_settings_rows(config::visual_profile::player::info_flags &flags)
{
    using flag = config::visual_profile::player::info_flags::flag;
    static constexpr std::pair<const char *, int> options[]{
        {"Money", flag::money},   {"Armor", flag::armor},       {"Defuse Kit", flag::kit},
        {"Scoped", flag::scoped}, {"Defusing", flag::defusing}, {"Flashed", flag::flashed},
        {"Ping", flag::ping},     {"Distance", flag::distance}, {"Bomb Damage", flag::bomb_damage}};
    int mask = flags.flags;
    multiselect_row("Active Flags", mask, options, (1 << 9) - 1);
    flags.flags = static_cast<std::uint16_t>(mask);

    static int selected{};
    static constexpr const char *names[]{"Money",   "Armor", "Defuse Kit", "Scoped",     "Defusing",
                                         "Flashed", "Ping",  "Distance",   "Bomb Damage"};
    select_row("Edit Flag", selected, names);
    auto &style = selected_info_flag_style(flags, selected);
    color_row("Color", style.color);
    slider_row("Scale", style.scale, 0.55f, 2.0f, "x", 0.05f);
}

void chams_material_rows(config::visual_profile::chams::material &m)
{
    select_row("Material", m.type, k_chams_materials);
    color_row("Color", m.color);
    toggle_row("Wireframe", m.wireframe);

    // Only the parameters the selected material actually reads are shown;
    // the rest keep their values so switching type back restores the tuning.
    switch (m.type)
    {
    case config::visual_profile::chams::shaded:
        slider_row("Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f);
        slider_row("Metalness", m.metalness, 0.0f, 1.0f, "", 0.01f);
        break;

    case config::visual_profile::chams::glow:
        slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
        break;

    case config::visual_profile::chams::glow_outline:
        slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
        slider_row("Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f);
        slider_row("Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f);
        break;

    case config::visual_profile::chams::iridescent:
        slider_row("Strength", m.strength, 0.0f, 1.0f, "", 0.01f);
        slider_row("Roughness", m.roughness, 0.0f, 1.0f, "", 0.01f);
        break;

    case config::visual_profile::chams::water_flow:
        slider_row("Speed", m.speed, 0.0f, 4.0f, "", 0.05f);
        break;

    case config::visual_profile::chams::glossy:
        slider_row("Exponent", m.exponent, 0.1f, 8.0f, "", 0.1f);
        slider_row("Fresnel Filling", m.fresnel_fill, 0.0f, 1.0f, "", 0.01f);
        slider_row("Falloff", m.falloff, 0.05f, 2.0f, "", 0.05f);
        color_row("Tint", m.tint);
        break;

    default:
        break;
    }
}
} // namespace render::menu::detail
