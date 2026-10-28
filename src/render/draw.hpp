#pragma once

#include <foundation/color.hpp>

#include <imgui.h>
#include <render/stroke.hpp>
#include <render/text/esp_text.hpp>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <misc/freetype/imgui_freetype.h>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <array>

namespace render::draw
{

// Forward declarations
struct font;
struct font_atlas;

using rgba = foundation::rgba;

enum class text_style : std::uint8_t
{
    normal,
    outlined,
    shadowed
};

enum class font_raster_profile : std::uint8_t
{
    smooth,
    esp_text,
    esp_icon
};

[[nodiscard]] constexpr bool is_esp_profile(font_raster_profile profile) noexcept
{
    return profile == font_raster_profile::esp_text || profile == font_raster_profile::esp_icon;
}

// font wrapper around ImFont
struct font
{
    ImFont* im_font{nullptr};
    // Optional source without the embedded ESP outline. It is only used for a
    // deliberately unoutlined draw (for example the clipped ammo colour mask).
    ImFont* plain_im_font{nullptr};
    float font_size{12.0f};
    font_raster_profile raster_profile{font_raster_profile::smooth};

    float m_font_size{12.0f};
    float m_ascent{0.0f};
    float m_descent{0.0f};
    float m_line_gap{0.0f};
    float m_line_height{0.0f};
};

// Font atlas (stub for compatibility)
struct font_atlas
{
    int m_width{0};
    int m_height{0};
};

// Global font stack
inline std::vector<font*> g_font_stack{};
inline font* g_default_font{nullptr};
inline std::vector<std::unique_ptr<font>> g_owned_fonts{};

inline font* own_font(ImFont* im_font, float size_pixels, font_raster_profile raster_profile)
{
    if (!im_font)
        return nullptr;

    auto owned = std::make_unique<font>();
    owned->im_font = im_font;
    owned->font_size = size_pixels;
    owned->m_font_size = size_pixels;
    owned->m_line_height = size_pixels * 1.2f;
    owned->raster_profile = raster_profile;
    auto* result = owned.get();
    g_owned_fonts.push_back(std::move(owned));
    if (!g_default_font)
        g_default_font = result;
    return result;
}

inline void shutdown_fonts() noexcept
{
    g_font_stack.clear();
    g_default_font = nullptr;
    g_owned_fonts.clear();
}

// Push/pop font
inline void push_font(font* f)
{
    if (!f || !f->im_font)
    {
        // Fall back to default font if the requested font is invalid or null
        f = g_default_font;
        if (!f)
            return; // No default font available
    }
    g_font_stack.push_back(f);
}

inline void pop_font()
{
    if (!g_font_stack.empty())
        g_font_stack.pop_back();
}

inline font* get_current_font()
{
    if (!g_font_stack.empty())
        return g_font_stack.back();
    return g_default_font;
}

// Measure text
inline std::pair<float, float> measure_text(std::string_view text, const font* fnt = nullptr)
{
    const auto* f = fnt ? fnt : get_current_font();
    if (!f || !f->im_font)
        return {0.0f, 0.0f};
    const auto size = is_esp_profile(f->raster_profile) ? std::max(1.0f, std::round(f->font_size)) : f->font_size;
    const ImVec2 sz = f->im_font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size(), nullptr);
    return {sz.x, sz.y};
}

// Get display size
inline std::pair<int, int> get_display_size()
{
    const auto& io = ImGui::GetIO();
    return {static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y)};
}

// draw_list - wraps ImDrawList
class draw_list
{
public:
    ImDrawList* m_im_draw_list{nullptr};
    float m_alpha{1.0f};

    explicit draw_list(ImDrawList* list = nullptr, float alpha = 1.0f)
        : m_im_draw_list(list)
        , m_alpha(std::clamp(alpha, 0.0f, 1.0f))
    {
    }

    // Convert rgba to ImU32
    static ImU32 to_im_color(const rgba& c) { return IM_COL32(c.r, c.g, c.b, c.a); }

    [[nodiscard]] rgba scaled(rgba color) const
    {
        color.a = static_cast<std::uint8_t>(std::round(static_cast<float>(color.a) * m_alpha));
        return color;
    }

    void add_line(float x0, float y0, float x1, float y1, rgba color, float thickness = 1.0f)
    {
        if (!m_im_draw_list)
            return;
        m_im_draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), to_im_color(scaled(color)), thickness);
    }

    void add_rect(float x, float y, float w, float h, rgba color, float thickness = 1.0f)
    {
        if (!m_im_draw_list)
            return;
        // Draw as outline using AddRect with rounding
        const float half = thickness * 0.5f;
        m_im_draw_list->AddRect(ImVec2(x - half, y - half), ImVec2(x + w + half, y + h + half),
                                to_im_color(scaled(color)), 0.0f, ImDrawFlags_None, thickness);
    }

    void add_rect_cornered(float x, float y, float w, float h, rgba color, float corner_length, float thickness)
    {
        if (!m_im_draw_list)
            return;
        const auto corner = std::clamp(corner_length, 0.0f, std::min(w, h) * 0.5f);
        const auto right = x + w;
        const auto bottom = y + h;
        const auto packed = to_im_color(scaled(color));

        m_im_draw_list->AddLine({x, y}, {x + corner, y}, packed, thickness);
        m_im_draw_list->AddLine({x, y}, {x, y + corner}, packed, thickness);
        m_im_draw_list->AddLine({right - corner, y}, {right, y}, packed, thickness);
        m_im_draw_list->AddLine({right, y}, {right, y + corner}, packed, thickness);
        m_im_draw_list->AddLine({x, bottom - corner}, {x, bottom}, packed, thickness);
        m_im_draw_list->AddLine({x, bottom}, {x + corner, bottom}, packed, thickness);
        m_im_draw_list->AddLine({right - corner, bottom}, {right, bottom}, packed, thickness);
        m_im_draw_list->AddLine({right, bottom - corner}, {right, bottom}, packed, thickness);
    }

    void add_rect_filled(float x, float y, float w, float h, rgba color)
    {
        if (!m_im_draw_list)
            return;
        m_im_draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), to_im_color(scaled(color)));
    }

    void add_rect_filled_multi_color(float x, float y, float w, float h, rgba color_tl, rgba color_tr, rgba color_br,
                                     rgba color_bl)
    {
        if (!m_im_draw_list)
            return;
        m_im_draw_list->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h), to_im_color(scaled(color_tl)),
                                                to_im_color(scaled(color_tr)), to_im_color(scaled(color_br)),
                                                to_im_color(scaled(color_bl)));
    }

    void add_convex_poly_filled(std::span<const float> points, rgba color)
    {
        if (!m_im_draw_list || points.size() < 6)
            return;
        const size_t count = points.size() / 2;
        m_im_draw_list->AddConvexPolyFilled(reinterpret_cast<const ImVec2*>(points.data()), static_cast<int>(count),
                                            to_im_color(scaled(color)));
    }

    // Cleaned first (render/stroke.hpp): world lines projected at some view angles otherwise wobble or spike.
    void add_polyline(std::span<const float> points, rgba color, bool closed = false, float thickness = 1.0f)
    {
        if (!m_im_draw_list || points.size() < 4)
            return;
        thread_local clean_stroke stroke;
        clean_polyline(points, closed, stroke);
        static_assert(sizeof(stroke_point) == sizeof(ImVec2));
        const auto im_color = to_im_color(scaled(color));
        for (const auto& run : stroke.runs)
            m_im_draw_list->AddPolyline(reinterpret_cast<const ImVec2*>(stroke.points.data() + run.first),
                                        static_cast<int>(run.count), im_color, stroke.closed ? ImDrawFlags_Closed : 0,
                                        thickness);
    }

    void add_circle(float x, float y, float radius, rgba color, int segments = 32, float thickness = 1.0f)
    {
        if (!m_im_draw_list)
            return;
        m_im_draw_list->AddCircle(ImVec2(x, y), radius, to_im_color(scaled(color)), segments, thickness);
    }

    void add_circle_filled(float x, float y, float radius, rgba color, int segments = 32)
    {
        if (!m_im_draw_list)
            return;
        m_im_draw_list->AddCircleFilled(ImVec2(x, y), radius, to_im_color(scaled(color)), segments);
    }

    void add_text(float x, float y, std::string_view text, const font* f, rgba color,
                  text_style style = text_style::normal)
    {
        if (!m_im_draw_list)
            return;
        const auto* font = f ? f : get_current_font();
        if (!font || !font->im_font)
            return;

        const auto esp_profile = is_esp_profile(font->raster_profile);
        const float size = esp_profile ? std::max(1.0f, std::round(font->font_size)) : font->font_size;
        if (esp_profile)
        {
            x = std::floor(x);
            y = std::floor(y);
        }

        if (esp_profile)
        {
            // ESP fonts: every draw goes through the ESP text block (render/text/esp_text) so the order of outlined
            // text, shadows and the clipped ammo colour mask is kept; only `outlined` uses the outline shader.
            const ImU32 packed = to_im_color(scaled(color));
            if (style == text_style::shadowed)
            {
                const ImU32 shadow_col = IM_COL32(0, 0, 0, static_cast<std::uint8_t>(scaled(color).a * 0.6f));
                render::esp_text::enqueue(m_im_draw_list, font->im_font, size, ImVec2(x + 1, y + 1), shadow_col, text,
                                          false);
            }
            auto* esp_font = style == text_style::normal && font->plain_im_font ? font->plain_im_font : font->im_font;
            render::esp_text::enqueue(m_im_draw_list, esp_font, size, ImVec2(x, y), packed, text,
                                      style == text_style::outlined);
            return;
        }

        if (style == text_style::outlined)
        {
            const ImU32 outline_col = IM_COL32(0, 0, 0, scaled(color).a);
            m_im_draw_list->AddText(font->im_font, size, ImVec2(x - 1, y), outline_col, text.data(),
                                    text.data() + text.size());
            m_im_draw_list->AddText(font->im_font, size, ImVec2(x + 1, y), outline_col, text.data(),
                                    text.data() + text.size());
            m_im_draw_list->AddText(font->im_font, size, ImVec2(x, y - 1), outline_col, text.data(),
                                    text.data() + text.size());
            m_im_draw_list->AddText(font->im_font, size, ImVec2(x, y + 1), outline_col, text.data(),
                                    text.data() + text.size());
        }
        else if (style == text_style::shadowed)
        {
            const ImU32 shadow_col = IM_COL32(0, 0, 0, static_cast<std::uint8_t>(scaled(color).a * 0.6f));
            m_im_draw_list->AddText(font->im_font, size, ImVec2(x + 1, y + 1), shadow_col, text.data(),
                                    text.data() + text.size());
        }

        m_im_draw_list->AddText(font->im_font, size, ImVec2(x, y), to_im_color(scaled(color)), text.data(),
                                text.data() + text.size());
    }
};

// Font management
inline font* add_font_from_memory(const void* font_data, int font_size, float size_pixels,
                                  font_raster_profile raster_profile = font_raster_profile::smooth,
                                  const ImWchar* glyph_ranges = nullptr)
{
    // Validate the common TrueType and OpenType signatures before handing the
    // embedded byte array to FreeType.
    if (!font_data || font_size < 4 || !std::isfinite(size_pixels) || size_pixels <= 0.0f)
        return nullptr;
    const auto* bytes = static_cast<const unsigned char*>(font_data);
    const auto true_type =
        font_size >= 4 && bytes[0] == 0x00 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00;
    const auto open_type = font_size >= 4 && bytes[0] == 'O' && bytes[1] == 'T' && bytes[2] == 'T' && bytes[3] == 'O';
    if (!true_type && !open_type)
    {
        // Not a valid TTF font - silently skip
        return nullptr;
    }

    auto* fonts = ImGui::GetIO().Fonts;
    if (!fonts)
        return nullptr;
    ImFontConfig cfg;
    cfg.SizePixels = size_pixels;
    cfg.OversampleH = 0;
    cfg.OversampleV = 0;
    cfg.PixelSnapH = is_esp_profile(raster_profile);
    cfg.PixelSnapV = is_esp_profile(raster_profile);
    cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    cfg.FontDataOwnedByAtlas = false; // Data stays valid for lifetime of program

    ImFont* im_font =
        fonts->AddFontFromMemoryTTF(const_cast<void*>(font_data), font_size, size_pixels, &cfg, glyph_ranges);

    return own_font(im_font, size_pixels, raster_profile);
}

inline font* add_font_from_file(std::string_view filepath, float size_pixels, const ImWchar* glyph_ranges = nullptr,
                                font_raster_profile raster_profile = font_raster_profile::smooth)
{
    if (filepath.empty() || !std::isfinite(size_pixels) || size_pixels <= 0.0f)
        return nullptr;
    auto* fonts = ImGui::GetIO().Fonts;
    if (!fonts)
        return nullptr;
    ImFontConfig cfg;
    cfg.SizePixels = size_pixels;
    cfg.OversampleH = 0;
    cfg.OversampleV = 0;
    cfg.PixelSnapH = is_esp_profile(raster_profile);
    cfg.PixelSnapV = is_esp_profile(raster_profile);
    cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

    ImFont* im_font = fonts->AddFontFromFileTTF(std::string(filepath).c_str(), size_pixels, &cfg, glyph_ranges);

    return own_font(im_font, size_pixels, raster_profile);
}

inline bool merge_font_from_file(font* destination, std::string_view filepath, float size_pixels,
                                 font_raster_profile raster_profile, const ImWchar* preload_ranges = nullptr)
{
    if (!destination || !destination->im_font || filepath.empty() || !std::isfinite(size_pixels) ||
        size_pixels <= 0.0f || !ImGui::GetIO().Fonts)
        return false;

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.DstFont = destination->im_font;
    cfg.SizePixels = size_pixels;
    cfg.OversampleH = 0;
    cfg.OversampleV = 0;
    cfg.PixelSnapH = is_esp_profile(raster_profile);
    cfg.PixelSnapV = is_esp_profile(raster_profile);
    cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(std::string(filepath).c_str(), size_pixels, &cfg, preload_ranges) !=
           nullptr;
}

inline font* get_default_font()
{
    return g_default_font;
}

// Seconds since the previous ImGui frame.
inline float get_delta_time()
{
    return ImGui::GetIO().DeltaTime;
}
} // namespace render::draw
