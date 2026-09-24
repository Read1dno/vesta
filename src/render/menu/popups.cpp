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
void popup_blur_callback(const ImDrawList *, const ImDrawCmd *command)
{
    if (!command || command->UserCallbackDataSize != sizeof(popup_blur_request))
        return;
    const auto *request = static_cast<const popup_blur_request *>(command->UserCallbackData);
    auto *render_state =
        static_cast<ImGui_ImplDX11_RenderState *>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (!request || !render_state || !render_state->Device || !render_state->DeviceContext ||
        !g_popup_blur.ensure_shaders(render_state->Device))
        return;

    auto *context = render_state->DeviceContext;
    ID3D11RenderTargetView *target{};
    context->OMGetRenderTargets(1, &target, nullptr);
    if (!target)
        return;
    ID3D11Resource *resource{};
    ID3D11Texture2D *source{};
    target->GetResource(&resource);
    if (resource)
        resource->QueryInterface(IID_PPV_ARGS(&source));
    if (resource)
        resource->Release();
    if (!source)
    {
        target->Release();
        return;
    }

    D3D11_TEXTURE2D_DESC source_description{};
    source->GetDesc(&source_description);
    if (!g_popup_blur.ensure_texture(source_description))
    {
        source->Release();
        target->Release();
        return;
    }
    context->CopyResource(g_popup_blur.copy, source);

    const auto physical_width = std::max(1.0f, command->ClipRect.z - command->ClipRect.x);
    const auto physical_height = std::max(1.0f, command->ClipRect.w - command->ClipRect.y);
    const auto scale = std::min(physical_width / std::max(request->logical_width, 1.0f),
                                physical_height / std::max(request->logical_height, 1.0f));
    struct constants_t
    {
        float rect[4];
        float texture[4];
    } constants{{command->ClipRect.x, command->ClipRect.y, command->ClipRect.z, command->ClipRect.w},
                {1.0f / source_description.Width, 1.0f / source_description.Height, request->rounding * scale,
                 request->radius * scale}};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(g_popup_blur.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(g_popup_blur.constants, 0);
        const D3D11_RECT scissor{static_cast<LONG>(std::floor(command->ClipRect.x)),
                                 static_cast<LONG>(std::floor(command->ClipRect.y)),
                                 static_cast<LONG>(std::ceil(command->ClipRect.z)),
                                 static_cast<LONG>(std::ceil(command->ClipRect.w))};
        context->RSSetScissorRects(1, &scissor);
        context->IASetInputLayout(nullptr);
        context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(g_popup_blur.vertex_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &g_popup_blur.constants);
        context->PSSetShader(g_popup_blur.pixel_shader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &g_popup_blur.constants);
        context->PSSetShaderResources(0, 1, &g_popup_blur.copy_view);
        context->PSSetSamplers(0, 1, &render_state->SamplerDefault);
        const float blend_factor[4]{};
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        context->Draw(3, 0);
        ID3D11ShaderResourceView *empty{};
        context->PSSetShaderResources(0, 1, &empty);
    }
    source->Release();
    target->Release();
}

void draw_popup_surface(ImDrawList *draw, ImVec2 min, ImVec2 max, float rounding)
{
    popup_blur_request blur{max.x - min.x, max.y - min.y, rounding, 2.25f};
    draw->PushClipRect(min, max, true);
    draw->AddCallback(popup_blur_callback, &blur, sizeof(blur));
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    draw->PopClipRect();
    draw->AddRectFilled(min, max, packed(k_bg_popup), rounding);
    draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, IM_COL32(224, 226, 236, 10),
                        std::max(0.0f, rounding - 1.0f));
    add_vertical_gradient_rounded(draw, min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f},
                                  IM_COL32(255, 255, 255, 8), IM_COL32(255, 255, 255, 0),
                                  std::max(0.0f, rounding - 1.0f), ImDrawFlags_RoundCornersAll);
    draw->AddRect(min, max, IM_COL32(255, 255, 255, 24), rounding, 0, 1.0f);
}

void color_picker_popup(zdraw::rgba &color, ImVec2 item_min, ImVec2 item_max, ImGuiID picker_id)
{
    auto value = to_imvec(color);
    constexpr auto picker_size = ImVec2{312.0f, 314.0f};
    constexpr auto picker_area = 216.0f;
    auto picker_position = ImVec2{item_max.x - picker_size.x, item_max.y + 8.0f};
    if (picker_position.y + picker_size.y > settings_bounds_max().y - 8.0f)
        picker_position.y = item_min.y - picker_size.y - 8.0f;
    picker_position.x = render::menu::fit_popup(picker_position.x, settings_bounds_min().x + 8.0f,
                                                settings_bounds_max().x - picker_size.x - 8.0f);
    picker_position.y = render::menu::fit_popup(picker_position.y, settings_bounds_min().y + 8.0f,
                                                settings_bounds_max().y - picker_size.y - 8.0f);
    ImGui::SetNextWindowPos(picker_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(picker_size, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoBackground))
    {
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max = popup_min + picker_size;
        auto *popup_draw = ImGui::GetWindowDrawList();

        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        soft_shadow(popup_draw, popup_min, popup_max, 14.0f, {0.0f, 6.0f}, 14.0f, {0, 0, 0, 1}, 0.55f);
        draw_popup_surface(popup_draw, popup_min, popup_max, 14.0f);
        popup_draw->PopClipRect();

        float hue{}, saturation{}, brightness{};
        ImGui::ColorConvertRGBtoHSV(value.x, value.y, value.z, hue, saturation, brightness);
        auto &stored_hue = g_color_hues.try_emplace(picker_id, 0.735f).first->second;
        if (saturation > 0.001f)
            stored_hue = hue;
        else
            hue = stored_hue;
        const auto sv_min = popup_min + ImVec2{16.0f, 16.0f};
        const auto sv_max = sv_min + ImVec2{picker_area, picker_area};
        float hue_r{}, hue_g{}, hue_b{};
        ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, hue_r, hue_g, hue_b);
        const auto hue_color = IM_COL32(static_cast<int>(hue_r * 255.0f), static_cast<int>(hue_g * 255.0f),
                                        static_cast<int>(hue_b * 255.0f), 255);
        add_linear_gradient_rounded(popup_draw, sv_min, sv_max, IM_COL32_WHITE, hue_color, 8.0f,
                                    ImDrawFlags_RoundCornersAll, true);
        add_linear_gradient_rounded(popup_draw, sv_min, sv_max, IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, 8.0f,
                                    ImDrawFlags_RoundCornersAll, false);
        popup_draw->AddRect(sv_min, sv_max, packed(k_border_light), 8.0f, 0, 1.5f);

        ImGui::SetCursorPos(sv_min - popup_min);
        ImGui::InvisibleButton("##sv", sv_max - sv_min);
        pointer_cursor_if_hovered();
        if (ImGui::IsItemActive())
        {
            const auto mouse = ImGui::GetIO().MousePos;
            saturation = std::clamp((mouse.x - sv_min.x) / picker_area, 0.0f, 1.0f);
            brightness = 1.0f - std::clamp((mouse.y - sv_min.y) / picker_area, 0.0f, 1.0f);
            ImGui::ColorConvertHSVtoRGB(hue, saturation, brightness, value.x, value.y, value.z);
            from_imvec(value, color);
        }
        const auto sv_thumb =
            ImVec2{sv_min.x + saturation * picker_area, sv_min.y + (1.0f - brightness) * picker_area};
        popup_draw->AddCircle(sv_thumb, 7.0f, IM_COL32(0, 0, 0, 160), 24, 3.0f);
        popup_draw->AddCircle(sv_thumb, 6.0f, IM_COL32_WHITE, 24, 2.0f);

        const auto hue_min = popup_min + ImVec2{244.0f, 16.0f};
        const auto hue_max = hue_min + ImVec2{16.0f, picker_area};
        for (int slice = 0; slice < 6; ++slice)
        {
            float r0{}, g0{}, b0{}, r1{}, g1{}, b1{};
            ImGui::ColorConvertHSVtoRGB(static_cast<float>(slice) / 6.0f, 1, 1, r0, g0, b0);
            ImGui::ColorConvertHSVtoRGB(static_cast<float>(slice + 1) / 6.0f, 1, 1, r1, g1, b1);
            const auto y0 = hue_min.y + picker_area * slice / 6.0f;
            const auto y1 = hue_min.y + picker_area * (slice + 1) / 6.0f + 0.5f;
            const auto corners = slice == 0   ? ImDrawFlags_RoundCornersTop
                                 : slice == 5 ? ImDrawFlags_RoundCornersBottom
                                              : ImDrawFlags_RoundCornersNone;
            add_linear_gradient_rounded(
                popup_draw, {hue_min.x, y0}, {hue_max.x, y1}, ImGui::ColorConvertFloat4ToU32({r0, g0, b0, 1}),
                ImGui::ColorConvertFloat4ToU32({r1, g1, b1, 1}), 7.0f, corners, false);
        }
        popup_draw->AddRect(hue_min, hue_max, packed(k_border_light), 7.0f);
        ImGui::SetCursorPos(hue_min - popup_min);
        ImGui::InvisibleButton("##hue", hue_max - hue_min);
        pointer_cursor_if_hovered();
        if (ImGui::IsItemActive())
        {
            hue = std::clamp((ImGui::GetIO().MousePos.y - hue_min.y) / picker_area, 0.0f, 1.0f);
            stored_hue = hue;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, brightness, value.x, value.y, value.z);
            from_imvec(value, color);
        }
        const auto hue_y = hue_min.y + hue * picker_area;
        popup_draw->AddRectFilled({hue_min.x - 3.0f, hue_y - 3.0f}, {hue_max.x + 3.0f, hue_y + 3.0f},
                                  IM_COL32_WHITE, 3.0f);
        popup_draw->AddRect({hue_min.x - 3.0f, hue_y - 3.0f}, {hue_max.x + 3.0f, hue_y + 3.0f},
                            IM_COL32(0, 0, 0, 150), 3.0f);

        const auto alpha_min = popup_min + ImVec2{272.0f, 16.0f};
        const auto alpha_max = alpha_min + ImVec2{16.0f, picker_area};
        popup_draw->AddRectFilled(alpha_min, alpha_max, IM_COL32(112, 112, 120, 255), 7.0f);
        for (int y = 1; y < 13; ++y)
            for (int x = 0; x < 2; ++x)
                popup_draw->AddRectFilled(
                    alpha_min + ImVec2{x * 8.0f, y * 16.0f},
                    alpha_min + ImVec2{(x + 1) * 8.0f, std::min((y + 1) * 16.0f, picker_area)},
                    (x + y) % 2 ? IM_COL32(100, 100, 106, 255) : IM_COL32(174, 174, 180, 255));
        add_linear_gradient_rounded(popup_draw, alpha_min, alpha_max,
                                    ImGui::ColorConvertFloat4ToU32({value.x, value.y, value.z, 1}),
                                    ImGui::ColorConvertFloat4ToU32({value.x, value.y, value.z, 0}), 7.0f,
                                    ImDrawFlags_RoundCornersAll, false);
        popup_draw->AddRect(alpha_min, alpha_max, packed(k_border_light), 7.0f);
        ImGui::SetCursorPos(alpha_min - popup_min);
        ImGui::InvisibleButton("##alpha", alpha_max - alpha_min);
        pointer_cursor_if_hovered();
        if (ImGui::IsItemActive())
        {
            value.w = 1.0f - std::clamp((ImGui::GetIO().MousePos.y - alpha_min.y) / picker_area, 0.0f, 1.0f);
            from_imvec(value, color);
        }
        const auto alpha_y = alpha_min.y + (1.0f - value.w) * picker_area;
        popup_draw->AddRectFilled({alpha_min.x - 3.0f, alpha_y - 3.0f}, {alpha_max.x + 3.0f, alpha_y + 3.0f},
                                  IM_COL32_WHITE, 3.0f);
        popup_draw->AddRect({alpha_min.x - 3.0f, alpha_y - 3.0f}, {alpha_max.x + 3.0f, alpha_y + 3.0f},
                            IM_COL32(0, 0, 0, 150), 3.0f);

        auto &hex = g_color_hex[picker_id];
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
            std::snprintf(hex.data(), hex.size(), "#%02X%02X%02X", color.r, color.g, color.b);
        ImGui::SetCursorPos({16.0f, 250.0f});
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{1, 1, 1, 0.045f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{1, 1, 1, 0.075f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{1, 1, 1, 0.09f});
        ImGui::PushStyleColor(ImGuiCol_Border, k_border_light);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::SetNextItemWidth(88.0f);
        if (ImGui::InputText("##hex", hex.data(), hex.size(),
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            unsigned int r{}, g{}, b{};
            const auto *text = hex[0] == '#' ? hex.data() + 1 : hex.data();
            if (std::sscanf(text, "%02x%02x%02x", &r, &g, &b) == 3)
            {
                color.r = static_cast<std::uint8_t>(r);
                color.g = static_cast<std::uint8_t>(g);
                color.b = static_cast<std::uint8_t>(b);
            }
        }
        ImGui::SameLine(0.0f, 8.0f);
        int channels[4]{color.r, color.g, color.b, color.a};
        for (int channel = 0; channel < 4; ++channel)
        {
            ImGui::PushID(channel);
            ImGui::SetNextItemWidth(42.0f);
            if (ImGui::InputInt("##channel", &channels[channel], 0, 0))
            {
                channels[channel] = std::clamp(channels[channel], 0, 255);
                color.r = static_cast<std::uint8_t>(channels[0]);
                color.g = static_cast<std::uint8_t>(channels[1]);
                color.b = static_cast<std::uint8_t>(channels[2]);
                color.a = static_cast<std::uint8_t>(channels[3]);
            }
            ImGui::PopID();
            if (channel != 3)
                ImGui::SameLine(0.0f, 4.0f);
        }
        popup_draw->AddText(popup_min + ImVec2{18.0f, 286.0f}, packed(k_text_muted), "HEX");
        popup_draw->AddText(popup_min + ImVec2{130.0f, 286.0f}, packed(k_text_muted), "R");
        popup_draw->AddText(popup_min + ImVec2{176.0f, 286.0f}, packed(k_text_muted), "G");
        popup_draw->AddText(popup_min + ImVec2{222.0f, 286.0f}, packed(k_text_muted), "B");
        popup_draw->AddText(popup_min + ImVec2{268.0f, 286.0f}, packed(k_text_muted), "A");
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void settings_popup(ImVec2 anchor_min, ImVec2 anchor_max, int rows, callback_ref callback)
{
    auto *parent = ImGui::GetCurrentWindow();
    const auto parent_min = ImGui::GetWindowPos();
    const auto parent_size = ImGui::GetWindowSize();
    const auto parent_max = parent_min + parent_size;
    const auto popup_size =
        ImVec2{std::clamp(parent_size.x - 24.0f, 288.0f, 320.0f), 24.0f + rows * k_row_height};
    const auto bounds_min = settings_bounds_min();
    const auto bounds_max = settings_bounds_max();
    const auto nested = parent && (parent->Flags & ImGuiWindowFlags_Popup) != 0;

    ImVec2 popup_position{};
    if (nested)
    {

        const auto right = parent_max.x + 8.0f;
        const auto left = parent_min.x - popup_size.x - 8.0f;
        popup_position.x = right + popup_size.x <= bounds_max.x - 8.0f ? right : left;
        popup_position.y = anchor_min.y - 12.0f;
    }
    else
    {
        popup_position = {anchor_max.x - popup_size.x, anchor_max.y + 8.0f};
        if (popup_position.y + popup_size.y > bounds_max.y - 8.0f)
            popup_position.y = anchor_min.y - popup_size.y - 8.0f;
    }

    popup_position.x =
        render::menu::fit_popup(popup_position.x, bounds_min.x + 8.0f, bounds_max.x - popup_size.x - 8.0f);
    popup_position.y =
        render::menu::fit_popup(popup_position.y, bounds_min.y + 8.0f, bounds_max.y - popup_size.y - 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(popup_size, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopup("##settings", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                            ImGuiWindowFlags_NoBackground))
    {
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max = popup_min + popup_size;
        auto *popup_draw = ImGui::GetWindowDrawList();
        popup_draw->PushClipRect(bounds_min, bounds_max, false);
        soft_shadow(popup_draw, popup_min, popup_max, 12.0f, {0.0f, 7.0f}, 15.0f, {0, 0, 0, 1}, 0.78f);
        draw_popup_surface(popup_draw, popup_min, popup_max, 12.0f);
        popup_draw->PopClipRect();

        ImGui::SetCursorPos({16.0f, 12.0f});
        callback();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void settings_popup_row(const char *label, int rows, callback_ref callback)
{
    constexpr auto options_width = 28.0f;
    begin_row(label, options_width);
    ImGui::InvisibleButton("##settings_options", {options_width, 24.0f});
    const auto options_min = ImGui::GetItemRectMin();
    const auto options_max = ImGui::GetItemRectMax();
    pointer_cursor_if_hovered();
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##settings");

    auto *draw = ImGui::GetWindowDrawList();
    const auto dot_y = (options_min.y + options_max.y) * 0.5f;
    for (int dot = 0; dot < 3; ++dot)
        draw->AddCircleFilled({options_min.x + 5.0f + dot * 9.0f, dot_y}, 1.08f, IM_COL32_WHITE, 12);

    settings_popup(options_min, options_max, rows, callback);
    end_row();
}

void toggle_popup_row(const char *label, bool &value, int rows, callback_ref callback)
{
    constexpr auto options_width = 28.0f;
    constexpr auto spacing = 8.0f;
    constexpr auto switch_width = 46.0f;
    begin_row(label, options_width + spacing + switch_width);

    ImGui::InvisibleButton("##settings_options", {options_width, 24.0f});
    const auto options_min = ImGui::GetItemRectMin();
    const auto options_max = ImGui::GetItemRectMax();
    pointer_cursor_if_hovered();
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##settings");

    auto *draw = ImGui::GetWindowDrawList();
    const auto dot_y = (options_min.y + options_max.y) * 0.5f;
    for (int dot = 0; dot < 3; ++dot)
    {
        draw->AddCircleFilled({options_min.x + 5.0f + dot * 9.0f, dot_y}, 1.08f, IM_COL32_WHITE, 12);
    }

    ImGui::SetCursorScreenPos({options_max.x + spacing, options_min.y});
    toggle_control(value);

    settings_popup(options_min, options_max, rows, callback);
    end_row();
}

void visual_editor_settings_popup(int rows, callback_ref callback)
{
    const auto element_min = ImGui::GetItemRectMin();
    const auto element_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##element_settings");

    const auto popup_size = ImVec2{320.0f, 24.0f + rows * k_row_height};
    const auto display = ImGui::GetIO().DisplaySize;
    const settings_bounds_scope popup_bounds{{0.0f, 0.0f}, display};
    const auto cursor = ImGui::GetMousePos();
    auto popup_position = ImVec2{std::max(cursor.x + 10.0f, element_max.x + 10.0f), cursor.y + 8.0f};
    if (popup_position.x + popup_size.x > display.x - 8.0f)
        popup_position.x = std::min(cursor.x - popup_size.x - 10.0f, element_min.x - popup_size.x - 10.0f);
    popup_position.x = render::menu::fit_popup(popup_position.x, 8.0f, display.x - popup_size.x - 8.0f);
    popup_position.y = render::menu::fit_popup(popup_position.y, 8.0f, display.y - popup_size.y - 8.0f);
    ImGui::SetNextWindowPos(popup_position, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(popup_size, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    if (ImGui::BeginPopup("##element_settings", ImGuiWindowFlags_NoScrollbar |
                                                    ImGuiWindowFlags_NoScrollWithMouse |
                                                    ImGuiWindowFlags_NoBackground))
    {
        const auto popup_min = ImGui::GetWindowPos();
        const auto popup_max = popup_min + popup_size;
        auto *popup_draw = ImGui::GetWindowDrawList();
        popup_draw->PushClipRect(settings_bounds_min(), settings_bounds_max(), false);
        soft_shadow(popup_draw, popup_min, popup_max, 12.0f, {0.0f, 7.0f}, 15.0f, {0, 0, 0, 1}, 0.78f);
        draw_popup_surface(popup_draw, popup_min, popup_max, 12.0f);
        popup_draw->PopClipRect();
        ImGui::SetCursorPos({16.0f, 12.0f});
        callback();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}
} // namespace render::menu::detail
