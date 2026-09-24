#include <stdafx.hpp>
#include <system/frame_schedule.hpp>
#include <render/overlay/graphics_device.hpp>
#include <scripting/runtime.hpp>
#include <app/context.hpp>
#include <core/input/hotkeys.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/misc.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/event_log.hpp>
#include <render/chams/preview.hpp>
#include <render/chams/renderer.hpp>
#include <render/chams/texture.hpp>
#include <render/menu/localization.hpp>
#include <render/overlay/overlay.hpp>
#include <resources/fonts/notosans_medium.hpp>
#include <resources/fonts/weapons.hpp>
#include <resources/images/ct.hpp>
#include <resources/watermark_loss_icon.hpp>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <render/overlay/ui.hpp>
#include <wincodec.h>
#include <commdlg.h>
#include <fstream>
#include <filesystem>
#include <limits>
#include <optional>
#include <wrl/client.h>

#include <render/overlay/hud.hpp>

namespace render::hud
{

constexpr ImU32 k_wm_bg = IM_COL32(13, 13, 18, 140);
constexpr ImU32 k_wm_border = IM_COL32(255, 255, 255, 18);
constexpr ImU32 k_wm_text = IM_COL32(248, 248, 242, 255);
constexpr ImU32 k_wm_muted = IM_COL32(150, 150, 160, 255);

// Uppercase, matching the sidebar wordmark.
constexpr const char *k_wm_brand = "VESTA";

enum class stat_icon
{
    ping,
    loss,
    cpu
};

void draw_stat_icon(ImDrawList *dl, stat_icon icon, float x, float y, float size, ImU32 color)
{
    if (!dl || size <= 0.0f)
        return;

    if (icon == stat_icon::cpu)
    {
        // Original CPU chip: rounded body plus three pill-shaped pins per side.
        constexpr float body_inset = 0.1822f;
        constexpr float pin_w = 0.1018f;
        constexpr float pin_len = 0.1332f;
        constexpr float pin_centers[3]{0.5f - 0.1657f, 0.5f, 0.5f + 0.1657f};
        const ImVec2 body_min{x + size * body_inset, y + size * body_inset};
        const ImVec2 body_max{x + size * (1.0f - body_inset), y + size * (1.0f - body_inset)};
        const auto pin_width = size * pin_w;
        const auto pin_length = size * pin_len;
        const auto pin_rounding = pin_width * 0.5f;

        for (const auto center : pin_centers)
        {
            const auto pin_x = x + size * center - pin_width * 0.5f;
            const auto pin_y = y + size * center - pin_width * 0.5f;
            dl->AddRectFilled({pin_x, y}, {pin_x + pin_width, y + pin_length}, color, pin_rounding);
            dl->AddRectFilled({pin_x, y + size - pin_length}, {pin_x + pin_width, y + size}, color,
                              pin_rounding);
            dl->AddRectFilled({x, pin_y}, {x + pin_length, pin_y + pin_width}, color, pin_rounding);
            dl->AddRectFilled({x + size - pin_length, pin_y}, {x + size, pin_y + pin_width}, color,
                              pin_rounding);
        }
        dl->AddRectFilled(body_min, body_max, color, size * 0.053f);
        return;
    }

    if (icon == stat_icon::ping)
    {
        // Segoe MobSignal5 reference: four equally spaced, ascending bars.
        constexpr std::array<float, 4> heights{0.24f, 0.42f, 0.60f, 0.78f};
        constexpr auto left = 0.10f;
        constexpr auto bottom = 0.88f;
        constexpr auto bar_width = 0.135f;
        constexpr auto gap = 0.095f;
        for (std::size_t index{}; index < heights.size(); ++index)
        {
            const auto bar_x = x + size * (left + static_cast<float>(index) * (bar_width + gap));
            const auto bar_bottom = y + size * bottom;
            const auto bar_top = bar_bottom - size * heights[index];
            const auto width = size * bar_width;
            dl->AddRectFilled({bar_x, bar_top}, {bar_x + width, bar_bottom}, color, width * 0.46f);
        }
        return;
    }

    if (icon == stat_icon::loss)
    {

        const auto to_screen = [=](const float px, const float py) {
            return ImVec2{x + px * size, y + py * size};
        };
        for (const auto &contour : resources::icons::watermark_loss_contours)
        {
            dl->PathClear();
            for (auto offset = std::uint16_t{}; offset < contour.count; ++offset)
            {
                const auto &command = resources::icons::watermark_loss_commands[contour.first + offset];
                switch (command.op)
                {
                case resources::icons::glyph_path_op::move:
                case resources::icons::glyph_path_op::line:
                    dl->PathLineTo(to_screen(command.x1, command.y1));
                    break;
                case resources::icons::glyph_path_op::quadratic:
                    dl->PathBezierQuadraticCurveTo(to_screen(command.x1, command.y1),
                                                   to_screen(command.x2, command.y2), 4);
                    break;
                case resources::icons::glyph_path_op::cubic:
                    dl->PathBezierCubicCurveTo(to_screen(command.x1, command.y1),
                                               to_screen(command.x2, command.y2),
                                               to_screen(command.x3, command.y3), 5);
                    break;
                }
            }
            dl->PathFillConcave(color);
        }
    }
}

float sampled_process_cpu()
{
    static const int num_cpu = [] {
        SYSTEM_INFO si{};
        ::GetSystemInfo(&si);
        return std::max<int>(1, static_cast<int>(si.dwNumberOfProcessors));
    }();
    static ULARGE_INTEGER prev_cpu{}, prev_wall{};
    static bool primed = false;
    static float cached = 0.0f;
    static auto next = std::chrono::steady_clock::now();

    const auto now = std::chrono::steady_clock::now();
    if (now < next)
    {
        return cached;
    }
    next = now + std::chrono::milliseconds(500);

    FILETIME create_ft{}, exit_ft{}, kernel_ft{}, user_ft{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &create_ft, &exit_ft, &kernel_ft, &user_ft))
    {
        return cached;
    }
    FILETIME wall_ft{};
    ::GetSystemTimeAsFileTime(&wall_ft);

    ULARGE_INTEGER kernel{.LowPart = kernel_ft.dwLowDateTime, .HighPart = kernel_ft.dwHighDateTime};
    ULARGE_INTEGER user{.LowPart = user_ft.dwLowDateTime, .HighPart = user_ft.dwHighDateTime};
    ULARGE_INTEGER cpu{};
    cpu.QuadPart = kernel.QuadPart + user.QuadPart;
    ULARGE_INTEGER wall{.LowPart = wall_ft.dwLowDateTime, .HighPart = wall_ft.dwHighDateTime};

    if (primed)
    {
        const auto cpu_delta = static_cast<double>(cpu.QuadPart - prev_cpu.QuadPart);
        const auto wall_delta = static_cast<double>(wall.QuadPart - prev_wall.QuadPart);
        if (wall_delta > 0.0)
        {
            cached = static_cast<float>(std::clamp(cpu_delta / wall_delta / num_cpu * 100.0, 0.0, 100.0));
        }
    }
    prev_cpu = cpu;
    prev_wall = wall;
    primed = true;
    return cached;
}

int sampled_ping()
{
    static int cached = 0;
    static auto next = std::chrono::steady_clock::now();

    const auto now = std::chrono::steady_clock::now();
    if (now < next)
    {
        return cached;
    }
    next = now + std::chrono::milliseconds(250);

    const auto controller = game::local_player().controller();
    if (!controller)
    {
        cached = 0;
        return cached;
    }
    const auto ping =
        app::context().process.load<std::int32_t>(controller + SCHEMA("CCSPlayerController", "m_iPing"_id));
    cached = std::clamp(ping, 0, 999);
    return cached;
}

float sampled_loss(int ping)
{
    static std::array<int, 16> samples{};
    static std::size_t head = 0;
    static bool primed = false;
    static float cached = 0.0f;
    static auto next = std::chrono::steady_clock::now();

    const auto now = std::chrono::steady_clock::now();
    if (now < next)
    {
        return cached;
    }
    next = now + std::chrono::milliseconds(250);

    if (!primed)
    {
        samples.fill(ping);
        primed = true;
    }
    samples[head] = ping;
    head = (head + 1) % samples.size();

    auto mean = 0.0f;
    for (const auto s : samples)
        mean += static_cast<float>(s);
    mean /= static_cast<float>(samples.size());
    auto variance = 0.0f;
    for (const auto s : samples)
    {
        const auto d = static_cast<float>(s) - mean;
        variance += d * d;
    }
    variance /= static_cast<float>(samples.size());
    const auto stddev = std::sqrt(variance);

    // Map ping standard deviation (ms) onto a 0..100 instability percentage.
    cached = std::clamp(stddev / mean * 140.0f, 0.0f, 100.0f);
    if (!std::isfinite(cached))
        cached = 0.0f;
    return cached;
}

// Green below the low threshold, amber up to the high one, red beyond.
ImU32 threshold_color(float value, float low, float high)
{
    if (value <= low)
        return k_wm_text;
    if (value >= high)
        return IM_COL32(235, 70, 70, 255);
    const auto t = (value - low) / (high - low);
    if (t < 0.5f)
        return IM_COL32(235, 200, 90, 255);
    return IM_COL32(235, 130, 70, 255);
}

ImU32 loss_icon_color(float loss, float low, float high)
{
    if (loss <= low)
        return k_wm_muted;
    if (loss >= high)
        return IM_COL32(235, 70, 70, 255);
    const auto t = (loss - low) / (high - low);
    const auto lerp = [](int a, int b, float u) {
        return static_cast<int>(a + static_cast<float>(b - a) * u);
    };
    return IM_COL32(lerp(150, 235, t), lerp(150, 70, t), lerp(160, 70, t), 255);
}

[[nodiscard]] float overlay_dpi_scale()
{

    return app::context().overlay.ui_dpi_scale();
}

[[nodiscard]] ImVec2 physical_mouse_position()
{
    POINT cursor{};
    const auto window = app::context().overlay.hwnd();
    if (window && ::GetCursorPos(&cursor) && ::ScreenToClient(window, &cursor))
    {
        return {static_cast<float>(cursor.x), static_cast<float>(cursor.y)};
    }

    auto mouse = ImGui::GetIO().MousePos;
    const auto display = ImGui::GetIO().DisplaySize;
    app::context().menu.map_pointer_to_screen(mouse.x, mouse.y, display.x, display.y);
    return mouse;
}

class screen_ui_space final
{
  public:
    screen_ui_space()
    {
        m_display = ImGui::GetIO().DisplaySize;
        const auto &overlay = app::context().overlay;
        if (!overlay.ui_uses_fullscreen_canvas() || overlay.ui_reference_width() == 0 ||
            overlay.ui_reference_height() == 0)
        {
            return;
        }
        m_position_scale = {m_display.x / std::max(1.0f, static_cast<float>(overlay.ui_reference_width())),
                            m_display.y / std::max(1.0f, static_cast<float>(overlay.ui_reference_height()))};
    }

    [[nodiscard]] ImVec2 display() const noexcept
    {
        return m_display;
    }
    [[nodiscard]] ImVec2 position_scale() const noexcept
    {
        return m_position_scale;
    }
    [[nodiscard]] ImVec2 to_layout(const ImVec2 point) const noexcept
    {
        return point;
    }

  private:
    ImVec2 m_display{};
    ImVec2 m_position_scale{1.0f, 1.0f};
};

[[nodiscard]] ImVec2 resolve_screen_layout(const config::general_profile::screen_layout &layout,
                                           const ImVec2 panel_size, const ImVec2 display,
                                           const float dpi_scale, const ImVec2 position_scale)
{
    const auto x_offset = std::max(0.0f, layout.offset_x) * dpi_scale * position_scale.x;
    const auto y_offset = std::max(0.0f, layout.offset_y) * dpi_scale * position_scale.y;
    float x = x_offset;
    float y = y_offset;
    using anchor = config::general_profile::screen_anchor;
    if (layout.anchor == anchor::top_right || layout.anchor == anchor::bottom_right)
    {
        x = display.x - panel_size.x - x_offset;
    }
    if (layout.anchor == anchor::bottom_left || layout.anchor == anchor::bottom_right)
    {
        y = display.y - panel_size.y - y_offset;
    }
    return {std::clamp(x, 0.0f, std::max(0.0f, display.x - panel_size.x)),
            std::clamp(y, 0.0f, std::max(0.0f, display.y - panel_size.y))};
}

void capture_screen_layout(config::general_profile::screen_layout &layout, const ImVec2 position,
                           const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
                           const ImVec2 position_scale)
{
    using anchor = config::general_profile::screen_anchor;
    const bool right = position.x + panel_size.x * 0.5f > display.x * 0.5f;
    const bool bottom = position.y + panel_size.y * 0.5f > display.y * 0.5f;
    layout.anchor = bottom ? (right ? anchor::bottom_right : anchor::bottom_left)
                           : (right ? anchor::top_right : anchor::top_left);
    const auto safe_x = std::max(0.01f, dpi_scale * position_scale.x);
    const auto safe_y = std::max(0.01f, dpi_scale * position_scale.y);
    layout.offset_x = std::max(0.0f, right ? display.x - position.x - panel_size.x : position.x) / safe_x;
    layout.offset_y = std::max(0.0f, bottom ? display.y - position.y - panel_size.y : position.y) / safe_y;
    layout.version = 1;
}

void migrate_screen_layout(config::general_profile::screen_layout &layout, const ImVec2 panel_size,
                           const ImVec2 display, const float dpi_scale, const ImVec2 position_scale)
{
    if (layout.version >= 1)
        return;
    capture_screen_layout(layout, {layout.legacy_position_x, layout.legacy_position_y}, panel_size, display,
                          dpi_scale, position_scale);
}

void draw_watermark(bool interactive)
{
    auto &cfg = config::general_settings.m_watermark;

    if (!cfg.enabled)
    {
        return;
    }

    // Rolling overlay frame rate.
    static int frames = 0;
    static int fps = 0;
    static auto fps_reset = std::chrono::steady_clock::now();
    ++frames;
    const auto now = std::chrono::steady_clock::now();
    if (now - fps_reset >= std::chrono::milliseconds(500))
    {
        fps = frames * 2;
        frames = 0;
        fps_reset = now;
    }

    const auto ping = sampled_ping();
    const auto loss = sampled_loss(ping);
    const auto cpu = sampled_process_cpu();

    // "vesta" uses the menu's brand font (same as the sidebar logo) at a much
    // smaller size; the stats use the menu's regular font.
    const auto *brand_wrap = app::context().overlay.fonts().menu_brand_30;
    const auto *text_wrap = app::context().overlay.fonts().menu_regular_12;
    auto *brand_font = brand_wrap && brand_wrap->im_font ? brand_wrap->im_font : ImGui::GetFont();
    auto *text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
    const auto dpi_scale = overlay_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto user_scale = cfg.layout.scale;
    const auto metric_scale = dpi_scale * user_scale;
    const float brand_size = 22.5f * metric_scale;
    const auto text_size =
        (text_wrap && text_wrap->im_font ? text_wrap->font_size : ImGui::GetFontSize()) * user_scale;

    const auto measure = [](ImFont *font, float size, const char *text) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    };

    const auto centered_y = [](ImFont *font, float size, std::initializer_list<const char *> parts,
                               float box_top, float box_h) {
        auto *baked = font->GetFontBaked(size);
        float top = FLT_MAX, bot = -FLT_MAX;
        if (baked)
        {
            for (const char *s : parts)
                for (const char *p = s; *p; ++p)
                {
                    const auto *g = baked->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(*p)));
                    if (!g || !g->Visible)
                        continue;
                    top = std::min(top, g->Y0);
                    bot = std::max(bot, g->Y1);
                }
        }
        if (top > bot)
        {
            top = 0.0f;
            bot = size;
        }
        return box_top + (box_h - (bot - top)) * 0.5f - top;
    };

    struct chip
    {
        stat_icon icon;
        bool no_icon;
        char value[24];
        ImU32 value_col;
        ImU32 icon_col;
    };
    std::array<chip, 4> chips{};
    int chip_count = 0;
    if (cfg.show_fps)
    {
        auto &c = chips[chip_count++];
        c.no_icon = true;
        std::snprintf(c.value, sizeof(c.value), "%d FPS", fps);
        c.value_col = k_wm_text;
    }
    if (cfg.show_ping)
    {
        auto &c = chips[chip_count++];
        c.icon = stat_icon::ping;
        std::snprintf(c.value, sizeof(c.value), "%dms", ping);
        c.value_col = threshold_color(static_cast<float>(ping), 80.0f, 150.0f);
        c.icon_col = k_wm_muted;
    }
    if (cfg.show_loss)
    {
        auto &c = chips[chip_count++];
        c.icon = stat_icon::loss;
        std::snprintf(c.value, sizeof(c.value), "%.0f%%", loss);
        c.value_col = threshold_color(loss, 2.0f, 15.0f);
        c.icon_col = loss_icon_color(loss, 2.0f, 15.0f);
    }
    if (cfg.show_cpu)
    {
        auto &c = chips[chip_count++];
        c.icon = stat_icon::cpu;
        std::snprintf(c.value, sizeof(c.value), "%.1f%%", cpu);
        c.value_col = threshold_color(cpu, 8.0f, 25.0f);
        c.icon_col = k_wm_muted;
    }

    // Every metric reserves the same square em-box, which keeps the glyph/value
    // rhythm identical in both horizontal and vertical watermark layouts.
    const float icon_w = text_size;

    const auto brand_w = measure(brand_font, brand_size, k_wm_brand);
    const auto row_h = std::max(brand_size, text_size);

    const float h_pad_l = 26.0f * metric_scale;
    const float h_pad_r = 28.0f * metric_scale;
    const float h_pad_y = 5.0f * metric_scale;
    const float h_gap = 14.0f * metric_scale;
    const float label_gap = 6.0f * metric_scale;
    const float divider_gap = 14.0f * metric_scale;

    const auto chip_line_w = [&](const chip &c) {
        return c.no_icon ? measure(text_font, text_size, c.value)
                         : icon_w + label_gap + measure(text_font, text_size, c.value);
    };

    float h_content = brand_w;
    if (chip_count > 0)
        h_content += divider_gap * 2.0f + 1.0f;
    for (int i = 0; i < chip_count; ++i)
    {
        if (i > 0)
            h_content += h_gap;
        h_content += chip_line_w(chips[i]);
    }
    const float width_h = h_content + h_pad_l + h_pad_r;
    const float height_h = row_h + h_pad_y * 2.0f;

    // --- Vertical layout metrics ---
    const float v_pad_x = 18.0f * metric_scale;
    const float v_pad_y = 12.0f * metric_scale;
    const float v_brand_gap = 10.0f * metric_scale;
    const float v_line_gap = 7.0f * metric_scale;
    float v_inner = brand_w;
    for (int i = 0; i < chip_count; ++i)
    {
        v_inner = std::max(v_inner, chip_line_w(chips[i]));
    }
    const float width_v = v_inner + v_pad_x * 2.0f;
    float v_content_h = brand_size;
    if (chip_count > 0)
        v_content_h += v_brand_gap + chip_count * text_size + (chip_count - 1) * v_line_gap;
    const float height_v = v_content_h + v_pad_y * 2.0f;

    const screen_ui_space ui_space{};
    const auto display = ui_space.display();
    const auto position_scale = ui_space.position_scale();
    const float snap = 22.0f * dpi_scale;

    // Live drag (only while the menu owns the mouse).
    static bool dragging = false;
    static ImVec2 drag_offset{};

    bool vertical = cfg.vertical;
    const ImVec2 initial_size{vertical ? width_v : width_h, vertical ? height_v : height_h};
    migrate_screen_layout(cfg.layout, initial_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(cfg.layout, initial_size, display, dpi_scale, position_scale);
    float pos_x = position.x;
    float pos_y = position.y;

    // Animated panel size for smooth grow/shrink on toggles and orientation change.
    static float anim_w = 0.0f, anim_h = 0.0f;

    if (interactive)
    {
        const auto mouse = ui_space.to_layout(physical_mouse_position());
        const auto down = ImGui::GetIO().MouseDown[0];
        const float cur_w = vertical ? width_v : width_h;
        const float cur_h = vertical ? height_v : height_h;
        const float box_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - cur_w));
        const auto inside =
            mouse.x >= box_x && mouse.x <= box_x + cur_w && mouse.y >= pos_y && mouse.y <= pos_y + cur_h;
        if (inside && !dragging && std::abs(ImGui::GetIO().MouseWheel) > 0.001f)
        {
            cfg.layout.scale = std::clamp(cfg.layout.scale + ImGui::GetIO().MouseWheel * 0.08f, 0.55f, 2.0f);
        }
        if (down && !dragging && inside)
        {
            dragging = true;
            drag_offset = {mouse.x - box_x, mouse.y - pos_y};
        }
        if (!down)
            dragging = false;
        if (dragging)
        {
            pos_x = mouse.x - drag_offset.x;
            pos_y = mouse.y - drag_offset.y;

            const auto center_x = pos_x + cur_w * 0.5f;
            const auto center_y = pos_y + cur_h * 0.5f;
            const auto near_side = pos_x <= snap || pos_x + cur_w >= display.x - snap;
            const auto near_top_or_bottom = pos_y <= snap || pos_y + cur_h >= display.y - snap;
            const auto side_middle = center_y >= display.y * 0.25f && center_y <= display.y * 0.75f;
            const auto horizontal_middle = center_x >= display.x * 0.25f && center_x <= display.x * 0.75f;
            if (!vertical && near_side && side_middle)
                vertical = true;
            else if (vertical && near_top_or_bottom && horizontal_middle)
                vertical = false;
        }
    }

    const float target_w = vertical ? width_v : width_h;
    const float target_h = vertical ? height_v : height_h;

    if (anim_w <= 0.0f)
    {
        anim_w = target_w;
        anim_h = target_h;
    }
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    const float k = 1.0f - std::pow(0.0025f, dt); // smooth critically-ish damped
    anim_w += (target_w - anim_w) * k;
    anim_h += (target_h - anim_h) * k;

    if (!dragging)
    {
        position = resolve_screen_layout(cfg.layout, {anim_w, anim_h}, display, dpi_scale, position_scale);
        pos_x = position.x;
        pos_y = position.y;
    }
    const float min_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - anim_w));
    const float min_y = std::clamp(pos_y, 0.0f, std::max(0.0f, display.y - anim_h));
    if (dragging)
    {
        capture_screen_layout(cfg.layout, {min_x, min_y}, {anim_w, anim_h}, display, dpi_scale,
                              position_scale);
    }
    cfg.vertical = vertical;

    const ImVec2 draw_min{min_x, min_y};
    const ImVec2 draw_max{min_x + anim_w, min_y + anim_h};

    auto *draw = ImGui::GetForegroundDrawList();

    const float rounding = vertical ? 14.0f * metric_scale : std::min(anim_w, anim_h) * 0.5f;

    // Drop shadow cast only to the bottom-right (offset, not an all-around halo).
    for (int layer = 5; layer >= 1; --layer)
    {
        const auto off = static_cast<float>(layer) * 1.6f * metric_scale;
        const auto a = static_cast<int>(30.0f * (1.0f - static_cast<float>(layer) / 6.0f));
        draw->AddRectFilled({draw_min.x + off, draw_min.y + off}, {draw_max.x + off, draw_max.y + off},
                            IM_COL32(0, 0, 0, a), rounding);
    }

    draw->AddRectFilled(draw_min, draw_max, k_wm_bg, rounding);
    draw->AddRect(draw_min, draw_max, k_wm_border, rounding, ImDrawFlags_None, std::max(1.0f, metric_scale));

    // Content is clipped to the animated panel so it reveals/hides cleanly while
    // the box grows or shrinks.
    draw->PushClipRect(draw_min, draw_max, true);
    if (vertical)
    {
        const float brand_x = draw_min.x + (anim_w - brand_w) * 0.5f;
        float y = draw_min.y + v_pad_y;
        draw->AddText(brand_font, brand_size,
                      {brand_x, centered_y(brand_font, brand_size, {k_wm_brand}, y, brand_size)}, k_wm_text,
                      k_wm_brand);
        y += brand_size;
        if (chip_count > 0)
        {
            y += v_brand_gap * 0.5f;
            draw->AddLine({draw_min.x + v_pad_x, y}, {draw_max.x - v_pad_x, y}, k_wm_border,
                          std::max(1.0f, metric_scale));
            y += v_brand_gap * 0.5f;

            const auto block_x = draw_min.x + (anim_w - v_inner) * 0.5f;
            for (int i = 0; i < chip_count; ++i)
            {
                const auto &c = chips[i];
                float x = block_x;
                const auto ry = centered_y(text_font, text_size, {c.value}, y, text_size);
                if (!c.no_icon)
                {
                    draw_stat_icon(draw, c.icon, x, y + (text_size - icon_w) * 0.5f, icon_w, c.icon_col);
                    x += icon_w + label_gap;
                }
                draw->AddText(text_font, text_size, {x, ry}, c.value_col, c.value);
                y += text_size + v_line_gap;
            }
        }
    }
    else
    {
        const float y_offset = 2.0f * metric_scale;
        auto cursor_x = draw_min.x + h_pad_l;

        // Для бренда
        draw->AddText(
            brand_font, brand_size,
            {cursor_x, centered_y(brand_font, brand_size, {k_wm_brand}, draw_min.y, anim_h) + y_offset},
            k_wm_text, k_wm_brand);
        cursor_x += brand_w;

        if (chip_count > 0)
        {
            cursor_x += divider_gap;
            draw->AddLine({cursor_x, draw_min.y + 6.0f * metric_scale},
                          {cursor_x, draw_max.y - 6.0f * metric_scale}, k_wm_border,
                          std::max(1.0f, metric_scale));
            cursor_x += divider_gap;
        }

        for (int i = 0; i < chip_count; ++i)
        {
            if (i > 0)
                cursor_x += h_gap;
            const auto &c = chips[i];
            const auto ry = centered_y(text_font, text_size, {c.value}, draw_min.y, anim_h) + y_offset;
            if (!c.no_icon)
            {
                const auto icon_y = draw_min.y + (anim_h - icon_w) * 0.5f + y_offset;
                draw_stat_icon(draw, c.icon, cursor_x, icon_y, icon_w, c.icon_col);
                cursor_x += icon_w + label_gap;
            }
            draw->AddText(text_font, text_size, {cursor_x, ry}, c.value_col, c.value);
            cursor_x += measure(text_font, text_size, c.value);
        }
    }
    draw->PopClipRect();
}

void draw_spectator_list(bool interactive)
{
    auto &cfg = config::general_settings.m_spectator_list;
    if (!cfg.enabled)
    {
        return;
    }

    // The 200ms observer-chain scan is collected on the game worker. Rendering
    // consumes an immutable snapshot and performs no process-memory calls.
    const auto specs = game::world().spectators();

    const auto *title_wrap = app::context().overlay.fonts().menu_semibold_13;
    const auto *text_wrap = app::context().overlay.fonts().menu_regular_12;
    auto *title_font = title_wrap && title_wrap->im_font ? title_wrap->im_font : ImGui::GetFont();
    auto *text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
    const auto dpi_scale = overlay_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto user_scale = cfg.layout.scale;
    const auto metric_scale = dpi_scale * user_scale;
    const float title_size =
        (title_wrap && title_wrap->im_font ? title_wrap->font_size : ImGui::GetFontSize()) * user_scale;
    const float text_size =
        (text_wrap && text_wrap->im_font ? text_wrap->font_size : ImGui::GetFontSize()) * user_scale;

    const auto measure = [](ImFont *font, float size, const char *text) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    };

    char header[48];
    std::snprintf(header, sizeof(header), "%s (%d)", render::localization::tr("Spectators"),
                  static_cast<int>(specs->size()));

    const float pad_x = 14.0f * metric_scale;
    const float pad_y = 10.0f * metric_scale;
    const float row_gap = 5.0f * metric_scale;
    const float title_gap = 8.0f * metric_scale;
    const float row_height = text_size;
    float inner_w = measure(title_font, title_size, header);
    for (const auto &s : *specs)
    {
        const float w = measure(text_font, text_size, s.name.c_str());
        inner_w = std::max(inner_w, w);
    }
    inner_w = std::max(inner_w, 120.0f * metric_scale);

    const int row_count = static_cast<int>(specs->size());
    const float panel_w = inner_w + pad_x * 2.0f;
    const float panel_h =
        pad_y * 2.0f + title_size +
        (row_count > 0 ? title_gap + row_count * row_height + (row_count - 1) * row_gap : 0.0f);

    const screen_ui_space ui_space{};
    const auto display = ui_space.display();
    const auto position_scale = ui_space.position_scale();
    const ImVec2 panel_size{panel_w, panel_h};
    migrate_screen_layout(cfg.layout, panel_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(cfg.layout, panel_size, display, dpi_scale, position_scale);
    float pos_x = position.x;
    float pos_y = position.y;

    static bool dragging = false;
    static ImVec2 drag_offset{};
    if (interactive)
    {
        const auto mouse = ui_space.to_layout(physical_mouse_position());
        const auto down = ImGui::GetIO().MouseDown[0];
        const bool inside =
            mouse.x >= pos_x && mouse.x <= pos_x + panel_w && mouse.y >= pos_y && mouse.y <= pos_y + panel_h;
        if (inside && !dragging && std::abs(ImGui::GetIO().MouseWheel) > 0.001f)
        {
            cfg.layout.scale = std::clamp(cfg.layout.scale + ImGui::GetIO().MouseWheel * 0.08f, 0.55f, 2.0f);
        }
        if (down && !dragging && inside)
        {
            dragging = true;
            drag_offset = {mouse.x - pos_x, mouse.y - pos_y};
        }
        if (!down)
            dragging = false;
        if (dragging)
        {
            pos_x = mouse.x - drag_offset.x;
            pos_y = mouse.y - drag_offset.y;
        }
    }

    pos_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - panel_w));
    pos_y = std::clamp(pos_y, 0.0f, std::max(0.0f, display.y - panel_h));
    if (dragging)
    {
        capture_screen_layout(cfg.layout, {pos_x, pos_y}, panel_size, display, dpi_scale, position_scale);
    }

    const ImVec2 draw_min{pos_x, pos_y};
    const ImVec2 draw_max{pos_x + panel_w, pos_y + panel_h};

    auto *draw = ImGui::GetForegroundDrawList();
    const float rounding = 8.0f * metric_scale;
    for (int layer = 5; layer >= 1; --layer)
    {
        const auto off = static_cast<float>(layer) * 1.4f * metric_scale;
        const auto a = static_cast<int>(28.0f * (1.0f - static_cast<float>(layer) / 6.0f));
        draw->AddRectFilled({draw_min.x + off, draw_min.y + off}, {draw_max.x + off, draw_max.y + off},
                            IM_COL32(0, 0, 0, a), rounding);
    }
    draw->AddRectFilled(draw_min, draw_max, k_wm_bg, rounding);
    draw->AddRect(draw_min, draw_max, k_wm_border, rounding, ImDrawFlags_None, std::max(1.0f, metric_scale));

    draw->PushClipRect(draw_min, draw_max, true);
    float y = draw_min.y + pad_y;
    draw->AddText(title_font, title_size, {draw_min.x + pad_x, y}, k_wm_text, header);
    y += title_size + title_gap;
    for (const auto &s : *specs)
    {
        const auto name_x = draw_min.x + pad_x;
        const auto text_y = y + (row_height - text_size) * 0.5f;
        draw->AddText(text_font, text_size, {name_x, text_y}, k_wm_text, s.name.c_str());
        y += row_height + row_gap;
    }
    draw->PopClipRect();
}

struct compact_panel_row
{
    std::string text{};
    ImU32 color{k_wm_text};
};

void draw_compact_panel(const char *title, const std::vector<compact_panel_row> &rows,
                        config::general_profile::screen_layout &layout, const bool interactive)
{
    if (rows.empty() && !interactive)
        return;
    const auto *title_wrap = app::context().overlay.fonts().menu_semibold_13;
    const auto *text_wrap = app::context().overlay.fonts().menu_regular_12;
    auto *title_font = title_wrap && title_wrap->im_font ? title_wrap->im_font : ImGui::GetFont();
    auto *text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
    const auto dpi = overlay_dpi_scale();
    layout.scale = std::clamp(layout.scale, 0.55f, 2.0f);
    const auto metric = dpi * layout.scale;
    const auto title_size =
        (title_wrap && title_wrap->im_font ? title_wrap->font_size : ImGui::GetFontSize()) * layout.scale;
    const auto text_size =
        (text_wrap && text_wrap->im_font ? text_wrap->font_size : ImGui::GetFontSize()) * layout.scale;
    const auto width_of = [](ImFont *font, float size, const char *value) {
        return font->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x;
    };
    const auto pad_x = 14.0f * metric, pad_y = 10.0f * metric;
    const auto gap = 5.0f * metric, title_gap = 8.0f * metric;
    auto inner_w = std::max(132.0f * metric, width_of(title_font, title_size, title));
    for (const auto &row : rows)
        inner_w = std::max(inner_w, width_of(text_font, text_size, row.text.c_str()));
    const auto row_count = std::max<std::size_t>(rows.size(), interactive ? 1 : 0);
    const ImVec2 panel_size{inner_w + pad_x * 2.0f, pad_y * 2.0f + title_size + title_gap +
                                                        row_count * text_size +
                                                        (row_count ? row_count - 1 : 0) * gap};
    const screen_ui_space ui_space{};
    const auto display = ui_space.display(), position_scale = ui_space.position_scale();
    migrate_screen_layout(layout, panel_size, display, dpi, position_scale);
    auto position = resolve_screen_layout(layout, panel_size, display, dpi, position_scale);
    static const void *dragging{};
    static ImVec2 drag_offset{};
    if (interactive)
    {
        const auto mouse = ui_space.to_layout(physical_mouse_position());
        const auto inside = mouse.x >= position.x && mouse.x <= position.x + panel_size.x &&
                            mouse.y >= position.y && mouse.y <= position.y + panel_size.y;
        if (!dragging && inside && ImGui::GetIO().MouseDown[0])
        {
            dragging = &layout;
            drag_offset = {mouse.x - position.x, mouse.y - position.y};
        }
        if (dragging == &layout && !ImGui::GetIO().MouseDown[0])
            dragging = nullptr;
        if (dragging == &layout)
        {
            position = {std::clamp(mouse.x - drag_offset.x, 0.0f, std::max(0.0f, display.x - panel_size.x)),
                        std::clamp(mouse.y - drag_offset.y, 0.0f, std::max(0.0f, display.y - panel_size.y))};
            capture_screen_layout(layout, position, panel_size, display, dpi, position_scale);
        }
    }
    auto *draw = ImGui::GetForegroundDrawList();
    const ImVec2 maximum{position.x + panel_size.x, position.y + panel_size.y};
    const auto rounding = 12.0f * metric;
    draw->AddRectFilled(position, maximum, k_wm_bg, rounding);
    draw->AddRect(position, maximum, k_wm_border, rounding, ImDrawFlags_None, std::max(1.0f, metric));
    auto y = position.y + pad_y;
    draw->AddText(title_font, title_size, {position.x + pad_x, y}, k_wm_text, title);
    y += title_size + title_gap;
    if (rows.empty())
        draw->AddText(text_font, text_size, {position.x + pad_x, y}, k_wm_muted,
                      render::localization::tr("No active entries"));
    else
        for (const auto &row : rows)
        {
            draw->AddText(text_font, text_size, {position.x + pad_x, y}, row.color, row.text.c_str());
            y += text_size + gap;
        }
}

void draw_event_log(const bool interactive)
{
    auto &cfg = config::general_settings.m_event_log;
    if (!cfg.enabled)
        return;
    const auto maximum = std::clamp(cfg.max_entries, 1, 5);
    auto entries = features::visuals::event_log().snapshot(cfg.duration, maximum);
    if (entries.empty() && !interactive)
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto dpi = overlay_dpi_scale();
    cfg.layout.scale = std::clamp(cfg.layout.scale, 0.55f, 2.0f);
    const auto metric = dpi * cfg.layout.scale;
    const auto *wrap = app::context().overlay.fonts().menu_regular_12;
    auto *font = wrap && wrap->im_font ? wrap->im_font : ImGui::GetFont();
    const auto font_size =
        (wrap && wrap->im_font ? wrap->font_size : ImGui::GetFontSize()) * cfg.layout.scale;
    const auto row_height = std::max(30.0f * metric, font_size + 16.0f * metric);
    const auto gap = 6.0f * metric;
    auto width = 172.0f * metric;
    for (const auto &entry : entries)
        width = std::max(width, font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, entry.text.c_str()).x +
                                    30.0f * metric);
    const auto visible_rows = std::max<std::size_t>(entries.size(), interactive ? 1 : 0);
    const ImVec2 bounds{width, visible_rows * row_height + (visible_rows ? visible_rows - 1 : 0) * gap};
    const screen_ui_space ui_space{};
    const auto display = ui_space.display(), position_scale = ui_space.position_scale();
    migrate_screen_layout(cfg.layout, bounds, display, dpi, position_scale);
    auto anchor = resolve_screen_layout(cfg.layout, bounds, display, dpi, position_scale);
    static bool dragging{};
    static ImVec2 drag_offset{};
    if (interactive)
    {
        const auto mouse = ui_space.to_layout(physical_mouse_position());
        const auto inside = mouse.x >= anchor.x && mouse.x <= anchor.x + bounds.x && mouse.y >= anchor.y &&
                            mouse.y <= anchor.y + bounds.y;
        if (!dragging && inside && ImGui::GetIO().MouseDown[0])
        {
            dragging = true;
            drag_offset = mouse - anchor;
        }
        if (dragging && !ImGui::GetIO().MouseDown[0])
            dragging = false;
        if (dragging)
        {
            anchor = {std::clamp(mouse.x - drag_offset.x, 0.0f, std::max(0.0f, display.x - bounds.x)),
                      std::clamp(mouse.y - drag_offset.y, 0.0f, std::max(0.0f, display.y - bounds.y))};
            capture_screen_layout(cfg.layout, anchor, bounds, display, dpi, position_scale);
        }
    }
    auto *draw = ImGui::GetForegroundDrawList();
    const auto with_alpha = [](const ImU32 color, const float factor) {
        const auto alpha =
            static_cast<ImU32>(std::lround(static_cast<float>(color >> 24) * std::clamp(factor, 0.0f, 1.0f)));
        return (color & 0x00ffffffu) | (alpha << 24);
    };
    if (entries.empty())
    {
        const ImVec2 maximum_pos{anchor.x + width, anchor.y + row_height};
        draw->AddRectFilled(anchor, maximum_pos, k_wm_bg, 9.0f * metric);
        draw->AddRect(anchor, maximum_pos, k_wm_border, 9.0f * metric);
        draw->AddText(font, font_size, anchor + ImVec2{15.0f * metric, (row_height - font_size) * 0.5f},
                      k_wm_muted, render::localization::tr("No active entries"));
        return;
    }
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto &entry = entries[i];
        const auto age = std::chrono::duration<float>(now - entry.timestamp).count();
        const auto lifetime = std::max(0.5f, cfg.duration);
        const auto fade_in = std::clamp(age / 0.16f, 0.0f, 1.0f);
        const auto fade_out = std::clamp((lifetime - age) / 0.55f, 0.0f, 1.0f);
        const auto alpha =
            fade_in * fade_in * (3.0f - 2.0f * fade_in) * fade_out * fade_out * (3.0f - 2.0f * fade_out);
        const auto rise = std::clamp(age / lifetime, 0.0f, 1.0f) * 8.0f * metric;
        const auto slide = (1.0f - fade_in) * 18.0f * metric;
        const ImVec2 minimum{anchor.x + slide, anchor.y + static_cast<float>(i) * (row_height + gap) - rise};
        const ImVec2 maximum_pos{minimum.x + width, minimum.y + row_height};
        const auto accent = entry.kind == features::visuals::event_kind::kill  ? IM_COL32(255, 112, 135, 255)
                            : entry.kind == features::visuals::event_kind::hit ? IM_COL32(120, 215, 255, 255)
                            : entry.kind == features::visuals::event_kind::blocked
                                ? IM_COL32(245, 190, 95, 255)
                                : IM_COL32(180, 188, 215, 255);
        draw->AddRectFilled(minimum, maximum_pos, with_alpha(k_wm_bg, alpha), 9.0f * metric);
        draw->AddRect(minimum, maximum_pos, with_alpha(k_wm_border, alpha), 9.0f * metric, ImDrawFlags_None,
                      std::max(1.0f, metric));
        draw->AddRectFilled(minimum, {minimum.x + 3.0f * metric, maximum_pos.y}, with_alpha(accent, alpha),
                            9.0f * metric, ImDrawFlags_RoundCornersLeft);
        draw->AddText(font, font_size, minimum + ImVec2{15.0f * metric, (row_height - font_size) * 0.5f},
                      with_alpha(k_wm_text, alpha), entry.text.c_str());
    }
}

void draw_keybind_list(const bool interactive)
{
    auto &cfg = config::general_settings.m_keybind_list;
    if (!cfg.enabled)
        return;
    std::vector<compact_panel_row> rows{};
    const auto combat = config::combat_settings.get(game::local_player().weapon_type());
    const auto mode_visible = [&](const int mode) {
        return mode == config::combat_profile::activation::always   ? cfg.show_always
               : mode == config::combat_profile::activation::toggle ? cfg.show_toggle
                                                                    : cfg.show_hold;
    };
    const auto add_combat = [&](std::string name, const bool enabled, const int mode, const int key) {
        if (!enabled || !mode_visible(mode) || !config::combat_profile::activation_active(mode, key))
            return;
        const auto *mode_name = mode == config::combat_profile::activation::always   ? "always"
                                : mode == config::combat_profile::activation::toggle ? "toggle"
                                                                                     : "hold";
        rows.push_back({std::format("{}  [{}]", std::move(name), mode_name), k_wm_text});
    };
    add_combat("Aimbot", combat.aimbot.enabled, combat.aimbot.activation_mode, combat.aimbot.key);
    add_combat("Trigger", combat.triggerbot.enabled, combat.triggerbot.activation_mode,
               combat.triggerbot.key);
    const auto &global = config::combat_settings.global;
    add_combat(
        std::format("Aim Damage: {}", static_cast<int>(std::lround(global.aimbot_min_damage_override))),
        global.aimbot_enabled && global.aimbot_min_damage_override_enabled,
        global.aimbot_min_damage_override_mode, global.aimbot_min_damage_override_key);
    add_combat(std::format("Trigger Damage: {}",
                           static_cast<int>(std::lround(global.triggerbot_min_damage_override))),
               global.triggerbot_enabled && global.triggerbot_min_damage_override_enabled,
               global.triggerbot_min_damage_override_mode, global.triggerbot_min_damage_override_key);
    const auto &player_esp = config::visual_settings.m_player;
    const auto visual_mode_visible = [&](const int mode) {
        return mode == config::visual_profile::player::always_on ? cfg.show_always
               : mode == config::visual_profile::player::toggle  ? cfg.show_toggle
                                                                 : cfg.show_hold;
    };
    if (visual_mode_visible(player_esp.activation_mode) && player_esp.active())
    {
        const auto *mode = player_esp.activation_mode == config::visual_profile::player::always_on ? "always"
                           : player_esp.activation_mode == config::visual_profile::player::toggle  ? "toggle"
                                                                                                   : "hold";
        rows.push_back({std::format("Player ESP  [{}]", mode), k_wm_text});
    }
    const auto &radar = config::visual_settings.m_radar;
    if (visual_mode_visible(radar.activation_mode) && radar.active())
    {
        const auto *mode = radar.activation_mode == config::visual_profile::radar::always_on ? "always"
                           : radar.activation_mode == config::visual_profile::radar::toggle  ? "toggle"
                                                                                             : "hold";
        rows.push_back({std::format("Radar  [{}]", mode), k_wm_text});
    }
    const auto key_down = [](int key) { return key > 0 && (::GetAsyncKeyState(key) & 0x8000) != 0; };
    const auto &misc = config::general_settings;
    if (cfg.show_hold && global.grenade_aim.enabled && key_down(global.grenade_aim.key))
        rows.push_back({"Grenade Aim  [hold]", k_wm_text});
    if (cfg.show_hold && misc.m_bunny_hop.enabled && key_down(misc.m_bunny_hop.activation_key))
        rows.push_back({"Bunny Hop  [hold]", k_wm_text});
    if (cfg.show_hold && misc.m_edge_jump.enabled && key_down(misc.m_edge_jump.activation_key))
        rows.push_back({"Edge Jump  [hold]", k_wm_text});
    if (cfg.show_hold && misc.m_nade_helper.enabled && misc.m_nade_helper.aim_assist &&
        key_down(misc.m_nade_helper.aim_key))
        rows.push_back({"Nade Helper  [hold]", k_wm_text});
    if (cfg.show_hold && misc.m_auto_stop.enabled && features::misc::auto_stop().active())
        rows.push_back({"Auto Stop  [active]", k_wm_text});
    if (cfg.show_always && config::visual_settings.m_no_flash.enabled)
        rows.push_back({"No Flash  [always]", k_wm_text});
    if (cfg.show_always && config::visual_settings.m_no_smoke.enabled)
        rows.push_back({"No Smoke  [always]", k_wm_text});
    draw_compact_panel(render::localization::tr("Active Binds"), rows, cfg.layout, interactive);
}

void draw_bomb_info(bool interactive)
{
    auto &cfg = config::visual_settings.m_bomb;
    auto &layout = config::general_settings.m_bomb_info.layout;
    if (!cfg.enabled || !cfg.show_info_panel)
        return;

    auto state = features::visuals::bomb().info_snapshot();
    if (!state.planted)
    {
        if (!interactive)
            return;
        // Menu-open preview makes the otherwise event-driven panel positionable.
        state.planted = true;
        state.time_remaining = 32.8f;
        state.timer_length = 40.0f;
        state.bomb_site = 0;
        state.predicted_damage = 124;
        state.local_health = 100;
    }

    const auto *title_wrap = app::context().overlay.fonts().menu_semibold_13;
    const auto *text_wrap = app::context().overlay.fonts().menu_semibold_13;
    const auto *icon_wrap = app::context().overlay.fonts().weapons_15;
    auto *title_font = title_wrap && title_wrap->im_font ? title_wrap->im_font : ImGui::GetFont();
    auto *text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
    auto *icon_font = icon_wrap && icon_wrap->im_font ? icon_wrap->im_font : ImGui::GetFont();

    const auto dpi_scale = overlay_dpi_scale();
    layout.scale = std::clamp(layout.scale, 0.55f, 2.0f);
    const auto user_scale = layout.scale;
    const auto metric_scale = dpi_scale * user_scale;
    const auto title_size =
        (title_wrap && title_wrap->im_font ? title_wrap->font_size : ImGui::GetFontSize()) * user_scale;
    const auto text_size =
        (text_wrap && text_wrap->im_font ? text_wrap->font_size : ImGui::GetFontSize()) * user_scale;
    const auto icon_size = 18.0f * user_scale;
    const ImVec2 panel_size{238.0f * metric_scale, 86.0f * metric_scale};

    const screen_ui_space ui_space{};
    const auto display = ui_space.display();
    const auto position_scale = ui_space.position_scale();
    migrate_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    auto position = resolve_screen_layout(layout, panel_size, display, dpi_scale, position_scale);
    float pos_x = position.x;
    float pos_y = position.y;

    static bool dragging = false;
    static ImVec2 drag_offset{};
    if (interactive)
    {
        const auto mouse = ui_space.to_layout(physical_mouse_position());
        const auto down = ImGui::GetIO().MouseDown[0];
        const auto inside = mouse.x >= pos_x && mouse.x <= pos_x + panel_size.x && mouse.y >= pos_y &&
                            mouse.y <= pos_y + panel_size.y;
        if (inside && !dragging && std::abs(ImGui::GetIO().MouseWheel) > 0.001f)
        {
            layout.scale = std::clamp(layout.scale + ImGui::GetIO().MouseWheel * 0.08f, 0.55f, 2.0f);
        }
        if (down && !dragging && inside)
        {
            dragging = true;
            drag_offset = {mouse.x - pos_x, mouse.y - pos_y};
        }
        if (!down)
            dragging = false;
        if (dragging)
        {
            pos_x = mouse.x - drag_offset.x;
            pos_y = mouse.y - drag_offset.y;
        }
    }

    pos_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - panel_size.x));
    pos_y = std::clamp(pos_y, 0.0f, std::max(0.0f, display.y - panel_size.y));
    if (dragging)
        capture_screen_layout(layout, {pos_x, pos_y}, panel_size, display, dpi_scale, position_scale);

    const ImVec2 draw_min{pos_x, pos_y};
    const ImVec2 draw_max{pos_x + panel_size.x, pos_y + panel_size.y};
    auto *draw = ImGui::GetForegroundDrawList();
    const auto rounding = 12.0f * metric_scale;
    const auto background = IM_COL32(cfg.panel_background.r, cfg.panel_background.g, cfg.panel_background.b,
                                     std::min<int>(cfg.panel_background.a, 150));
    for (int layer = 5; layer >= 1; --layer)
    {
        const auto offset = layer * 1.4f * metric_scale;
        const auto alpha = static_cast<int>(28.0f * (1.0f - static_cast<float>(layer) / 6.0f));
        draw->AddRectFilled({draw_min.x + offset, draw_min.y + offset},
                            {draw_max.x + offset, draw_max.y + offset}, IM_COL32(0, 0, 0, alpha), rounding);
    }
    draw->AddRectFilled(draw_min, draw_max, background, rounding);
    draw->AddRect(draw_min, draw_max, k_wm_border, rounding, ImDrawFlags_None, std::max(1.0f, metric_scale));

    const auto bomb_color = cfg.bomb_color_t;
    const auto bomb_u32 = IM_COL32(bomb_color.r, bomb_color.g, bomb_color.b, bomb_color.a);
    const auto danger_amount = std::clamp(1.0f - state.time_remaining / 10.0f, 0.0f, 1.0f);
    const auto mix = [danger_amount](std::uint8_t from, std::uint8_t to) {
        return static_cast<int>(from + (to - from) * danger_amount);
    };
    const auto timer_color = IM_COL32(mix(cfg.timer_text_color.r, 238), mix(cfg.timer_text_color.g, 68),
                                      mix(cfg.timer_text_color.b, 68), cfg.timer_text_color.a);

    draw->PushClipRect(draw_min, draw_max, true);
    const auto center = ImVec2{draw_min.x + 43.0f * metric_scale, draw_min.y + panel_size.y * 0.5f};
    const auto bomb_fraction =
        std::clamp(state.time_remaining / std::max(1.0f, state.timer_length), 0.0f, 1.0f);
    const auto defuse_fraction =
        state.being_defused
            ? std::clamp(state.defuse_remaining / std::max(0.1f, state.defuse_length), 0.0f, 1.0f)
            : 0.0f;
    const auto defuse_color = state.defuse_success ? IM_COL32(cfg.bomb_color_ct.r, cfg.bomb_color_ct.g,
                                                              cfg.bomb_color_ct.b, cfg.bomb_color_ct.a)
                                                   : IM_COL32(255, 92, 62, 255);
    const auto bomb_radius = 21.0f * metric_scale;
    const auto ring_width = std::max(1.6f, 2.2f * metric_scale);
    constexpr auto start = -std::numbers::pi_v<float> * 0.5f;
    draw->AddCircle(center, bomb_radius, IM_COL32(0, 0, 0, 145), 48, ring_width);
    if (bomb_fraction > 0.002f)
    {
        draw->PathArcTo(center, bomb_radius, start, start + 2.0f * std::numbers::pi_v<float> * bomb_fraction,
                        48);
        draw->PathStroke(timer_color, 0, ring_width);
    }
    if (state.being_defused && defuse_fraction > 0.002f)
    {
        draw->PathArcTo(center, bomb_radius, start,
                        start + 2.0f * std::numbers::pi_v<float> * defuse_fraction, 48);
        draw->PathStroke(defuse_color, 0, ring_width * 1.35f);
    }

    const auto *icon = state.being_defused ? "r" : "o";
    const auto icon_color = state.being_defused ? defuse_color : bomb_u32;
    const auto icon_extent = icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.0f, icon);
    draw->AddText(icon_font, icon_size, {center.x - icon_extent.x * 0.5f, center.y - icon_extent.y * 0.5f},
                  icon_color, icon);

    const auto divider_x = draw_min.x + 79.0f * metric_scale;
    draw->AddLine({divider_x, draw_min.y + 12.0f * metric_scale},
                  {divider_x, draw_max.y - 12.0f * metric_scale}, k_wm_border, std::max(1.0f, metric_scale));
    const auto text_x = divider_x + 14.0f * metric_scale;
    const auto line_height = 22.0f * metric_scale;
    const auto first_y = draw_min.y + 9.0f * metric_scale;
    const auto draw_row = [&](float y, const char *label, const std::string &value, ImU32 value_color) {
        draw->AddText(title_font, title_size, {text_x, y}, k_wm_muted, label);
        const auto label_width = title_font->CalcTextSizeA(title_size, FLT_MAX, 0.0f, label).x;
        draw->AddText(text_font, text_size, {text_x + label_width + 5.0f * metric_scale, y}, value_color,
                      value.c_str());
    };

    const auto site_value = std::string(state.bomb_site == 1 ? "B" : "A");
    auto hp_value = std::string("--");
    auto hp_color = k_wm_muted;
    if (state.predicted_damage >= 0 && state.local_health > 0)
    {
        if (state.predicted_damage >= state.local_health)
        {
            hp_value = "LETHAL";
            hp_color = IM_COL32(238, 68, 68, 255);
        }
        else
        {
            hp_value = std::to_string(std::max(0, state.local_health - state.predicted_damage));
            hp_color = IM_COL32(110, 225, 135, 255);
        }
    }
    const auto time_value = std::format("{:.1f} sec", state.time_remaining);
    draw_row(first_y, "Site:", site_value, bomb_u32);
    draw_row(first_y + line_height, "HP:", hp_value, hp_color);
    draw_row(first_y + line_height * 2.0f, "Time:", time_value, timer_color);
    draw->PopClipRect();
}

} // namespace render::hud
