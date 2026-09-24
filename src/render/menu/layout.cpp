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
[[nodiscard]] ImVec4 menu_color(const zdraw::rgba color)
{
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

void synchronize_menu_palette()
{
    const auto &palette = config::general_settings.palette;
    k_bg_base = menu_color(palette.background);
    k_bg_panel = menu_color(palette.panel);
    k_bg_card = menu_color(palette.card);
    k_bg_popup = menu_color(palette.popup);
    k_bg_hover = menu_color(palette.hover);
    k_accent = menu_color(palette.accent);
    k_text_main = menu_color(palette.text);
    k_text_muted = menu_color(palette.muted_text);
    k_border = menu_color(palette.border);
    k_border_light = k_border;
    k_border_light.w = std::min(1.0f, k_border.w * 2.0f);
}

[[nodiscard]] float current_menu_scale(const float display_width, const float display_height)
{
    const auto requested = std::clamp(config::general_settings.menu_scale, 0.50f, 1.50f);
    const auto fit = std::min(display_width / k_menu_width, display_height / k_menu_height);
    return std::min(requested, std::max(0.35f, fit));
}

[[nodiscard]] ImVec2 menu_transform_origin(const float display_width, const float display_height)
{
    return {display_width * 0.5f, display_height * 0.5f};
}

[[nodiscard]] ImVec2 initial_menu_layout_position(const float display_width, const float display_height)
{
    return {(display_width - k_menu_width) * 0.5f, (display_height - k_menu_height) * 0.5f};
}

[[nodiscard]] bool belongs_to_menu(ImGuiWindow *window)
{
    for (auto *current = window; current; current = current->ParentWindow)
    {
        const auto name = std::string_view{current->Name ? current->Name : ""};
        if (name == "##vesta_native_menu" || name == "##esp_visual_editor")
            return true;
    }
    return false;
}

void scale_menu_draw_lists(const ImVec2 origin, const float scale)
{
    if (std::abs(scale - 1.0f) < 0.0001f || !GImGui)
        return;

    for (auto *window : GImGui->Windows)
    {
        if (!window || window->LastFrameActive != GImGui->FrameCount || !belongs_to_menu(window) ||
            !window->DrawList)
        {
            continue;
        }

        for (auto &vertex : window->DrawList->VtxBuffer)
        {
            vertex.pos.x = origin.x + (vertex.pos.x - origin.x) * scale;
            vertex.pos.y = origin.y + (vertex.pos.y - origin.y) * scale;
        }
        for (auto &command : window->DrawList->CmdBuffer)
        {
            command.ClipRect.x = origin.x + (command.ClipRect.x - origin.x) * scale;
            command.ClipRect.y = origin.y + (command.ClipRect.y - origin.y) * scale;
            command.ClipRect.z = origin.x + (command.ClipRect.z - origin.x) * scale;
            command.ClipRect.w = origin.y + (command.ClipRect.w - origin.y) * scale;
        }
    }
}

std::array<ImVec2, 24> make_preview_bones()
{
    std::array<ImVec2, 24> result{};
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
}

[[nodiscard]] ImVec2 settings_bounds_min()
{
    return g_settings_bounds_override ? g_settings_bounds_min : g_menu_min;
}

[[nodiscard]] ImVec2 settings_bounds_max()
{
    return g_settings_bounds_override ? g_settings_bounds_max : g_menu_max;
}

ImU32 packed(const ImVec4 &color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 to_imvec(const zdraw::rgba &color)
{
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

float approach(float current, float target, float duration)
{
    const auto step = duration > 0.0f ? ImGui::GetIO().DeltaTime / duration : 1.0f;
    return current + (target - current) * std::clamp(step, 0.0f, 1.0f);
}

ImVec4 mix(const ImVec4 &from, const ImVec4 &to, float amount)
{
    return {from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount};
}

float animate_state(std::unordered_map<ImGuiID, float> &animations, ImGuiID id, bool enabled, float duration)
{
    auto &value = animations[id];
    value = approach(value, enabled ? 1.0f : 0.0f, duration);
    return value;
}

float animate_selection(ImGuiID id, bool active)
{
    auto &value = g_active_animations[id];
    if (!active)
    {
        value = 0.0f;
        return value;
    }

    value = approach(value, 1.0f, 0.10f);
    if (value > 0.995f)
        value = 1.0f;
    return value;
}

void spring_to(float &position, float &velocity, float target)
{
    const auto delta_time = std::min(ImGui::GetIO().DeltaTime, 1.0f / 30.0f);
    constexpr auto stiffness = 430.0f;
    constexpr auto damping = 24.0f;
    velocity += (target - position) * stiffness * delta_time;
    velocity *= std::exp(-damping * delta_time);
    position += velocity * delta_time;

    if (std::abs(target - position) < 0.0005f && std::abs(velocity) < 0.005f)
    {
        position = target;
        velocity = 0.0f;
    }
}

void pointer_cursor_if_hovered()
{
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

void soft_shadow(ImDrawList *draw, ImVec2 min, ImVec2 max, float rounding, ImVec2 offset, float spread,
                 ImVec4 tint, float strength)
{
    constexpr int layers = 18;
    for (int layer = layers; layer >= 1; --layer)
    {
        const auto outer = static_cast<float>(layer) / layers;
        const auto expansion = spread * outer;
        const auto falloff = 1.0f - outer;
        auto color = tint;
        color.w = strength * (0.012f + falloff * falloff * 0.075f);
        draw->AddRectFilled({min.x + offset.x - expansion, min.y + offset.y - expansion},
                            {max.x + offset.x + expansion, max.y + offset.y + expansion}, packed(color),
                            rounding + expansion);
    }
}

void add_vertical_gradient_rounded(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom,
                                   float rounding, ImDrawFlags corners)
{
    const auto top_alpha = static_cast<float>((top >> IM_COL32_A_SHIFT) & 0xff);
    if (top_alpha <= 0.0f)
    {
        return;
    }

    draw->PathRect(min, max, rounding, corners);
    const auto first = draw->VtxBuffer.Size;
    draw->PathFillConvex(top);
    const auto last = draw->VtxBuffer.Size;

    const auto bottom_alpha = static_cast<float>((bottom >> IM_COL32_A_SHIFT) & 0xff);
    const auto span = std::max(1.0f, max.y - min.y);
    const auto tr = (top >> IM_COL32_R_SHIFT) & 0xff;
    const auto tg = (top >> IM_COL32_G_SHIFT) & 0xff;
    const auto tb = (top >> IM_COL32_B_SHIFT) & 0xff;
    for (int i = first; i < last; ++i)
    {
        auto &vertex = draw->VtxBuffer[i];
        const auto t = std::clamp((vertex.pos.y - min.y) / span, 0.0f, 1.0f);
        const auto desired = top_alpha + (bottom_alpha - top_alpha) * t;
        // Existing alpha already carries the AA fringe (0 on the edge, top_alpha
        // inside); scale it toward the desired per-row alpha to keep the fringe.
        const auto existing = static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xff);
        const auto alpha = static_cast<int>(std::clamp(existing * (desired / top_alpha), 0.0f, 255.0f));
        vertex.col = IM_COL32(tr, tg, tb, alpha);
    }
}

void add_linear_gradient_rounded(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 from, ImU32 to,
                                 float rounding, ImDrawFlags corners, bool horizontal)
{
    draw->PathRect(min, max, rounding, corners);
    const auto first = draw->VtxBuffer.Size;
    draw->PathFillConvex(IM_COL32_WHITE);
    const auto last = draw->VtxBuffer.Size;
    const auto span = std::max(1.0f, horizontal ? max.x - min.x : max.y - min.y);
    const auto channel = [](ImU32 value, int shift) { return static_cast<float>((value >> shift) & 0xff); };
    for (int index = first; index < last; ++index)
    {
        auto &vertex = draw->VtxBuffer[index];
        const auto coordinate = horizontal ? vertex.pos.x - min.x : vertex.pos.y - min.y;
        const auto t = std::clamp(coordinate / span, 0.0f, 1.0f);
        const auto coverage = channel(vertex.col, IM_COL32_A_SHIFT) / 255.0f;
        const auto lerp_channel = [&](int shift) {
            return static_cast<int>(std::clamp(
                channel(from, shift) + (channel(to, shift) - channel(from, shift)) * t, 0.0f, 255.0f));
        };
        vertex.col = IM_COL32(lerp_channel(IM_COL32_R_SHIFT), lerp_channel(IM_COL32_G_SHIFT),
                              lerp_channel(IM_COL32_B_SHIFT),
                              static_cast<int>(lerp_channel(IM_COL32_A_SHIFT) * coverage));
    }
}

void push_menu_font(zdraw::font *font)
{
    if (font && font->im_font)
    {
        ImGui::PushFont(font->im_font, font->font_size);
    }
    else
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
    }
}

std::size_t utf8_decode(const char *begin, const char *end, unsigned int &out)
{
    const auto lead = static_cast<unsigned char>(*begin);
    const auto available = static_cast<std::size_t>(end - begin);

    std::size_t length{1};
    unsigned int codepoint{lead};

    if (lead >= 0xf0)
    {
        length = 4;
        codepoint = lead & 0x07u;
    }
    else if (lead >= 0xe0)
    {
        length = 3;
        codepoint = lead & 0x0fu;
    }
    else if (lead >= 0xc0)
    {
        length = 2;
        codepoint = lead & 0x1fu;
    }

    if (length > available)
    {
        out = lead;
        return 1;
    }

    for (std::size_t i = 1; i < length; ++i)
    {
        const auto continuation = static_cast<unsigned char>(begin[i]);
        if ((continuation & 0xc0) != 0x80)
        {
            out = lead;
            return 1;
        }

        codepoint = (codepoint << 6) | (continuation & 0x3fu);
    }

    out = codepoint;
    return length;
}

void draw_section_title(ImDrawList *draw, ImVec2 position, float line_end_x, std::string_view title,
                        bool uppercase)
{
    const auto *title_wrapper = app::context().overlay.fonts().menu_semibold_13;
    auto *title_font = title_wrapper && title_wrapper->im_font ? title_wrapper->im_font : ImGui::GetFont();
    const auto title_size =
        title_wrapper && title_wrapper->im_font ? title_wrapper->font_size : ImGui::GetFontSize();

    auto title_x = position.x;
    for (std::size_t i = 0; i < title.size();)
    {
        unsigned int codepoint{};
        const auto *begin = title.data() + i;
        i += utf8_decode(begin, title.data() + title.size(), codepoint);
        const auto *end = title.data() + i;

        if (uppercase)
        {
            if (codepoint >= 'a' && codepoint <= 'z')
                codepoint -= 0x20;
            else if (codepoint >= 0x430 && codepoint <= 0x44f)
                codepoint -= 0x20; // а-я
            else if (codepoint == 0x451)
                codepoint = 0x401; // ё
        }

        title_font->RenderChar(draw, title_size, {title_x, position.y}, IM_COL32(255, 255, 255, 217),
                               static_cast<ImWchar>(codepoint));
        title_x += title_font->CalcTextSizeA(title_size, FLT_MAX, 0.0f, begin, end).x + 1.5f;
    }

    const auto line_start = ImVec2{title_x + 10.5f, position.y + title_size * 0.5f};
    if (line_start.x < line_end_x)
    {
        draw->AddRectFilledMultiColor(line_start, {line_end_x, line_start.y + 1.0f}, packed(k_border_light),
                                      IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0),
                                      packed(k_border_light));
    }
}

void begin_row(const char *label, float control_width)
{
    ImGui::PushID(label);
    const auto y = ImGui::GetCursorPosY();
    g_row_start_y_stack.push_back(y);
    ImGui::SetCursorPosY(y + 13.0f);
    const auto *translated = render::localization::tr(label);
    const auto text_min = ImGui::GetCursorScreenPos();
    const auto label_max_x =
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - control_width - 8.0f;
    if (label_max_x > text_min.x)
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), text_min,
                                  {label_max_x, text_min.y + ImGui::GetTextLineHeight()}, label_max_x,
                                  translated, nullptr, nullptr);
    ImGui::SetCursorPos({ImGui::GetWindowContentRegionMax().x - control_width, y + 9.0f});
}

void clipped_row_text(const std::string_view text, const ImVec4 color)
{
    const auto minimum = ImGui::GetCursorScreenPos();
    const auto maximum_x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const auto available = std::max(1.0f, maximum_x - minimum.x);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), minimum,
                              {maximum_x, minimum.y + ImGui::GetTextLineHeight()}, maximum_x, text.data(),
                              text.data() + text.size(), nullptr);
    ImGui::PopStyleColor();
    ImGui::InvisibleButton("##clipped_row_text", {available, ImGui::GetTextLineHeight()});
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(text.data(), text.data() + text.size()).x > available)
        ImGui::SetTooltip("%.*s", static_cast<int>(text.size()), text.data());
}

void end_row()
{
    if (g_row_start_y_stack.empty())
        return;
    const auto row_start_y = g_row_start_y_stack.back();
    g_row_start_y_stack.pop_back();
    ImGui::SetCursorPosY(row_start_y);
    const auto registered_height = std::max(0.0f, k_row_height - ImGui::GetStyle().ItemSpacing.y);
    ImGui::Dummy({0.0f, registered_height});
    ImGui::PopID();
}

std::string key_name(int key)
{
    switch (key)
    {
    case 0:
        return render::localization::tr("None");
    case VK_LBUTTON:
        return "M1";
    case VK_RBUTTON:
        return "M2";
    case VK_MBUTTON:
        return "M3";
    case VK_XBUTTON1:
        return "M4";
    case VK_XBUTTON2:
        return "M5";
    case VK_SHIFT:
    case VK_LSHIFT:
        return "LEFT SHIFT";
    case VK_RSHIFT:
        return "RIGHT SHIFT";
    case VK_CONTROL:
    case VK_LCONTROL:
        return "LEFT CTRL";
    case VK_RCONTROL:
        return "RIGHT CTRL";
    case VK_MENU:
    case VK_LMENU:
        return "LEFT ALT";
    case VK_RMENU:
        return "RIGHT ALT";
    case VK_SPACE:
        return "SPACE";
    case VK_INSERT:
        return "INSERT";
    case VK_DELETE:
        return "DELETE";
    case VK_HOME:
        return "HOME";
    case VK_END:
        return "END";
    case VK_PRIOR:
        return "PAGE UP";
    case VK_NEXT:
        return "PAGE DOWN";
    case VK_TAB:
        return "TAB";
    case VK_RETURN:
        return "ENTER";
    case VK_BACK:
        return "BACKSPACE";
    case VK_CAPITAL:
        return "CAPS LOCK";
    case VK_NUMLOCK:
        return "NUM LOCK";
    case VK_SCROLL:
        return "SCROLL LOCK";
    case VK_PAUSE:
        return "PAUSE";
    case VK_UP:
        return "UP";
    case VK_DOWN:
        return "DOWN";
    case VK_LEFT:
        return "LEFT";
    case VK_RIGHT:
        return "RIGHT";
    default:
        if (key >= '0' && key <= '9')
            return std::string(1, static_cast<char>(key));
        if (key >= 'A' && key <= 'Z')
            return std::string(1, static_cast<char>(key));
        if (key >= VK_F1 && key <= VK_F24)
            return std::format("F{}", key - VK_F1 + 1);
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
            return std::format("NUM {}", key - VK_NUMPAD0);
        wchar_t wide_name[64]{};
        const auto scan = ::MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
        if (scan && ::GetKeyNameTextW(static_cast<LONG>(scan << 16), wide_name,
                                      static_cast<int>(std::size(wide_name))) > 0)
        {
            char utf8[128]{};
            const auto length = ::WideCharToMultiByte(CP_UTF8, 0, wide_name, -1, utf8,
                                                      static_cast<int>(std::size(utf8)), nullptr, nullptr);
            if (length > 1)
                return std::string(utf8, length - 1);
        }
        return std::format("KEY {}", key);
    }
}

[[nodiscard]] bool bind_input_is_down()
{
    for (int button = 0; button < 5; ++button)
    {
        if (ImGui::IsMouseDown(button))
            return true;
    }
    for (int key = 1; key < 256; ++key)
    {
        const auto imgui_key = overlay_input::key_from_virtual_key(key);
        if (imgui_key != ImGuiKey_None && ImGui::IsKeyDown(imgui_key))
            return true;
    }
    return false;
}

[[nodiscard]] int pressed_bind_key()
{
    constexpr int mouse_keys[]{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (int button = 0; button < static_cast<int>(std::size(mouse_keys)); ++button)
    {
        if (ImGui::IsMouseClicked(button, false))
            return mouse_keys[button];
    }

    for (int key = 1; key < 256; ++key)
    {
        const auto imgui_key = overlay_input::key_from_virtual_key(key);
        if (imgui_key != ImGuiKey_None && ImGui::IsKeyPressed(imgui_key, false))
            return key;
    }
    return 0;
}

config::visual_profile::player::info_flags::style &selected_info_flag_style(
    config::visual_profile::player::info_flags &flags, int selected)
{
    switch (std::clamp(selected, 0, 8))
    {
    case 0:
        return flags.money_style;
    case 1:
        return flags.armor_style;
    case 2:
        return flags.kit_style;
    case 3:
        return flags.scoped_style;
    case 4:
        return flags.defusing_style;
    case 5:
        return flags.flashed_style;
    case 6:
        return flags.ping_style;
    case 7:
        return flags.distance_style;
    default:
        return flags.bomb_damage_style;
    }
}

[[nodiscard]] int chams_material_row_count(const config::visual_profile::chams::material &m)
{
    constexpr auto common = 3; // material, colour, wireframe
    switch (m.type)
    {
    case config::visual_profile::chams::shaded:
        return common + 2;
    case config::visual_profile::chams::glow:
        return common + 1;
    case config::visual_profile::chams::glow_outline:
        return common + 3;
    case config::visual_profile::chams::iridescent:
        return common + 2;
    case config::visual_profile::chams::water_flow:
        return common + 1;
    case config::visual_profile::chams::glossy:
        return common + 4;
    default:
        return common;
    }
}

void card_in_column(const char *id, const char *title, int rows, int column, callback_ref callback)
{
    column = std::clamp(column, 0, 1);
    const auto card_id = ImGui::GetID(id);
    const auto card_min =
        ImVec2{g_cards_origin.x + column * (g_cards_width + 20.0f), g_cards_origin.y + g_cards_y[column]};
    ImGui::SetCursorScreenPos(card_min);
    const auto target_height = 54.0f + rows * k_row_height;
    auto &card_height = g_card_height_animations[card_id];
    if (card_height <= 0.0f)
        card_height = target_height;
    card_height = approach(card_height, target_height, 0.40f);
    const auto card_max = ImVec2{card_min.x + g_cards_width, card_min.y + card_height};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, k_bg_card);
    ImGui::PushStyleColor(ImGuiCol_Border, k_border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20.0f, 16.0f});
    ImGui::BeginChild(id, {g_cards_width, card_height}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const auto *title_wrapper = app::context().overlay.fonts().menu_semibold_13;
    const auto title_size =
        title_wrapper && title_wrapper->im_font ? title_wrapper->font_size : ImGui::GetFontSize();
    const auto title_pos = ImGui::GetCursorScreenPos();
    draw_section_title(ImGui::GetWindowDrawList(), title_pos,
                       ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x,
                       render::localization::tr(title));
    ImGui::Dummy({0.0f, title_size});
    ImGui::Dummy({0.0f, 3.0f});
    callback();

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    g_cards_y[column] += card_height + 20.0f;
}

void card(const char *id, const char *title, int rows, callback_ref callback)
{
    card_in_column(id, title, rows, g_cards_index++ % 2, callback);
}

bool tab_button(const char *label, bool active)
{
    const auto *text = render::localization::tr(label);
    const auto text_size = ImGui::CalcTextSize(text);
    const auto size = ImVec2{text_size.x + 32.0f, text_size.y + 16.0f};
    ImGui::InvisibleButton(label, size);
    const auto clicked = ImGui::IsItemClicked();
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    const auto id = ImGui::GetItemID();
    const auto hover_t = animate_state(g_hover_animations, id, hovered);
    const auto active_t = animate_selection(id, active);
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    auto *draw = ImGui::GetWindowDrawList();
    if (active_t > 0.01f)
    {
        soft_shadow(draw, min, max, 20.0f, {0.0f, 4.0f}, 9.0f, {k_accent.x, k_accent.y, k_accent.z, 1.0f},
                    active_t * 0.55f);
    }
    const auto idle = mix(k_bg_card, k_bg_hover, hover_t);
    draw->AddRectFilled(min, max, packed(mix(idle, k_accent, active_t)), 20.0f);
    if (active_t < 0.99f && hover_t > 0.01f)
        draw->AddRect(min, max, packed(mix(k_border, k_border_light, hover_t)), 20.0f);
    draw->AddText({min.x + 16.0f, min.y + (size.y - text_size.y) * 0.5f},
                  packed(mix(k_text_muted, ImVec4{1, 1, 1, 1}, std::max(hover_t, active_t))), text);
    return clicked;
}

ImVec2 svg_point(ImVec2 center, float x, float y)
{
    constexpr auto scale = 0.78f;
    return {center.x + (x - 12.0f) * scale, center.y + (y - 12.0f) * scale};
}

void svg_segment(ImDrawList *draw, ImVec2 from, ImVec2 to, ImU32 color, float thickness)
{
    draw->AddLine(from, to, color, thickness);
    const auto cap_radius = thickness * 0.5f;
    draw->AddCircleFilled(from, cap_radius, color, 12);
    draw->AddCircleFilled(to, cap_radius, color, 12);
}

void svg_polyline(ImDrawList *draw, std::span<const ImVec2> points, ImU32 color, bool closed, float thickness)
{
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        svg_segment(draw, points[i - 1], points[i], color, thickness);
    }
    if (closed && points.size() > 2)
        svg_segment(draw, points.back(), points.front(), color, thickness);
}

void draw_nav_icon(ImDrawList *draw, int icon, ImVec2 center, ImU32 color)
{
    if (icon == 0)
    {
        draw->AddCircle(center, 6.2f, color, 32, 1.65f);
        svg_segment(draw, svg_point(center, 12, 1.5f), svg_point(center, 12, 4.0f), color);
        svg_segment(draw, svg_point(center, 12, 20.0f), svg_point(center, 12, 22.5f), color);
        svg_segment(draw, svg_point(center, 1.5f, 12), svg_point(center, 4.0f, 12), color);
        svg_segment(draw, svg_point(center, 20.0f, 12), svg_point(center, 22.5f, 12), color);
        draw->AddCircleFilled(center, 1.35f, color, 18);
    }
    else if (icon == 1)
    {
        const std::array points{svg_point(center, 13, 2),  svg_point(center, 4, 14),
                                svg_point(center, 11, 14), svg_point(center, 10, 22),
                                svg_point(center, 20, 9),  svg_point(center, 13, 9)};
        svg_polyline(draw, points, color, true, 1.75f);
    }
    else if (icon == 2)
    {
        draw->PathClear();
        draw->PathLineTo(svg_point(center, 2, 12));
        draw->PathBezierCubicCurveTo(svg_point(center, 5.0f, 7.0f), svg_point(center, 8.0f, 5.0f),
                                     svg_point(center, 12, 5.0f), 12);
        draw->PathBezierCubicCurveTo(svg_point(center, 16.0f, 5.0f), svg_point(center, 19.0f, 7.0f),
                                     svg_point(center, 22, 12), 12);
        draw->PathBezierCubicCurveTo(svg_point(center, 19.0f, 17.0f), svg_point(center, 16.0f, 19.0f),
                                     svg_point(center, 12, 19.0f), 12);
        draw->PathBezierCubicCurveTo(svg_point(center, 8.0f, 19.0f), svg_point(center, 5.0f, 17.0f),
                                     svg_point(center, 2, 12), 12);
        draw->PathStroke(color, ImDrawFlags_Closed, 1.65f);
        draw->AddCircle(center, 3.1f, color, 24, 1.65f);
    }
    else
    {
        constexpr std::array<ImVec2, 4> cells{ImVec2{4.0f, 4.0f}, ImVec2{13.0f, 4.0f}, ImVec2{4.0f, 13.0f},
                                              ImVec2{13.0f, 13.0f}};
        for (const auto &cell : cells)
        {
            draw->AddRect(svg_point(center, cell.x, cell.y), svg_point(center, cell.x + 7.0f, cell.y + 7.0f),
                          color, 1.8f, 0, 1.65f);
        }
    }
}

bool nav_button(const char *label, bool active, int icon)
{
    ImGui::InvisibleButton(label, {200.0f, 48.0f});
    const auto clicked = ImGui::IsItemClicked();
    const auto hovered = ImGui::IsItemHovered();
    pointer_cursor_if_hovered();
    const auto id = ImGui::GetItemID();
    const auto hover_t = animate_state(g_hover_animations, id, hovered);
    const auto active_t = animate_selection(id, active);
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    auto *draw = ImGui::GetWindowDrawList();
    if (active_t > 0.01f)
        soft_shadow(draw, min, max, 14.0f, {0.0f, 5.0f}, 10.0f, {k_accent.x, k_accent.y, k_accent.z, 1.0f},
                    active_t * 0.70f);
    const auto idle = ImVec4{k_bg_hover.x, k_bg_hover.y, k_bg_hover.z, k_bg_hover.w * hover_t};
    const auto background = mix(idle, k_accent, active_t);
    draw->AddRectFilled(min, max, packed(background), 14.0f);
    const auto color = packed(mix(k_text_muted, ImVec4{1, 1, 1, 1}, std::max(hover_t, active_t)));
    draw_nav_icon(draw, icon, {min.x + 27.0f, min.y + 24.0f}, color);
    draw->AddText({min.x + 50.0f, min.y + (48.0f - ImGui::GetTextLineHeight()) * 0.5f}, color,
                  render::localization::tr(label));
    return clicked;
}

void begin_cards(const char *id)
{
    ImGui::PushID(id);
    g_cards_origin = ImGui::GetCursorScreenPos();
    g_cards_width = (ImGui::GetContentRegionAvail().x - 20.0f) * 0.5f;
    g_cards_y[0] = g_cards_y[1] = 0.0f;
    g_cards_index = 0;
}

void end_cards()
{
    const auto content_height = std::max(g_cards_y[0], g_cards_y[1]);
    ImGui::SetCursorScreenPos(g_cards_origin);
    ImGui::Dummy({g_cards_width * 2.0f + 20.0f, std::max(0.0f, content_height - 20.0f)});
    ImGui::PopID();
}
} // namespace render::menu::detail
