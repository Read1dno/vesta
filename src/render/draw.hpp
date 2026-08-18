#pragma once

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <misc/freetype/imgui_freetype.h>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <memory>
#include <cmath>
#include <array>

namespace zdraw {

struct font;
struct font_atlas;

struct rgba
{
	union
	{
		std::uint32_t val;
		struct
		{
			std::uint8_t r;
			std::uint8_t g;
			std::uint8_t b;
			std::uint8_t a;
		};
	};

	constexpr rgba() : r{ 0 }, g{ 0 }, b{ 0 }, a{ 0 } {}
	constexpr rgba(std::uint32_t v) : val{ v } {}
	constexpr rgba(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_) : r{ r_ }, g{ g_ }, b{ b_ }, a{ a_ } {}

	constexpr operator std::uint32_t() const { return this->val; }

	constexpr auto to_float() const
	{
		return std::array{
			static_cast<float>(this->r) / 255.0f,
			static_cast<float>(this->g) / 255.0f,
			static_cast<float>(this->b) / 255.0f,
			static_cast<float>(this->a) / 255.0f
		};
	}
};

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

[[nodiscard]] constexpr bool is_esp_profile( font_raster_profile profile ) noexcept
{
	return profile == font_raster_profile::esp_text
		|| profile == font_raster_profile::esp_icon;
}

struct font
{
	ImFont* im_font{ nullptr };

	ImFont* plain_im_font{ nullptr };
	float font_size{ 12.0f };
	font_raster_profile raster_profile{ font_raster_profile::smooth };

	float m_font_size{ 12.0f };
	float m_ascent{ 0.0f };
	float m_descent{ 0.0f };
	float m_line_gap{ 0.0f };
	float m_line_height{ 0.0f };

	void calc_text_size(std::string_view text, float& width, float& height) const
	{
		if (!this->im_font)
		{
			width = 0;
			height = 0;
			return;
		}
		const auto resolved_size = is_esp_profile( this->raster_profile )
			? std::max( 1.0f, std::round( this->font_size ) ) : this->font_size;
		const ImVec2 sz = this->im_font->CalcTextSizeA(resolved_size, FLT_MAX, 0.0f, text.data(), text.data() + text.size(), nullptr);
		width = sz.x;
		height = sz.y;
	}

	void clear_caches() const noexcept {}
};

struct font_atlas
{
	int m_width{ 0 };
	int m_height{ 0 };
};

inline std::vector<font*> g_font_stack{};
inline font* g_default_font{ nullptr };
inline std::vector<std::unique_ptr<font>> g_owned_fonts{};

inline font* own_font(ImFont* im_font, float size_pixels,
	font_raster_profile raster_profile)
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

inline void push_font(font* f)
{
	if (!f || !f->im_font)
	{

		f = g_default_font;
		if (!f) return;
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

inline std::pair<float, float> measure_text(std::string_view text, const font* fnt = nullptr)
{
	const auto* f = fnt ? fnt : get_current_font();
	if (!f || !f->im_font)
		return { 0.0f, 0.0f };
	const auto size = is_esp_profile( f->raster_profile )
		? std::max( 1.0f, std::round( f->font_size ) ) : f->font_size;
	const ImVec2 sz = f->im_font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size(), nullptr);
	return { sz.x, sz.y };
}

inline std::pair<int, int> get_display_size()
{
	const auto& io = ImGui::GetIO();
	return { static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y) };
}

class draw_list
{
public:
	ImDrawList* m_im_draw_list{ nullptr };
	float m_alpha{ 1.0f };

	explicit draw_list(ImDrawList* list = nullptr, float alpha = 1.0f)
		: m_im_draw_list(list), m_alpha(std::clamp(alpha, 0.0f, 1.0f)) {}

	static ImU32 to_im_color(const rgba& c)
	{
		return IM_COL32(c.r, c.g, c.b, c.a);
	}

	[[nodiscard]] rgba scaled(rgba color) const
	{
		color.a = static_cast<std::uint8_t>(std::round(
			static_cast<float>(color.a) * m_alpha));
		return color;
	}

	void add_line(float x0, float y0, float x1, float y1, rgba color, float thickness = 1.0f)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), to_im_color(scaled(color)), thickness);
	}

	void add_rect(float x, float y, float w, float h, rgba color, float thickness = 1.0f)
	{
		if (!this->m_im_draw_list) return;

		const float half = thickness * 0.5f;
		this->m_im_draw_list->AddRect(
			ImVec2(x - half, y - half),
			ImVec2(x + w + half, y + h + half),
			to_im_color(scaled(color)), 0.0f, ImDrawFlags_None, thickness);
	}

	void add_rect_cornered(float x, float y, float w, float h, rgba color, float corner_length, float thickness)
	{
		if (!this->m_im_draw_list) return;
		const auto corner = std::clamp(corner_length, 0.0f, std::min(w, h) * 0.5f);
		const auto right = x + w;
		const auto bottom = y + h;
		const auto packed = to_im_color(scaled(color));

		this->m_im_draw_list->AddLine({x, y}, {x + corner, y}, packed, thickness);
		this->m_im_draw_list->AddLine({x, y}, {x, y + corner}, packed, thickness);
		this->m_im_draw_list->AddLine({right - corner, y}, {right, y}, packed, thickness);
		this->m_im_draw_list->AddLine({right, y}, {right, y + corner}, packed, thickness);
		this->m_im_draw_list->AddLine({x, bottom - corner}, {x, bottom}, packed, thickness);
		this->m_im_draw_list->AddLine({x, bottom}, {x + corner, bottom}, packed, thickness);
		this->m_im_draw_list->AddLine({right - corner, bottom}, {right, bottom}, packed, thickness);
		this->m_im_draw_list->AddLine({right, bottom - corner}, {right, bottom}, packed, thickness);
	}

	void add_rect_filled(float x, float y, float w, float h, rgba color)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), to_im_color(scaled(color)));
	}

	void add_rect_filled_multi_color(float x, float y, float w, float h, rgba color_tl, rgba color_tr, rgba color_br, rgba color_bl)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddRectFilledMultiColor(
			ImVec2(x, y), ImVec2(x + w, y + h),
			to_im_color(scaled(color_tl)), to_im_color(scaled(color_tr)),
			to_im_color(scaled(color_br)), to_im_color(scaled(color_bl)));
	}

	void add_triangle_filled(float x0, float y0, float x1, float y1, float x2, float y2, rgba color)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddTriangleFilled(
			ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2),
			to_im_color(scaled(color)));
	}

	void add_convex_poly_filled(std::span<const float> points, rgba color)
	{
		if (!this->m_im_draw_list || points.size() < 6) return;
		const size_t count = points.size() / 2;
		this->m_im_draw_list->AddConvexPolyFilled(
			reinterpret_cast<const ImVec2*>(points.data()),
			static_cast<int>(count),
			to_im_color(scaled(color)));
	}

	void add_concave_poly_filled(std::span<const float> points, rgba color)
	{
		if (!this->m_im_draw_list || points.size() < 6) return;
		const size_t count = points.size() / 2;
		this->m_im_draw_list->AddConcavePolyFilled(
			reinterpret_cast<const ImVec2*>(points.data()),
			static_cast<int>(count),
			to_im_color(scaled(color)));
	}

	void add_polyline(std::span<const float> points, rgba color, bool closed = false, float thickness = 1.0f)
	{
		if (!this->m_im_draw_list || points.size() < 4) return;
		const size_t count = points.size() / 2;
		this->m_im_draw_list->AddPolyline(
			reinterpret_cast<const ImVec2*>(points.data()),
			static_cast<int>(count),
			to_im_color(scaled(color)),
			closed ? ImDrawFlags_Closed : 0,
			thickness);
	}

	void add_circle(float x, float y, float radius, rgba color, int segments = 32, float thickness = 1.0f)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddCircle(ImVec2(x, y), radius, to_im_color(scaled(color)), segments, thickness);
	}

	void add_circle_filled(float x, float y, float radius, rgba color, int segments = 32)
	{
		if (!this->m_im_draw_list) return;
		this->m_im_draw_list->AddCircleFilled(ImVec2(x, y), radius, to_im_color(scaled(color)), segments);
	}

	void add_text(float x, float y, std::string_view text, const font* f, rgba color, text_style style = text_style::normal)
	{
		if (!this->m_im_draw_list) return;
		const auto* font = f ? f : get_current_font();
		if (!font || !font->im_font) return;

		const auto esp_profile = is_esp_profile( font->raster_profile );
		const float size = esp_profile
			? std::max( 1.0f, std::round( font->font_size ) ) : font->font_size;
		if ( esp_profile )
		{
			x = std::floor( x );
			y = std::floor( y );
		}

		if (style == text_style::outlined)
		{
			if ( !esp_profile )
			{
				const ImU32 outline_col = IM_COL32(0, 0, 0, scaled(color).a);
				this->m_im_draw_list->AddText(font->im_font, size, ImVec2(x - 1, y), outline_col, text.data(), text.data() + text.size());
				this->m_im_draw_list->AddText(font->im_font, size, ImVec2(x + 1, y), outline_col, text.data(), text.data() + text.size());
				this->m_im_draw_list->AddText(font->im_font, size, ImVec2(x, y - 1), outline_col, text.data(), text.data() + text.size());
				this->m_im_draw_list->AddText(font->im_font, size, ImVec2(x, y + 1), outline_col, text.data(), text.data() + text.size());
			}

		}
		else if (style == text_style::shadowed)
		{
			const ImU32 shadow_col = IM_COL32(0, 0, 0, static_cast<std::uint8_t>(scaled(color).a * 0.6f));
			this->m_im_draw_list->AddText(font->im_font, size, ImVec2(x + 1, y + 1), shadow_col, text.data(), text.data() + text.size());
		}

		auto* render_font = esp_profile && style == text_style::normal
			&& font->plain_im_font ? font->plain_im_font : font->im_font;
		this->m_im_draw_list->AddText(render_font, size, ImVec2(x, y), to_im_color(scaled(color)), text.data(), text.data() + text.size());
	}

	void add_text_multi_color(float x, float y, std::string_view text, const font* f, rgba color_tl, rgba color_tr, rgba color_br, rgba color_bl)
	{

		this->add_text(x, y, text, f, color_tl);
	}
};

inline font* add_font_from_memory(const void* font_data, int font_size, float size_pixels,
	int atlas_width = 512, int atlas_height = 512,
	font_raster_profile raster_profile = font_raster_profile::smooth,
	const ImWchar* glyph_ranges = nullptr)
{
	(void)atlas_width;
	(void)atlas_height;

	if (!font_data || font_size < 4 || !std::isfinite(size_pixels) || size_pixels <= 0.0f)
		return nullptr;
	const auto* bytes = static_cast<const unsigned char*>(font_data);
	const auto true_type = font_size >= 4 && bytes[0] == 0x00 && bytes[1] == 0x01
		&& bytes[2] == 0x00 && bytes[3] == 0x00;
	const auto open_type = font_size >= 4 && bytes[0] == 'O' && bytes[1] == 'T'
		&& bytes[2] == 'T' && bytes[3] == 'O';
	if (!true_type && !open_type)
	{

		return nullptr;
	}

	auto* fonts = ImGui::GetIO().Fonts;
	if (!fonts)
		return nullptr;
	ImFontConfig cfg;
	cfg.SizePixels = size_pixels;
	cfg.OversampleH = 0;
	cfg.OversampleV = 0;
	cfg.PixelSnapH = is_esp_profile( raster_profile );
	cfg.PixelSnapV = is_esp_profile( raster_profile );
	cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting
		| ( is_esp_profile( raster_profile )
			? ImGuiFreeTypeLoaderFlags_VestaEspMask : 0u );
	cfg.FontDataOwnedByAtlas = false;

	ImFont* im_font = fonts->AddFontFromMemoryTTF(
		const_cast<void*>(font_data),
		font_size,
		size_pixels, &cfg, glyph_ranges);

	return own_font(im_font, size_pixels, raster_profile);
}

inline font* add_font_from_file(std::string_view filepath, float size_pixels, int atlas_width = 512, int atlas_height = 512,
	const ImWchar* glyph_ranges = nullptr,
	font_raster_profile raster_profile = font_raster_profile::smooth)
{
	(void)atlas_width;
	(void)atlas_height;
	if (filepath.empty() || !std::isfinite(size_pixels) || size_pixels <= 0.0f)
		return nullptr;
	auto* fonts = ImGui::GetIO().Fonts;
	if (!fonts)
		return nullptr;
	ImFontConfig cfg;
	cfg.SizePixels = size_pixels;
	cfg.OversampleH = 0;
	cfg.OversampleV = 0;
	cfg.PixelSnapH = is_esp_profile( raster_profile );
	cfg.PixelSnapV = is_esp_profile( raster_profile );
	cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting
		| ( is_esp_profile( raster_profile )
			? ImGuiFreeTypeLoaderFlags_VestaEspMask : 0u );

	ImFont* im_font = fonts->AddFontFromFileTTF(std::string(filepath).c_str(), size_pixels, &cfg, glyph_ranges);

	return own_font(im_font, size_pixels, raster_profile);
}

inline bool merge_font_from_file(font* destination, std::string_view filepath,
	float size_pixels, font_raster_profile raster_profile,
	const ImWchar* preload_ranges = nullptr)
{
	if (!destination || !destination->im_font || filepath.empty()
		|| !std::isfinite(size_pixels) || size_pixels <= 0.0f
		|| !ImGui::GetIO().Fonts)
		return false;

	ImFontConfig cfg;
	cfg.MergeMode = true;
	cfg.DstFont = destination->im_font;
	cfg.SizePixels = size_pixels;
	cfg.OversampleH = 0;
	cfg.OversampleV = 0;
	cfg.PixelSnapH = is_esp_profile( raster_profile );
	cfg.PixelSnapV = is_esp_profile( raster_profile );
	cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting
		| ( is_esp_profile( raster_profile )
			? ImGuiFreeTypeLoaderFlags_VestaEspMask : 0u );
	return ImGui::GetIO().Fonts->AddFontFromFileTTF(
		std::string(filepath).c_str(), size_pixels, &cfg, preload_ranges) != nullptr;
}

inline font* get_font() { return g_default_font; }
inline font* get_default_font() { return g_default_font; }

struct json_rgba
{
	std::uint8_t r, g, b, a;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(json_rgba, r, g, b, a)
};

inline void to_json(nlohmann::json& j, const rgba& c)
{
	j = json_rgba{ c.r, c.g, c.b, c.a };
}

inline void from_json(const nlohmann::json& j, rgba& c)
{
	json_rgba tmp{};
	j.get_to(tmp);
	c.r = tmp.r; c.g = tmp.g; c.b = tmp.b; c.a = tmp.a;
}

inline bool initialize(void* device, void* context) { return true; }
inline float get_delta_time() { return ImGui::GetIO().DeltaTime; }
inline void begin_frame() {}
inline void end_frame() {}
inline draw_list& get_draw_list(int layer = 0)
{
	static thread_local draw_list s_wrapper{ ImGui::GetBackgroundDrawList() };
	s_wrapper.m_im_draw_list = ImGui::GetBackgroundDrawList();
	return s_wrapper;
}
inline void push_clip_rect(float x0, float y0, float x1, float y1) {}
inline void pop_clip_rect() {}

}
