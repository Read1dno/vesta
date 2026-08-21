#include <stdafx.hpp>
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

namespace
{
	constexpr bool k_use_composition_backend = true;

	[[nodiscard]] std::optional<std::string> utf8_from_wide(
		const std::wstring_view value )
	{
		if ( value.empty( ) ) return std::string{};
		if ( value.size( ) > static_cast<std::size_t>(
			std::numeric_limits<int>::max( ) ) ) return std::nullopt;
		const auto input_size = static_cast<int>( value.size( ) );
		const auto output_size = ::WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS, value.data( ), input_size,
			nullptr, 0, nullptr, nullptr );
		if ( output_size <= 0 ) return std::nullopt;
		std::string result( static_cast<std::size_t>( output_size ), '\0' );
		if ( ::WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS, value.data( ), input_size,
			result.data( ), output_size, nullptr, nullptr ) != output_size )
		{
			return std::nullopt;
		}
		return result;
	}

	[[nodiscard]] std::optional<std::wstring> wide_from_utf8(
		const std::string_view value )
	{
		if ( value.empty( ) ) return std::wstring{};
		if ( value.size( ) > static_cast<std::size_t>(
			std::numeric_limits<int>::max( ) ) ) return std::nullopt;
		const auto input_size = static_cast<int>( value.size( ) );
		const auto output_size = ::MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.data( ), input_size,
			nullptr, 0 );
		if ( output_size <= 0 ) return std::nullopt;
		std::wstring result( static_cast<std::size_t>( output_size ), L'\0' );
		if ( ::MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.data( ), input_size,
			result.data( ), output_size ) != output_size )
		{
			return std::nullopt;
		}
		return result;
	}

	[[nodiscard]] bool configured_overlay_content( )
	{
		const auto& visuals = config::visual_settings;
		const auto& misc = config::general_settings;
		const auto& combat = config::combat_settings.global;
		return visuals.m_player.enabled || visuals.m_item.enabled
			|| visuals.m_projectile.enabled || visuals.m_bomb.enabled
			|| visuals.m_no_flash.enabled || visuals.m_no_smoke.enabled
			|| visuals.m_crosshair.enabled
			|| ( visuals.m_player.enabled && visuals.m_chams.enabled )
			|| visuals.m_radar.enabled
			|| visuals.m_sound.enabled || misc.m_grenades.enabled
			|| misc.m_nade_helper.enabled || misc.m_watermark.enabled
			|| misc.m_spectator_list.enabled || misc.m_event_log.enabled
			|| misc.m_keybind_list.enabled || misc.m_bullet_tracers.enabled || misc.m_hitmarker.enabled
			|| misc.m_hitsound.enabled || misc.m_hitsound.show_damage
			|| combat.aimbot_draw_fov
			|| combat.penetration_crosshair;
	}

#if defined( _DEBUG ) || ( defined( VESTA_PERF_LOG ) && VESTA_PERF_LOG )
	void write_overlay_backend_diagnostic(
		const bool ui_access_enabled,
		const bool tracker_enabled,
		const DWORD tracker_error,
		const std::uint32_t tracker_stage,
		const bool input_router_enabled,
		const bool composition_enabled,
		const HWND target,
		const HWND overlay,
		const RECT& client )
	{
		wchar_t temporary[MAX_PATH]{};
		if ( !::GetTempPathW( static_cast<DWORD>( std::size( temporary ) ), temporary ) )
			return;
		std::error_code error{};
		const auto directory = std::filesystem::path( temporary ) / L"vesta";
		std::filesystem::create_directories( directory, error );
		std::ofstream stream( directory / L"overlay_backend.log", std::ios::trunc );
		DEVMODEW display_mode{};
		display_mode.dmSize = sizeof( display_mode );
		const bool display_mode_valid = ::EnumDisplaySettingsW(
			nullptr, ENUM_CURRENT_SETTINGS, &display_mode );
		const POINT client_center{
			client.left + ( client.right - client.left ) / 2,
			client.top + ( client.bottom - client.top ) / 2
		};
		const HWND center_hit = ::WindowFromPoint( client_center );
		const HWND center_root = center_hit
			? ::GetAncestor( center_hit, GA_ROOT ) : nullptr;
		stream << "ui_access=" << ui_access_enabled
			<< " tracker=" << tracker_enabled
			<< " tracker_stage=" << tracker_stage
			<< " tracker_error=" << tracker_error
			<< " input_router=" << input_router_enabled
			<< " composition=" << composition_enabled
			<< " target=0x" << std::hex
			<< reinterpret_cast<std::uintptr_t>( target )
			<< " overlay=0x" << reinterpret_cast<std::uintptr_t>( overlay )
			<< " exstyle=0x" << static_cast<std::uintptr_t>(
				::GetWindowLongPtrW( overlay, GWL_EXSTYLE ) )
			<< " style=0x" << static_cast<std::uintptr_t>(
				::GetWindowLongPtrW( overlay, GWL_STYLE ) )
			<< " center_hit=0x" << reinterpret_cast<std::uintptr_t>( center_hit )
			<< " center_root=0x" << reinterpret_cast<std::uintptr_t>( center_root )
			<< std::dec << " enabled=" << ::IsWindowEnabled( overlay )
			<< " client=" << client.left << ',' << client.top << ','
			<< client.right << ',' << client.bottom
			<< " above_target=0x" << std::hex
			<< reinterpret_cast<std::uintptr_t>(
				target ? ::GetWindow( target, GW_HWNDPREV ) : nullptr )
			<< " above_overlay=0x"
			<< reinterpret_cast<std::uintptr_t>(
				overlay ? ::GetWindow( overlay, GW_HWNDPREV ) : nullptr )
			<< std::dec << " display_mode="
			<< ( display_mode_valid ? display_mode.dmPelsWidth : 0 ) << 'x'
			<< ( display_mode_valid ? display_mode.dmPelsHeight : 0 )
			<< std::dec << '\n';
	}

	void write_overlay_lifecycle_event(
		const std::string_view event, const HWND overlay = nullptr,
		const HRESULT result = S_OK )
	{
		wchar_t temporary[ MAX_PATH ]{};
		if ( !::GetTempPathW(
			static_cast<DWORD>( std::size( temporary ) ), temporary ) )
		{
			return;
		}
		std::error_code error{};
		const auto directory = std::filesystem::path( temporary ) / L"vesta";
		std::filesystem::create_directories( directory, error );
		std::ofstream stream(
			directory / L"overlay_lifecycle.log", std::ios::app );
		stream << "tick=" << ::GetTickCount64( )
			<< " pid=" << ::GetCurrentProcessId( )
			<< " event=" << event
			<< " hwnd=0x" << std::hex
			<< reinterpret_cast<std::uintptr_t>( overlay )
			<< " hr=0x" << static_cast<unsigned long>( result )
			<< std::dec << '\n';
		stream.flush( );
	}
#else
	void write_overlay_backend_diagnostic(
		bool, bool, DWORD, std::uint32_t, bool, bool, HWND, HWND, const RECT& ) {}

	void write_overlay_lifecycle_event(
		std::string_view, HWND = nullptr, HRESULT = S_OK ) {}
#endif

	[[nodiscard]] HRESULT wait_for_gpu_idle(
		ID3D11Device* const device, ID3D11DeviceContext* const context )
	{
		if ( !device || !context )
			return S_OK;

		D3D11_QUERY_DESC description{};
		description.Query = D3D11_QUERY_EVENT;
		ID3D11Query* completion{};
		const HRESULT create_result = device->CreateQuery( &description, &completion );
		if ( FAILED( create_result ) )
			return create_result;

		context->End( completion );
		context->Flush( );

		const ULONGLONG deadline = ::GetTickCount64( ) + 250;
		HRESULT result = S_FALSE;
		while ( result == S_FALSE && ::GetTickCount64( ) < deadline )
		{
			result = context->GetData(
				completion, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH );
			if ( result == S_FALSE )
				::Sleep( 1 );
		}
		completion->Release( );
		return result == S_FALSE ? HRESULT_FROM_WIN32( WAIT_TIMEOUT ) : result;
	}

	constexpr ImU32 k_wm_bg = IM_COL32(13, 13, 18, 140);
	constexpr ImU32 k_wm_border = IM_COL32(255, 255, 255, 18);
	constexpr ImU32 k_wm_text = IM_COL32(248, 248, 242, 255);
	constexpr ImU32 k_wm_muted = IM_COL32(150, 150, 160, 255);

	constexpr const char* k_wm_brand = "VESTA";

	std::uint64_t config_fingerprint()
	{

		std::uint64_t hash = 14695981039346656037ull;
		const auto append = [&]<typename T>(const T& value)
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
			for (std::size_t i = 0; i < sizeof(T); ++i)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
		};
		append(config::combat_settings);
		append(config::visual_settings);
		append(config::general_settings);
		return hash;
	}

	enum class stat_icon { ping, loss, cpu };

	void draw_stat_icon(ImDrawList* dl, stat_icon icon, float x, float y, float size, ImU32 color)
	{
		if ( !dl || size <= 0.0f ) return;

		if ( icon == stat_icon::cpu )
		{

			constexpr float body_inset = 0.1822f;
			constexpr float pin_w = 0.1018f;
			constexpr float pin_len = 0.1332f;
			constexpr float pin_centers[ 3 ]{ 0.5f - 0.1657f, 0.5f, 0.5f + 0.1657f };
			const ImVec2 body_min{ x + size * body_inset, y + size * body_inset };
			const ImVec2 body_max{ x + size * ( 1.0f - body_inset ), y + size * ( 1.0f - body_inset ) };
			const auto pin_width = size * pin_w;
			const auto pin_length = size * pin_len;
			const auto pin_rounding = pin_width * 0.5f;

			for ( const auto center : pin_centers )
			{
				const auto pin_x = x + size * center - pin_width * 0.5f;
				const auto pin_y = y + size * center - pin_width * 0.5f;
				dl->AddRectFilled( { pin_x, y }, { pin_x + pin_width, y + pin_length }, color, pin_rounding );
				dl->AddRectFilled( { pin_x, y + size - pin_length }, { pin_x + pin_width, y + size }, color, pin_rounding );
				dl->AddRectFilled( { x, pin_y }, { x + pin_length, pin_y + pin_width }, color, pin_rounding );
				dl->AddRectFilled( { x + size - pin_length, pin_y }, { x + size, pin_y + pin_width }, color, pin_rounding );
			}
			dl->AddRectFilled( body_min, body_max, color, size * 0.053f );
			return;
		}

		if ( icon == stat_icon::ping )
		{

			constexpr std::array<float, 4> heights{ 0.24f, 0.42f, 0.60f, 0.78f };
			constexpr auto left = 0.10f;
			constexpr auto bottom = 0.88f;
			constexpr auto bar_width = 0.135f;
			constexpr auto gap = 0.095f;
			for ( std::size_t index{}; index < heights.size(); ++index )
			{
				const auto bar_x = x + size * ( left
					+ static_cast<float>( index ) * ( bar_width + gap ) );
				const auto bar_bottom = y + size * bottom;
				const auto bar_top = bar_bottom - size * heights[ index ];
				const auto width = size * bar_width;
				dl->AddRectFilled( { bar_x, bar_top },
					{ bar_x + width, bar_bottom }, color, width * 0.46f );
			}
			return;
		}

		if ( icon == stat_icon::loss )
		{

			const auto to_screen = [=]( const float px, const float py )
			{
				return ImVec2{ x + px * size, y + py * size };
			};
			for ( const auto& contour : resources::icons::watermark_loss_contours )
			{
				dl->PathClear();
				for ( auto offset = std::uint16_t{}; offset < contour.count; ++offset )
				{
					const auto& command = resources::icons::watermark_loss_commands[
						contour.first + offset ];
					switch ( command.op )
					{
					case resources::icons::glyph_path_op::move:
					case resources::icons::glyph_path_op::line:
						dl->PathLineTo( to_screen( command.x1, command.y1 ) );
						break;
					case resources::icons::glyph_path_op::quadratic:
						dl->PathBezierQuadraticCurveTo(
							to_screen( command.x1, command.y1 ),
							to_screen( command.x2, command.y2 ), 4 );
						break;
					case resources::icons::glyph_path_op::cubic:
						dl->PathBezierCubicCurveTo(
							to_screen( command.x1, command.y1 ),
							to_screen( command.x2, command.y2 ),
							to_screen( command.x3, command.y3 ), 5 );
						break;
					}
				}
				dl->PathFillConcave( color );
			}
		}
	}

	float sampled_process_cpu()
	{
		static const int num_cpu = [] { SYSTEM_INFO si{}; ::GetSystemInfo(&si); return std::max<int>(1, static_cast<int>(si.dwNumberOfProcessors)); }();
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

		ULARGE_INTEGER kernel{ .LowPart = kernel_ft.dwLowDateTime, .HighPart = kernel_ft.dwHighDateTime };
		ULARGE_INTEGER user{ .LowPart = user_ft.dwLowDateTime, .HighPart = user_ft.dwHighDateTime };
		ULARGE_INTEGER cpu{}; cpu.QuadPart = kernel.QuadPart + user.QuadPart;
		ULARGE_INTEGER wall{ .LowPart = wall_ft.dwLowDateTime, .HighPart = wall_ft.dwHighDateTime };

		if (primed)
		{
			const auto cpu_delta = static_cast<double>(cpu.QuadPart - prev_cpu.QuadPart);
			const auto wall_delta = static_cast<double>(wall.QuadPart - prev_wall.QuadPart);
			if (wall_delta > 0.0)
			{
				cached = static_cast<float>(std::clamp(cpu_delta / wall_delta / num_cpu * 100.0, 0.0, 100.0));
			}
		}
		prev_cpu = cpu; prev_wall = wall; primed = true;
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
		const auto ping = app::context().process.load<std::int32_t>(controller + SCHEMA("CCSPlayerController", "m_iPing"_id));
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
		for (const auto s : samples) mean += static_cast<float>(s);
		mean /= static_cast<float>(samples.size());
		auto variance = 0.0f;
		for (const auto s : samples) { const auto d = static_cast<float>(s) - mean; variance += d * d; }
		variance /= static_cast<float>(samples.size());
		const auto stddev = std::sqrt(variance);

		cached = std::clamp(stddev / mean * 140.0f, 0.0f, 100.0f);
		if (!std::isfinite(cached)) cached = 0.0f;
		return cached;
	}

	ImU32 threshold_color(float value, float low, float high)
	{
		if (value <= low) return k_wm_text;
		if (value >= high) return IM_COL32(235, 70, 70, 255);
		const auto t = (value - low) / (high - low);
		if (t < 0.5f) return IM_COL32(235, 200, 90, 255);
		return IM_COL32(235, 130, 70, 255);
	}

	ImU32 loss_icon_color(float loss, float low, float high)
	{
		if (loss <= low) return k_wm_muted;
		if (loss >= high) return IM_COL32(235, 70, 70, 255);
		const auto t = (loss - low) / (high - low);
		const auto lerp = [](int a, int b, float u) { return static_cast<int>(a + static_cast<float>(b - a) * u); };
		return IM_COL32(lerp(150, 235, t), lerp(150, 70, t), lerp(160, 70, t), 255);
	}

	[[nodiscard]] float overlay_dpi_scale( )
	{

		return app::context().overlay.ui_dpi_scale( );
	}

	[[nodiscard]] ImVec2 physical_mouse_position( )
	{
		POINT cursor{};
		const auto window = app::context().overlay.hwnd( );
		if ( window && ::GetCursorPos( &cursor )
			&& ::ScreenToClient( window, &cursor ) )
		{
			return { static_cast<float>( cursor.x ),
				static_cast<float>( cursor.y ) };
		}

		auto mouse = ImGui::GetIO( ).MousePos;
		const auto display = ImGui::GetIO( ).DisplaySize;
		app::context().menu.map_pointer_to_screen(
			mouse.x, mouse.y, display.x, display.y );
		return mouse;
	}

	class screen_ui_space final
	{
	public:
		screen_ui_space( )
		{
			m_display = ImGui::GetIO( ).DisplaySize;
			const auto& overlay = app::context( ).overlay;
			if ( !overlay.ui_uses_fullscreen_canvas( )
				|| overlay.ui_reference_width( ) == 0
				|| overlay.ui_reference_height( ) == 0 )
			{
				return;
			}
			m_position_scale = {
				m_display.x / std::max( 1.0f, static_cast<float>(
					overlay.ui_reference_width( ) ) ),
				m_display.y / std::max( 1.0f, static_cast<float>(
					overlay.ui_reference_height( ) ) ) };
		}

		[[nodiscard]] ImVec2 display( ) const noexcept { return m_display; }
		[[nodiscard]] ImVec2 position_scale( ) const noexcept
			{ return m_position_scale; }
		[[nodiscard]] ImVec2 to_layout( const ImVec2 point ) const noexcept
			{ return point; }

	private:
		ImVec2 m_display{};
		ImVec2 m_position_scale{ 1.0f, 1.0f };
	};

	[[nodiscard]] ImVec2 resolve_screen_layout(
		const config::general_profile::screen_layout& layout,
		const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
		const ImVec2 position_scale )
	{
		const auto x_offset = std::max( 0.0f, layout.offset_x ) * dpi_scale
			* position_scale.x;
		const auto y_offset = std::max( 0.0f, layout.offset_y ) * dpi_scale
			* position_scale.y;
		float x = x_offset;
		float y = y_offset;
		using anchor = config::general_profile::screen_anchor;
		if ( layout.anchor == anchor::top_right
			|| layout.anchor == anchor::bottom_right )
		{
			x = display.x - panel_size.x - x_offset;
		}
		if ( layout.anchor == anchor::bottom_left
			|| layout.anchor == anchor::bottom_right )
		{
			y = display.y - panel_size.y - y_offset;
		}
		return {
			std::clamp( x, 0.0f, std::max( 0.0f, display.x - panel_size.x ) ),
			std::clamp( y, 0.0f, std::max( 0.0f, display.y - panel_size.y ) ) };
	}

	void capture_screen_layout(
		config::general_profile::screen_layout& layout, const ImVec2 position,
		const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
		const ImVec2 position_scale )
	{
		using anchor = config::general_profile::screen_anchor;
		const bool right = position.x + panel_size.x * 0.5f > display.x * 0.5f;
		const bool bottom = position.y + panel_size.y * 0.5f > display.y * 0.5f;
		layout.anchor = bottom
			? ( right ? anchor::bottom_right : anchor::bottom_left )
			: ( right ? anchor::top_right : anchor::top_left );
		const auto safe_x = std::max( 0.01f, dpi_scale * position_scale.x );
		const auto safe_y = std::max( 0.01f, dpi_scale * position_scale.y );
		layout.offset_x = std::max( 0.0f, right
			? display.x - position.x - panel_size.x : position.x ) / safe_x;
		layout.offset_y = std::max( 0.0f, bottom
			? display.y - position.y - panel_size.y : position.y ) / safe_y;
		layout.version = 1;
	}

	void migrate_screen_layout(
		config::general_profile::screen_layout& layout,
		const ImVec2 panel_size, const ImVec2 display, const float dpi_scale,
		const ImVec2 position_scale )
	{
		if ( layout.version >= 1 ) return;
		capture_screen_layout( layout,
			{ layout.legacy_position_x, layout.legacy_position_y },
			panel_size, display, dpi_scale, position_scale );
	}

	void draw_watermark(bool interactive)
	{
		auto& cfg = config::general_settings.m_watermark;

		if (!cfg.enabled)
		{
			return;
		}

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

		const auto* brand_wrap = app::context().overlay.fonts().menu_brand_30;
		const auto* text_wrap = app::context().overlay.fonts().menu_regular_12;
		auto* brand_font = brand_wrap && brand_wrap->im_font ? brand_wrap->im_font : ImGui::GetFont();
		auto* text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
		const auto dpi_scale = overlay_dpi_scale( );
		cfg.layout.scale = std::clamp( cfg.layout.scale, 0.55f, 2.0f );
		const auto user_scale = cfg.layout.scale;
		const auto metric_scale = dpi_scale * user_scale;
		const float brand_size = 22.5f * metric_scale;
		const auto text_size = ( text_wrap && text_wrap->im_font
			? text_wrap->font_size : ImGui::GetFontSize( ) ) * user_scale;

		const auto measure = [](ImFont* font, float size, const char* text)
		{
			return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
		};

		const auto centered_y = [](ImFont* font, float size, std::initializer_list<const char*> parts, float box_top, float box_h)
		{
			auto* baked = font->GetFontBaked(size);
			float top = FLT_MAX, bot = -FLT_MAX;
			if (baked)
			{
				for (const char* s : parts)
					for (const char* p = s; *p; ++p)
					{
						const auto* g = baked->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(*p)));
						if (!g || !g->Visible) continue;
						top = std::min(top, g->Y0);
						bot = std::max(bot, g->Y1);
					}
			}
			if (top > bot) { top = 0.0f; bot = size; }
			return box_top + (box_h - (bot - top)) * 0.5f - top;
		};

		struct chip { stat_icon icon; bool no_icon; char value[24]; ImU32 value_col; ImU32 icon_col; };
		std::array<chip, 4> chips{};
		int chip_count = 0;
		if (cfg.show_fps)
		{
			auto& c = chips[chip_count++]; c.no_icon = true;
			std::snprintf(c.value, sizeof(c.value), "%d FPS", fps);
			c.value_col = k_wm_text;
		}
		if (cfg.show_ping)
		{
			auto& c = chips[chip_count++]; c.icon = stat_icon::ping;
			std::snprintf(c.value, sizeof(c.value), "%dms", ping);
			c.value_col = threshold_color(static_cast<float>(ping), 80.0f, 150.0f);
			c.icon_col = k_wm_muted;
		}
		if (cfg.show_loss)
		{
			auto& c = chips[chip_count++]; c.icon = stat_icon::loss;
			std::snprintf(c.value, sizeof(c.value), "%.0f%%", loss);
			c.value_col = threshold_color(loss, 2.0f, 15.0f);
			c.icon_col = loss_icon_color(loss, 2.0f, 15.0f);
		}
		if (cfg.show_cpu)
		{
			auto& c = chips[chip_count++]; c.icon = stat_icon::cpu;
			std::snprintf(c.value, sizeof(c.value), "%.1f%%", cpu);
			c.value_col = threshold_color(cpu, 8.0f, 25.0f);
			c.icon_col = k_wm_muted;
		}

		const float icon_w = text_size;

		const auto brand_w = measure(brand_font, brand_size, k_wm_brand);
		const auto row_h = std::max(brand_size, text_size);

		const float h_pad_l = 26.0f * metric_scale;
		const float h_pad_r = 28.0f * metric_scale;
		const float h_pad_y = 5.0f * metric_scale;
		const float h_gap = 14.0f * metric_scale;
		const float label_gap = 6.0f * metric_scale;
		const float divider_gap = 14.0f * metric_scale;

		const auto chip_line_w = [&](const chip& c)
		{
			return c.no_icon ? measure(text_font, text_size, c.value)
				: icon_w + label_gap + measure(text_font, text_size, c.value);
		};

		float h_content = brand_w;
		if (chip_count > 0) h_content += divider_gap * 2.0f + 1.0f;
		for (int i = 0; i < chip_count; ++i)
		{
			if (i > 0) h_content += h_gap;
			h_content += chip_line_w(chips[i]);
		}
		const float width_h = h_content + h_pad_l + h_pad_r;
		const float height_h = row_h + h_pad_y * 2.0f;

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
		if (chip_count > 0) v_content_h += v_brand_gap + chip_count * text_size + (chip_count - 1) * v_line_gap;
		const float height_v = v_content_h + v_pad_y * 2.0f;

		const screen_ui_space ui_space{};
		const auto display = ui_space.display( );
		const auto position_scale = ui_space.position_scale( );
		const float snap = 22.0f * dpi_scale;

		static bool dragging = false;
		static ImVec2 drag_offset{};

		bool vertical = cfg.vertical;
		const ImVec2 initial_size{
			vertical ? width_v : width_h,
			vertical ? height_v : height_h };
		migrate_screen_layout( cfg.layout, initial_size, display, dpi_scale,
			position_scale );
		auto position = resolve_screen_layout(
			cfg.layout, initial_size, display, dpi_scale, position_scale );
		float pos_x = position.x;
		float pos_y = position.y;

		static float anim_w = 0.0f, anim_h = 0.0f;

		if (interactive)
		{
			const auto mouse = ui_space.to_layout( physical_mouse_position( ) );
			const auto down = ImGui::GetIO().MouseDown[0];
			const float cur_w = vertical ? width_v : width_h;
			const float cur_h = vertical ? height_v : height_h;
			const float box_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - cur_w));
			const auto inside = mouse.x >= box_x && mouse.x <= box_x + cur_w && mouse.y >= pos_y && mouse.y <= pos_y + cur_h;
			if ( inside && !dragging && std::abs( ImGui::GetIO( ).MouseWheel ) > 0.001f )
			{
				cfg.layout.scale = std::clamp(
					cfg.layout.scale + ImGui::GetIO( ).MouseWheel * 0.08f,
					0.55f, 2.0f );
			}
			if (down && !dragging && inside)
			{
				dragging = true;
				drag_offset = { mouse.x - box_x, mouse.y - pos_y };
			}
			if (!down) dragging = false;
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
				if ( !vertical && near_side && side_middle )
					vertical = true;
				else if ( vertical && near_top_or_bottom && horizontal_middle )
					vertical = false;
			}
		}

		const float target_w = vertical ? width_v : width_h;
		const float target_h = vertical ? height_v : height_h;

		if (anim_w <= 0.0f) { anim_w = target_w; anim_h = target_h; }
		const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
		const float k = 1.0f - std::pow(0.0025f, dt);
		anim_w += (target_w - anim_w) * k;
		anim_h += (target_h - anim_h) * k;

		if ( !dragging )
		{
			position = resolve_screen_layout(
				cfg.layout, { anim_w, anim_h }, display, dpi_scale,
				position_scale );
			pos_x = position.x;
			pos_y = position.y;
		}
		const float min_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - anim_w));
		const float min_y = std::clamp(pos_y, 0.0f, std::max(0.0f, display.y - anim_h));
		if ( dragging )
		{
			capture_screen_layout( cfg.layout, { min_x, min_y },
				{ anim_w, anim_h }, display, dpi_scale, position_scale );
		}
		cfg.vertical = vertical;

		const ImVec2 draw_min{ min_x, min_y };
		const ImVec2 draw_max{ min_x + anim_w, min_y + anim_h };

		auto* draw = ImGui::GetForegroundDrawList();

		const float rounding = vertical
			? 14.0f * metric_scale : std::min(anim_w, anim_h) * 0.5f;

		for (int layer = 5; layer >= 1; --layer)
		{
			const auto off = static_cast<float>(layer) * 1.6f * metric_scale;
			const auto a = static_cast<int>(30.0f * (1.0f - static_cast<float>(layer) / 6.0f));
			draw->AddRectFilled({ draw_min.x + off, draw_min.y + off }, { draw_max.x + off, draw_max.y + off },
				IM_COL32(0, 0, 0, a), rounding);
		}

		draw->AddRectFilled(draw_min, draw_max, k_wm_bg, rounding);
		draw->AddRect(draw_min, draw_max, k_wm_border, rounding,
			ImDrawFlags_None, std::max( 1.0f, metric_scale ));

		draw->PushClipRect(draw_min, draw_max, true);
		if (vertical)
		{
			const float brand_x = draw_min.x + (anim_w - brand_w) * 0.5f;
			float y = draw_min.y + v_pad_y;
			draw->AddText(brand_font, brand_size, { brand_x, centered_y(brand_font, brand_size, { k_wm_brand }, y, brand_size) }, k_wm_text, k_wm_brand);
			y += brand_size;
			if (chip_count > 0)
			{
				y += v_brand_gap * 0.5f;
				draw->AddLine({ draw_min.x + v_pad_x, y }, { draw_max.x - v_pad_x, y },
					k_wm_border, std::max( 1.0f, metric_scale ));
				y += v_brand_gap * 0.5f;

				const auto block_x = draw_min.x + (anim_w - v_inner) * 0.5f;
				for (int i = 0; i < chip_count; ++i)
				{
					const auto& c = chips[i];
					float x = block_x;
					const auto ry = centered_y(text_font, text_size, { c.value }, y, text_size);
					if (!c.no_icon)
					{
						draw_stat_icon(draw, c.icon, x, y + (text_size - icon_w) * 0.5f, icon_w, c.icon_col);
						x += icon_w + label_gap;
					}
					draw->AddText(text_font, text_size, { x, ry }, c.value_col, c.value);
					y += text_size + v_line_gap;
				}
			}
		}
		else
		{
			const float y_offset = 2.0f * metric_scale;
			auto cursor_x = draw_min.x + h_pad_l;

			draw->AddText(brand_font, brand_size,
				{ cursor_x, centered_y(brand_font, brand_size, { k_wm_brand }, draw_min.y, anim_h) + y_offset },
				k_wm_text, k_wm_brand);
			cursor_x += brand_w;

			if (chip_count > 0)
			{
				cursor_x += divider_gap;
				draw->AddLine({ cursor_x, draw_min.y + 6.0f * metric_scale },
					{ cursor_x, draw_max.y - 6.0f * metric_scale }, k_wm_border,
					std::max( 1.0f, metric_scale ));
				cursor_x += divider_gap;
			}

			for (int i = 0; i < chip_count; ++i)
			{
				if (i > 0) cursor_x += h_gap;
				const auto& c = chips[i];
				const auto ry = centered_y(text_font, text_size, { c.value }, draw_min.y, anim_h) + y_offset;
				if (!c.no_icon)
				{
					const auto icon_y = draw_min.y + (anim_h - icon_w) * 0.5f + y_offset;
					draw_stat_icon(draw, c.icon, cursor_x, icon_y, icon_w, c.icon_col);
					cursor_x += icon_w + label_gap;
				}
				draw->AddText(text_font, text_size, { cursor_x, ry }, c.value_col, c.value);
				cursor_x += measure(text_font, text_size, c.value);
			}
		}
		draw->PopClipRect();
	}

	void draw_spectator_list(bool interactive)
	{
		auto& cfg = config::general_settings.m_spectator_list;
		if (!cfg.enabled)
		{
			return;
		}

		const auto specs = game::world().spectators();

		const auto* title_wrap = app::context().overlay.fonts().menu_semibold_13;
		const auto* text_wrap = app::context().overlay.fonts().menu_regular_12;
		auto* title_font = title_wrap && title_wrap->im_font ? title_wrap->im_font : ImGui::GetFont();
		auto* text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
		const auto dpi_scale = overlay_dpi_scale( );
		cfg.layout.scale = std::clamp( cfg.layout.scale, 0.55f, 2.0f );
		const auto user_scale = cfg.layout.scale;
		const auto metric_scale = dpi_scale * user_scale;
		const float title_size = ( title_wrap && title_wrap->im_font
			? title_wrap->font_size : ImGui::GetFontSize( ) ) * user_scale;
		const float text_size = ( text_wrap && text_wrap->im_font
			? text_wrap->font_size : ImGui::GetFontSize( ) ) * user_scale;

		const auto measure = [](ImFont* font, float size, const char* text)
		{
			return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
		};

		char header[48];
		std::snprintf(header, sizeof(header), "%s (%d)", render::localization::tr("Spectators"), static_cast<int>(specs->size()));

		const float pad_x = 14.0f * metric_scale;
		const float pad_y = 10.0f * metric_scale;
		const float row_gap = 5.0f * metric_scale;
		const float title_gap = 8.0f * metric_scale;
		const float row_height = text_size;
		float inner_w = measure(title_font, title_size, header);
		for (const auto& s : *specs)
		{
			const float w = measure(text_font, text_size, s.name.c_str());
			inner_w = std::max(inner_w, w);
		}
		inner_w = std::max(inner_w, 120.0f * metric_scale);

		const int row_count = static_cast<int>(specs->size());
		const float panel_w = inner_w + pad_x * 2.0f;
		const float panel_h = pad_y * 2.0f + title_size
			+ (row_count > 0 ? title_gap + row_count * row_height + (row_count - 1) * row_gap : 0.0f);

		const screen_ui_space ui_space{};
		const auto display = ui_space.display( );
		const auto position_scale = ui_space.position_scale( );
		const ImVec2 panel_size{ panel_w, panel_h };
		migrate_screen_layout( cfg.layout, panel_size, display, dpi_scale,
			position_scale );
		auto position = resolve_screen_layout(
			cfg.layout, panel_size, display, dpi_scale, position_scale );
		float pos_x = position.x;
		float pos_y = position.y;

		static bool dragging = false;
		static ImVec2 drag_offset{};
		if (interactive)
		{
			const auto mouse = ui_space.to_layout( physical_mouse_position( ) );
			const auto down = ImGui::GetIO().MouseDown[0];
			const bool inside = mouse.x >= pos_x && mouse.x <= pos_x + panel_w && mouse.y >= pos_y && mouse.y <= pos_y + panel_h;
			if ( inside && !dragging && std::abs( ImGui::GetIO( ).MouseWheel ) > 0.001f )
			{
				cfg.layout.scale = std::clamp(
					cfg.layout.scale + ImGui::GetIO( ).MouseWheel * 0.08f,
					0.55f, 2.0f );
			}
			if (down && !dragging && inside)
			{
				dragging = true;
				drag_offset = { mouse.x - pos_x, mouse.y - pos_y };
			}
			if (!down) dragging = false;
			if (dragging)
			{
				pos_x = mouse.x - drag_offset.x;
				pos_y = mouse.y - drag_offset.y;
			}
		}

		pos_x = std::clamp(pos_x, 0.0f, std::max(0.0f, display.x - panel_w));
		pos_y = std::clamp(pos_y, 0.0f, std::max(0.0f, display.y - panel_h));
		if ( dragging )
		{
			capture_screen_layout( cfg.layout, { pos_x, pos_y },
				panel_size, display, dpi_scale, position_scale );
		}

		const ImVec2 draw_min{ pos_x, pos_y };
		const ImVec2 draw_max{ pos_x + panel_w, pos_y + panel_h };

		auto* draw = ImGui::GetForegroundDrawList();
		const float rounding = 8.0f * metric_scale;
		for (int layer = 5; layer >= 1; --layer)
		{
			const auto off = static_cast<float>(layer) * 1.4f * metric_scale;
			const auto a = static_cast<int>(28.0f * (1.0f - static_cast<float>(layer) / 6.0f));
			draw->AddRectFilled({ draw_min.x + off, draw_min.y + off }, { draw_max.x + off, draw_max.y + off }, IM_COL32(0, 0, 0, a), rounding);
		}
		draw->AddRectFilled(draw_min, draw_max, k_wm_bg, rounding);
		draw->AddRect(draw_min, draw_max, k_wm_border, rounding,
			ImDrawFlags_None, std::max( 1.0f, metric_scale ));

		draw->PushClipRect(draw_min, draw_max, true);
		float y = draw_min.y + pad_y;
		draw->AddText(title_font, title_size, { draw_min.x + pad_x, y }, k_wm_text, header);
		y += title_size + title_gap;
		for (const auto& s : *specs)
		{
			const auto name_x = draw_min.x + pad_x;
			const auto text_y = y + ( row_height - text_size ) * 0.5f;
			draw->AddText(text_font, text_size, { name_x, text_y }, k_wm_text, s.name.c_str());
			y += row_height + row_gap;
		}
		draw->PopClipRect();
	}

	struct compact_panel_row { std::string text{}; ImU32 color{ k_wm_text }; };

	void draw_compact_panel( const char* title,
		const std::vector<compact_panel_row>& rows,
		config::general_profile::screen_layout& layout, const bool interactive )
	{
		if ( rows.empty() && !interactive ) return;
		const auto* title_wrap = app::context().overlay.fonts().menu_semibold_13;
		const auto* text_wrap = app::context().overlay.fonts().menu_regular_12;
		auto* title_font = title_wrap && title_wrap->im_font ? title_wrap->im_font : ImGui::GetFont();
		auto* text_font = text_wrap && text_wrap->im_font ? text_wrap->im_font : ImGui::GetFont();
		const auto dpi = overlay_dpi_scale();
		layout.scale = std::clamp( layout.scale, 0.55f, 2.0f );
		const auto metric = dpi * layout.scale;
		const auto title_size = ( title_wrap && title_wrap->im_font ? title_wrap->font_size : ImGui::GetFontSize() ) * layout.scale;
		const auto text_size = ( text_wrap && text_wrap->im_font ? text_wrap->font_size : ImGui::GetFontSize() ) * layout.scale;
		const auto width_of = []( ImFont* font, float size, const char* value )
			{ return font->CalcTextSizeA( size, FLT_MAX, 0.0f, value ).x; };
		const auto pad_x = 14.0f * metric, pad_y = 10.0f * metric;
		const auto gap = 5.0f * metric, title_gap = 8.0f * metric;
		auto inner_w = std::max( 132.0f * metric, width_of( title_font, title_size, title ) );
		for ( const auto& row : rows ) inner_w = std::max( inner_w,
			width_of( text_font, text_size, row.text.c_str() ) );
		const auto row_count = std::max<std::size_t>( rows.size(), interactive ? 1 : 0 );
		const ImVec2 panel_size{ inner_w + pad_x * 2.0f,
			pad_y * 2.0f + title_size + title_gap + row_count * text_size
				+ ( row_count ? row_count - 1 : 0 ) * gap };
		const screen_ui_space ui_space{};
		const auto display = ui_space.display(), position_scale = ui_space.position_scale();
		migrate_screen_layout( layout, panel_size, display, dpi, position_scale );
		auto position = resolve_screen_layout( layout, panel_size, display, dpi, position_scale );
		static const void* dragging{};
		static ImVec2 drag_offset{};
		if ( interactive )
		{
			const auto mouse = ui_space.to_layout( physical_mouse_position() );
			const auto inside = mouse.x >= position.x && mouse.x <= position.x + panel_size.x
				&& mouse.y >= position.y && mouse.y <= position.y + panel_size.y;
			if ( !dragging && inside && ImGui::GetIO().MouseDown[0] )
			{
				dragging = &layout;
				drag_offset = { mouse.x - position.x, mouse.y - position.y };
			}
			if ( dragging == &layout && !ImGui::GetIO().MouseDown[0] ) dragging = nullptr;
			if ( dragging == &layout )
			{
				position = { std::clamp( mouse.x - drag_offset.x, 0.0f,
					std::max( 0.0f, display.x - panel_size.x ) ),
					std::clamp( mouse.y - drag_offset.y, 0.0f,
						std::max( 0.0f, display.y - panel_size.y ) ) };
				capture_screen_layout( layout, position, panel_size, display, dpi, position_scale );
			}
		}
		auto* draw = ImGui::GetForegroundDrawList();
		const ImVec2 maximum{ position.x + panel_size.x, position.y + panel_size.y };
		const auto rounding = 12.0f * metric;
		draw->AddRectFilled( position, maximum, k_wm_bg, rounding );
		draw->AddRect( position, maximum, k_wm_border, rounding, ImDrawFlags_None,
			std::max( 1.0f, metric ) );
		auto y = position.y + pad_y;
		draw->AddText( title_font, title_size, { position.x + pad_x, y }, k_wm_text, title );
		y += title_size + title_gap;
		if ( rows.empty() )
			draw->AddText( text_font, text_size, { position.x + pad_x, y },
				k_wm_muted, render::localization::tr( "No active entries" ) );
		else for ( const auto& row : rows )
		{
			draw->AddText( text_font, text_size, { position.x + pad_x, y },
				row.color, row.text.c_str() );
			y += text_size + gap;
		}
	}

	void draw_event_log( const bool interactive )
	{
		auto& cfg = config::general_settings.m_event_log;
		if ( !cfg.enabled ) return;
		const auto maximum = std::clamp( cfg.max_entries, 1, 5 );
		auto entries = features::visuals::event_log().snapshot( cfg.duration, maximum );
		if ( entries.empty() && !interactive ) return;
		const auto now = std::chrono::steady_clock::now();
		const auto dpi = overlay_dpi_scale();
		cfg.layout.scale = std::clamp( cfg.layout.scale, 0.55f, 2.0f );
		const auto metric = dpi * cfg.layout.scale;
		const auto* wrap = app::context().overlay.fonts().menu_regular_12;
		auto* font = wrap && wrap->im_font ? wrap->im_font : ImGui::GetFont();
		const auto font_size = ( wrap && wrap->im_font ? wrap->font_size
			: ImGui::GetFontSize() ) * cfg.layout.scale;
		const auto row_height = std::max( 30.0f * metric, font_size + 16.0f * metric );
		const auto gap = 6.0f * metric;
		auto width = 172.0f * metric;
		for ( const auto& entry : entries ) width = std::max( width,
			font->CalcTextSizeA( font_size, FLT_MAX, 0.0f, entry.text.c_str() ).x
				+ 30.0f * metric );
		const auto visible_rows = std::max<std::size_t>( entries.size(), interactive ? 1 : 0 );
		const ImVec2 bounds{ width, visible_rows * row_height
			+ ( visible_rows ? visible_rows - 1 : 0 ) * gap };
		const screen_ui_space ui_space{};
		const auto display = ui_space.display(), position_scale = ui_space.position_scale();
		migrate_screen_layout( cfg.layout, bounds, display, dpi, position_scale );
		auto anchor = resolve_screen_layout( cfg.layout, bounds, display, dpi, position_scale );
		static bool dragging{};
		static ImVec2 drag_offset{};
		if ( interactive )
		{
			const auto mouse = ui_space.to_layout( physical_mouse_position() );
			const auto inside = mouse.x >= anchor.x && mouse.x <= anchor.x + bounds.x
				&& mouse.y >= anchor.y && mouse.y <= anchor.y + bounds.y;
			if ( !dragging && inside && ImGui::GetIO().MouseDown[0] )
			{
				dragging = true;
				drag_offset = mouse - anchor;
			}
			if ( dragging && !ImGui::GetIO().MouseDown[0] ) dragging = false;
			if ( dragging )
			{
				anchor = { std::clamp( mouse.x - drag_offset.x, 0.0f,
					std::max( 0.0f, display.x - bounds.x ) ),
					std::clamp( mouse.y - drag_offset.y, 0.0f,
					std::max( 0.0f, display.y - bounds.y ) ) };
				capture_screen_layout( cfg.layout, anchor, bounds, display, dpi, position_scale );
			}
		}
		auto* draw = ImGui::GetForegroundDrawList();
		const auto with_alpha = []( const ImU32 color, const float factor )
		{
			const auto alpha = static_cast<ImU32>( std::lround(
				static_cast<float>( color >> 24 ) * std::clamp( factor, 0.0f, 1.0f ) ) );
			return ( color & 0x00ffffffu ) | ( alpha << 24 );
		};
		if ( entries.empty() )
		{
			const ImVec2 maximum_pos{ anchor.x + width, anchor.y + row_height };
			draw->AddRectFilled( anchor, maximum_pos, k_wm_bg, 9.0f * metric );
			draw->AddRect( anchor, maximum_pos, k_wm_border, 9.0f * metric );
			draw->AddText( font, font_size, anchor + ImVec2{ 15.0f * metric,
				( row_height - font_size ) * 0.5f }, k_wm_muted,
				render::localization::tr( "No active entries" ) );
			return;
		}
		for ( std::size_t i = 0; i < entries.size(); ++i )
		{
			const auto& entry = entries[i];
			const auto age = std::chrono::duration<float>( now - entry.timestamp ).count();
			const auto lifetime = std::max( 0.5f, cfg.duration );
			const auto fade_in = std::clamp( age / 0.16f, 0.0f, 1.0f );
			const auto fade_out = std::clamp( ( lifetime - age ) / 0.55f, 0.0f, 1.0f );
			const auto alpha = fade_in * fade_in * ( 3.0f - 2.0f * fade_in )
				* fade_out * fade_out * ( 3.0f - 2.0f * fade_out );
			const auto rise = std::clamp( age / lifetime, 0.0f, 1.0f ) * 8.0f * metric;
			const auto slide = ( 1.0f - fade_in ) * 18.0f * metric;
			const ImVec2 minimum{ anchor.x + slide,
				anchor.y + static_cast<float>( i ) * ( row_height + gap ) - rise };
			const ImVec2 maximum_pos{ minimum.x + width, minimum.y + row_height };
			const auto accent = entry.kind == features::visuals::event_kind::kill
				? IM_COL32( 255, 112, 135, 255 ) : entry.kind == features::visuals::event_kind::hit
					? IM_COL32( 120, 215, 255, 255 ) : entry.kind == features::visuals::event_kind::blocked
						? IM_COL32( 245, 190, 95, 255 ) : IM_COL32( 180, 188, 215, 255 );
			draw->AddRectFilled( minimum, maximum_pos, with_alpha( k_wm_bg, alpha ), 9.0f * metric );
			draw->AddRect( minimum, maximum_pos, with_alpha( k_wm_border, alpha ),
				9.0f * metric, ImDrawFlags_None, std::max( 1.0f, metric ) );
			draw->AddRectFilled( minimum, { minimum.x + 3.0f * metric, maximum_pos.y },
				with_alpha( accent, alpha ), 9.0f * metric, ImDrawFlags_RoundCornersLeft );
			draw->AddText( font, font_size, minimum + ImVec2{ 15.0f * metric,
				( row_height - font_size ) * 0.5f }, with_alpha( k_wm_text, alpha ),
				entry.text.c_str() );
		}
	}

	void draw_keybind_list( const bool interactive )
	{
		auto& cfg = config::general_settings.m_keybind_list;
		if ( !cfg.enabled ) return;
		std::vector<compact_panel_row> rows{};
		const auto combat = config::combat_settings.get(
			game::local_player().weapon_type() );
		const auto mode_visible = [ & ]( const int mode )
		{
			return mode == config::combat_profile::activation::always ? cfg.show_always
				: mode == config::combat_profile::activation::toggle ? cfg.show_toggle
				: cfg.show_hold;
		};
		const auto add_combat = [ & ]( std::string name, const bool enabled,
			const int mode, const int key )
		{
			if ( !enabled || !mode_visible( mode )
				|| !config::combat_profile::activation_active( mode, key ) ) return;
			const auto* mode_name = mode == config::combat_profile::activation::always
				? "always" : mode == config::combat_profile::activation::toggle
					? "toggle" : "hold";
			rows.push_back( { std::format( "{}  [{}]", std::move( name ),
				mode_name ), k_wm_text } );
		};
		add_combat( "Aimbot", combat.aimbot.enabled,
			combat.aimbot.activation_mode, combat.aimbot.key );
		add_combat( "Trigger", combat.triggerbot.enabled,
			combat.triggerbot.activation_mode, combat.triggerbot.key );
		const auto& global = config::combat_settings.global;
		add_combat( std::format( "Aim Damage: {}",
			static_cast<int>( std::lround( global.aimbot_min_damage_override ) ) ),
			global.aimbot_enabled && global.aimbot_min_damage_override_enabled,
			global.aimbot_min_damage_override_mode,
			global.aimbot_min_damage_override_key );
		add_combat( std::format( "Trigger Damage: {}",
			static_cast<int>( std::lround( global.triggerbot_min_damage_override ) ) ),
			global.triggerbot_enabled && global.triggerbot_min_damage_override_enabled,
			global.triggerbot_min_damage_override_mode,
			global.triggerbot_min_damage_override_key );
		const auto& player_esp = config::visual_settings.m_player;
		const auto visual_mode_visible = [ & ]( const int mode )
		{
			return mode == config::visual_profile::player::always_on ? cfg.show_always
				: mode == config::visual_profile::player::toggle ? cfg.show_toggle
				: cfg.show_hold;
		};
		if ( visual_mode_visible( player_esp.activation_mode ) && player_esp.active( ) )
		{
			const auto* mode = player_esp.activation_mode
				== config::visual_profile::player::always_on ? "always"
				: player_esp.activation_mode == config::visual_profile::player::toggle
					? "toggle" : "hold";
			rows.push_back( { std::format( "Player ESP  [{}]", mode ), k_wm_text } );
		}
		const auto& radar = config::visual_settings.m_radar;
		if ( visual_mode_visible( radar.activation_mode ) && radar.active( ) )
		{
			const auto* mode = radar.activation_mode
				== config::visual_profile::radar::always_on ? "always"
				: radar.activation_mode == config::visual_profile::radar::toggle
					? "toggle" : "hold";
			rows.push_back( { std::format( "Radar  [{}]", mode ), k_wm_text } );
		}
		const auto key_down = []( int key ) { return key > 0 && ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0; };
		const auto& misc = config::general_settings;
		if ( cfg.show_hold && global.grenade_aim.enabled
			&& key_down( global.grenade_aim.key ) ) rows.push_back( { "Grenade Aim  [hold]", k_wm_text } );
		if ( cfg.show_hold && misc.m_bunny_hop.enabled && key_down( misc.m_bunny_hop.activation_key ) ) rows.push_back( { "Bunny Hop  [hold]", k_wm_text } );
		if ( cfg.show_hold && misc.m_edge_jump.enabled && key_down( misc.m_edge_jump.activation_key ) ) rows.push_back( { "Edge Jump  [hold]", k_wm_text } );
		if ( cfg.show_hold && misc.m_nade_helper.enabled && misc.m_nade_helper.aim_assist && key_down( misc.m_nade_helper.aim_key ) ) rows.push_back( { "Nade Helper  [hold]", k_wm_text } );
		if ( cfg.show_hold && misc.m_auto_stop.enabled && features::misc::auto_stop().active() ) rows.push_back( { "Auto Stop  [active]", k_wm_text } );
		if ( cfg.show_always && config::visual_settings.m_no_flash.enabled ) rows.push_back( { "No Flash  [always]", k_wm_text } );
		if ( cfg.show_always && config::visual_settings.m_no_smoke.enabled ) rows.push_back( { "No Smoke  [always]", k_wm_text } );
		draw_compact_panel( render::localization::tr( "Active Binds" ), rows, cfg.layout, interactive );
	}

	void draw_bomb_info( bool interactive )
	{
		auto& cfg = config::visual_settings.m_bomb;
		auto& layout = config::general_settings.m_bomb_info.layout;
		if ( !cfg.enabled || !cfg.show_info_panel )
			return;

		auto state = features::visuals::bomb( ).info_snapshot( );
		if ( !state.planted )
		{
			if ( !interactive )
				return;

			state.planted = true;
			state.time_remaining = 32.8f;
			state.timer_length = 40.0f;
			state.bomb_site = 0;
			state.predicted_damage = 124;
			state.local_health = 100;
		}

		const auto* title_wrap = app::context().overlay.fonts( ).menu_semibold_13;
		const auto* text_wrap = app::context().overlay.fonts( ).menu_semibold_13;
		const auto* icon_wrap = app::context().overlay.fonts( ).weapons_15;
		auto* title_font = title_wrap && title_wrap->im_font
			? title_wrap->im_font : ImGui::GetFont( );
		auto* text_font = text_wrap && text_wrap->im_font
			? text_wrap->im_font : ImGui::GetFont( );
		auto* icon_font = icon_wrap && icon_wrap->im_font
			? icon_wrap->im_font : ImGui::GetFont( );

		const auto dpi_scale = overlay_dpi_scale( );
		layout.scale = std::clamp( layout.scale, 0.55f, 2.0f );
		const auto user_scale = layout.scale;
		const auto metric_scale = dpi_scale * user_scale;
		const auto title_size = ( title_wrap && title_wrap->im_font
			? title_wrap->font_size : ImGui::GetFontSize( ) ) * user_scale;
		const auto text_size = ( text_wrap && text_wrap->im_font
			? text_wrap->font_size : ImGui::GetFontSize( ) ) * user_scale;
		const auto icon_size = 18.0f * user_scale;
		const ImVec2 panel_size{ 238.0f * metric_scale, 86.0f * metric_scale };

		const screen_ui_space ui_space{};
		const auto display = ui_space.display( );
		const auto position_scale = ui_space.position_scale( );
		migrate_screen_layout( layout, panel_size, display, dpi_scale,
			position_scale );
		auto position = resolve_screen_layout(
			layout, panel_size, display, dpi_scale, position_scale );
		float pos_x = position.x;
		float pos_y = position.y;

		static bool dragging = false;
		static ImVec2 drag_offset{};
		if ( interactive )
		{
			const auto mouse = ui_space.to_layout( physical_mouse_position( ) );
			const auto down = ImGui::GetIO( ).MouseDown[ 0 ];
			const auto inside = mouse.x >= pos_x && mouse.x <= pos_x + panel_size.x
				&& mouse.y >= pos_y && mouse.y <= pos_y + panel_size.y;
			if ( inside && !dragging
				&& std::abs( ImGui::GetIO( ).MouseWheel ) > 0.001f )
			{
				layout.scale = std::clamp( layout.scale
					+ ImGui::GetIO( ).MouseWheel * 0.08f, 0.55f, 2.0f );
			}
			if ( down && !dragging && inside )
			{
				dragging = true;
				drag_offset = { mouse.x - pos_x, mouse.y - pos_y };
			}
			if ( !down ) dragging = false;
			if ( dragging )
			{
				pos_x = mouse.x - drag_offset.x;
				pos_y = mouse.y - drag_offset.y;
			}
		}

		pos_x = std::clamp( pos_x, 0.0f,
			std::max( 0.0f, display.x - panel_size.x ) );
		pos_y = std::clamp( pos_y, 0.0f,
			std::max( 0.0f, display.y - panel_size.y ) );
		if ( dragging )
			capture_screen_layout( layout, { pos_x, pos_y },
				panel_size, display, dpi_scale, position_scale );

		const ImVec2 draw_min{ pos_x, pos_y };
		const ImVec2 draw_max{ pos_x + panel_size.x, pos_y + panel_size.y };
		auto* draw = ImGui::GetForegroundDrawList( );
		const auto rounding = 12.0f * metric_scale;
		const auto background = IM_COL32( cfg.panel_background.r,
			cfg.panel_background.g, cfg.panel_background.b,
			std::min<int>( cfg.panel_background.a, 150 ) );
		for ( int layer = 5; layer >= 1; --layer )
		{
			const auto offset = layer * 1.4f * metric_scale;
			const auto alpha = static_cast<int>( 28.0f
				* ( 1.0f - static_cast<float>( layer ) / 6.0f ) );
			draw->AddRectFilled( { draw_min.x + offset, draw_min.y + offset },
				{ draw_max.x + offset, draw_max.y + offset },
				IM_COL32( 0, 0, 0, alpha ), rounding );
		}
		draw->AddRectFilled( draw_min, draw_max, background, rounding );
		draw->AddRect( draw_min, draw_max, k_wm_border, rounding,
			ImDrawFlags_None, std::max( 1.0f, metric_scale ) );

		const auto bomb_color = cfg.bomb_color_t;
		const auto bomb_u32 = IM_COL32( bomb_color.r, bomb_color.g,
			bomb_color.b, bomb_color.a );
		const auto danger_amount = std::clamp(
			1.0f - state.time_remaining / 10.0f, 0.0f, 1.0f );
		const auto mix = [ danger_amount ]( std::uint8_t from, std::uint8_t to )
		{
			return static_cast<int>( from + ( to - from ) * danger_amount );
		};
		const auto timer_color = IM_COL32(
			mix( cfg.timer_text_color.r, 238 ),
			mix( cfg.timer_text_color.g, 68 ),
			mix( cfg.timer_text_color.b, 68 ), cfg.timer_text_color.a );

		draw->PushClipRect( draw_min, draw_max, true );
		const auto center = ImVec2{ draw_min.x + 43.0f * metric_scale,
			draw_min.y + panel_size.y * 0.5f };
		const auto bomb_fraction = std::clamp( state.time_remaining
			/ std::max( 1.0f, state.timer_length ), 0.0f, 1.0f );
		const auto defuse_fraction = state.being_defused
			? std::clamp( state.defuse_remaining
				/ std::max( 0.1f, state.defuse_length ), 0.0f, 1.0f ) : 0.0f;
		const auto defuse_color = state.defuse_success
			? IM_COL32( cfg.bomb_color_ct.r, cfg.bomb_color_ct.g,
				cfg.bomb_color_ct.b, cfg.bomb_color_ct.a )
			: IM_COL32( 255, 92, 62, 255 );
		const auto bomb_radius = 21.0f * metric_scale;
		const auto ring_width = std::max( 1.6f, 2.2f * metric_scale );
		constexpr auto start = -std::numbers::pi_v<float> * 0.5f;
		draw->AddCircle( center, bomb_radius, IM_COL32( 0, 0, 0, 145 ),
			48, ring_width );
		if ( bomb_fraction > 0.002f )
		{
			draw->PathArcTo( center, bomb_radius, start,
				start + 2.0f * std::numbers::pi_v<float> * bomb_fraction, 48 );
			draw->PathStroke( timer_color, 0, ring_width );
		}
		if ( state.being_defused && defuse_fraction > 0.002f )
		{
			draw->PathArcTo( center, bomb_radius, start,
				start + 2.0f * std::numbers::pi_v<float> * defuse_fraction, 48 );
			draw->PathStroke( defuse_color, 0, ring_width * 1.35f );
		}

		const auto* icon = state.being_defused ? "r" : "o";
		const auto icon_color = state.being_defused ? defuse_color : bomb_u32;
		const auto icon_extent = icon_font->CalcTextSizeA(
			icon_size, FLT_MAX, 0.0f, icon );
		draw->AddText( icon_font, icon_size,
			{ center.x - icon_extent.x * 0.5f,
			  center.y - icon_extent.y * 0.5f }, icon_color, icon );

		const auto divider_x = draw_min.x + 79.0f * metric_scale;
		draw->AddLine( { divider_x, draw_min.y + 12.0f * metric_scale },
			{ divider_x, draw_max.y - 12.0f * metric_scale }, k_wm_border,
			std::max( 1.0f, metric_scale ) );
		const auto text_x = divider_x + 14.0f * metric_scale;
		const auto line_height = 22.0f * metric_scale;
		const auto first_y = draw_min.y + 9.0f * metric_scale;
		const auto draw_row = [ & ]( float y, const char* label,
			const std::string& value, ImU32 value_color )
		{
			draw->AddText( title_font, title_size, { text_x, y },
				k_wm_muted, label );
			const auto label_width = title_font->CalcTextSizeA(
				title_size, FLT_MAX, 0.0f, label ).x;
			draw->AddText( text_font, text_size,
				{ text_x + label_width + 5.0f * metric_scale, y },
				value_color, value.c_str( ) );
		};

		const auto site_value = std::string( state.bomb_site == 1 ? "B" : "A" );
		auto hp_value = std::string( "--" );
		auto hp_color = k_wm_muted;
		if ( state.predicted_damage >= 0 && state.local_health > 0 )
		{
			if ( state.predicted_damage >= state.local_health )
			{
				hp_value = "LETHAL";
				hp_color = IM_COL32( 238, 68, 68, 255 );
			}
			else
			{
				hp_value = std::to_string( std::max( 0,
					state.local_health - state.predicted_damage ) );
				hp_color = IM_COL32( 110, 225, 135, 255 );
			}
		}
		const auto time_value = std::format( "{:.1f} sec", state.time_remaining );
		draw_row( first_y, "Site:", site_value, bomb_u32 );
		draw_row( first_y + line_height, "HP:", hp_value, hp_color );
		draw_row( first_y + line_height * 2.0f, "Time:", time_value,
			timer_color );
		draw->PopClipRect( );
	}

}

bool overlay_t::launch()
{
	write_overlay_lifecycle_event( "launch.begin" );
	const auto process_id = app::context().process.process_id( );

	this->m_window_tracking_active =
		this->m_window_tracker.initialize( process_id );
	if ( !this->m_window_tracking_active )
	{
		this->m_window_tracker.shutdown( );
		this->shutdown( );
		return false;
	}

	if (!this->register_overlay_class())
	{
		write_overlay_lifecycle_event( "launch.class_failed" );
		this->shutdown( );
		return false;
	}

	constexpr int screen_w = 1;
	constexpr int screen_h = 1;
	constexpr int screen_x = 0;
	constexpr int screen_y = 0;
	this->m_render_width = 1;
	this->m_render_height = 1;
	this->m_resize_pending = false;
	this->m_frame_latency_recovery_pending = false;
	this->m_ui_reference_width = 0;
	this->m_ui_reference_height = 0;
	this->m_ui_fullscreen_canvas = false;

	const DWORD extended_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
		| WS_EX_NOACTIVATE
		| ( k_use_composition_backend
			? WS_EX_NOREDIRECTIONBITMAP : 0 );

	this->m_hwnd = ::CreateWindowExW(
		extended_style, k_class_name, k_class_name, WS_POPUP,
		screen_x, screen_y, 1, 1,
		nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
	if (!this->m_hwnd)
	{
		this->shutdown( );
		return false;
	}
	write_overlay_lifecycle_event( "window.created", this->m_hwnd );
	overlay_t::s_active_instance.store( this, std::memory_order_release );
	if ( this->m_window_tracking_active )
	{
		this->m_window_tracker.bind_overlay( this->m_hwnd );

		window_tracker::update initial_update{};
		static_cast<void>( this->m_window_tracker.poll( initial_update ) );
	}

	constexpr MARGINS margins{ -1, -1, -1, -1 };
	::DwmExtendFrameIntoClientArea(this->m_hwnd, &margins);
	::SetLayeredWindowAttributes(this->m_hwnd, 0, 255, LWA_ALPHA);
	this->apply_capture_policy();

	if (!this->initialize_graphics())
	{
		write_overlay_lifecycle_event( "graphics.failed", this->m_hwnd );
		this->shutdown( );
		return false;
	}
	write_overlay_lifecycle_event( "backend.prepared_without_source", this->m_hwnd );

	const bool chams_renderer_ready =
		chams::g_renderer.initialize(this->m_device, this->m_context);
	this->m_chams_renderer_initialized = true;
	if (!chams_renderer_ready)
	{
		app::context().diagnostics.warning("chams renderer failed to launch -- feature will be unavailable.");
	}

	const bool chams_preview_ready =
		chams::g_preview.initialize(this->m_device, this->m_context);
	this->m_chams_preview_initialized = true;
	if (!chams_preview_ready)
	{
		app::context().diagnostics.warning("chams preview failed to launch -- the ESP editor viewport will be unavailable.");
	}

	app::context().menu.initialize(this->m_hwnd);

	this->m_input_router_active = this->m_input_router.initialize( );
	if ( !this->m_input_router_active )
	{
		app::context().diagnostics.warning(
			"overlay input router failed to launch -- falling back to polled input." );
	}
	write_overlay_backend_diagnostic(
		ui_access::enabled( ), this->m_window_tracking_active,
		this->m_window_tracker.failure_code( ),
		this->m_window_tracker.failure_stage( ),
		this->m_input_router_active,
		this->m_composition_active,
		this->m_window_tracker.target( ),
		this->m_hwnd,
		this->m_window_tracker.client( ) );

	app::context().diagnostics.info("render initialized.");
	write_overlay_lifecycle_event( "run.begin", this->m_hwnd );

	this->run();
	write_overlay_lifecycle_event( "run.end", this->m_hwnd );
	this->shutdown( );

	return true;
}

bool overlay_t::register_overlay_class()
{
	const auto instance = ::GetModuleHandleW( nullptr );
	WNDCLASSEXW existing{};
	existing.cbSize = sizeof( existing );
	if ( ::GetClassInfoExW( instance, k_class_name, &existing ) )
	{
		return true;
	}

	WNDCLASSEXW descriptor{};
	descriptor.cbSize = sizeof( descriptor );
	descriptor.style = CS_CLASSDC;
	descriptor.lpfnWndProc = &overlay_t::window_callback;
	descriptor.hInstance = instance;

	descriptor.hCursor = nullptr;
	descriptor.lpszClassName = k_class_name;

	this->m_atom = ::RegisterClassExW( &descriptor );
	return this->m_atom != 0;
}

void overlay_t::run()
{
	this->m_render_thread_id.store(
		::GetCurrentThreadId( ), std::memory_order_release );
	constexpr float clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
	MSG msg{};
	game::camera().set_presentation_horizon( 0.0f );
	HANDLE fps_timer = ::CreateWaitableTimerExW( nullptr, nullptr,
		CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
	if ( !fps_timer )
		fps_timer = ::CreateWaitableTimerW( nullptr, FALSE, nullptr );
	auto last_frame_started = std::chrono::steady_clock::now( );

	std::atomic<std::uint64_t> cache_revision{ 0 };
	auto observed_config_id = config_fingerprint();
	std::jthread cache_writer([&cache_revision](std::stop_token stop)
	{

		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
		auto observed_revision = cache_revision.load(std::memory_order_relaxed);
		auto cache_dirty_since = std::chrono::steady_clock::time_point{};
		bool cache_dirty = false;

		while (!stop.stop_requested())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			const auto current_revision = cache_revision.load(std::memory_order_relaxed);
			const auto cache_now = std::chrono::steady_clock::now();
			if (current_revision != observed_revision)
			{
				observed_revision = current_revision;
				cache_dirty = true;
				cache_dirty_since = cache_now;
			}
			if (cache_dirty && cache_now - cache_dirty_since >= std::chrono::milliseconds(700))
			{
				if (config::storage.write_cache())
					cache_dirty = false;
				else
					cache_dirty_since = cache_now;
			}
		}
	});

	std::jthread shutdown_hotkey( [ this ]( std::stop_token stop )
	{
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_BELOW_NORMAL );
		bool was_down{};
		while ( !stop.stop_requested( )
			&& !this->m_shutdown_requested.load( std::memory_order_acquire ) )
		{
			const bool is_down = ( ::GetAsyncKeyState(
				platform::windows::lifecycle_keys( ).exit ) & 0x8000 ) != 0;
			if ( is_down && !was_down )
			{
				this->request_shutdown( );
				break;
			}
			was_down = is_down;
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}
	} );

	const auto pump_messages = [ & ]
	{
		while ( ::PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
		{
			if ( msg.message == WM_QUIT )
			{
				this->set_menu_hit_testing( false );
				config::storage.write_cache( );
				this->request_shutdown( );
				return false;
			}
			::TranslateMessage( &msg );
			::DispatchMessageW( &msg );
		}
		return !this->m_shutdown_requested.load( std::memory_order_acquire );
	};

	const auto wait_for_signal = [ & ]( HANDLE signal,
		std::uint64_t observed_transition )
	{
		if ( !signal ) return true;
		const bool frame_latency_signal = this->m_composition_active
			&& signal == this->m_frame_latency_waitable;
		std::optional<platform::performance::scope> wait_profile{};
		if ( frame_latency_signal )
			wait_profile.emplace(
				platform::performance::zone::wait_frame_latency );

		const ULONGLONG frame_latency_deadline = frame_latency_signal
			? ::GetTickCount64( ) + 250u : 0u;
		const auto presentation_generation = this->m_presentation_generation;
		const auto reconcile_transition = [ & ]
		{
			const auto current_transition =
				this->m_window_tracker.transition_revision( );
			if ( current_transition == observed_transition ) return true;
			this->synchronize_window_bounds( );
			this->synchronize_content_visibility( );
			observed_transition =
				this->m_window_tracker.transition_revision( );
			return this->m_presentation_attached && this->m_overlay_visible
				&& this->m_presentation_generation == presentation_generation;
		};
		while ( true )
		{
			const auto now_tick = ::GetTickCount64( );
			const DWORD wait_timeout = frame_latency_signal
				? ( now_tick >= frame_latency_deadline ? 0u
					: static_cast<DWORD>( frame_latency_deadline - now_tick ) )
				: INFINITE;
			const auto wait_result = ::MsgWaitForMultipleObjectsEx(
				1, &signal, wait_timeout, QS_ALLINPUT,
				MWMO_INPUTAVAILABLE );
			if ( wait_result == WAIT_OBJECT_0 )
			{

				if ( !pump_messages( ) ) return false;
				return reconcile_transition( );
			}
			if ( wait_result == WAIT_OBJECT_0 + 1 )
			{
				if ( !pump_messages( ) ) return false;
				if ( !reconcile_transition( ) ) return false;
				continue;
			}
			if ( wait_result == WAIT_TIMEOUT && frame_latency_signal )
			{
				if ( !this->m_frame_latency_recovery_pending )
				{
					this->m_frame_latency_recovery_pending = true;
					write_overlay_lifecycle_event(
						"presentation.frame_latency_stalled", this->m_hwnd );
				}
				return false;
			}
			return false;
		}
	};

	bool end_was_down = false;
	while (true)
	{
		if ( this->m_shutdown_requested.load( std::memory_order_acquire ) )
			break;

		const auto end_is_down = ( ::GetAsyncKeyState(
			platform::windows::lifecycle_keys( ).exit ) & 0x8000 ) != 0;
		if (end_is_down && !end_was_down)
		{

			::ClipCursor(nullptr);
			this->set_menu_hit_testing( false );
			features::aimbot::aim().reset();
			const std::array movement_releases{
				platform::windows::input_gateway::key_transition{ VK_CONTROL, false },
				platform::windows::input_gateway::key_transition{ VK_F24, false },
			};
			app::context().input.keys(movement_releases);
			app::context().input.pointer(0, 0, platform::windows::pointer_action::primary_up | platform::windows::pointer_action::secondary_up);
			config::storage.write_cache();
			this->request_shutdown( );
			break;
		}
		end_was_down = end_is_down;

		pump_messages( );
		if ( this->m_shutdown_requested.load( std::memory_order_acquire ) )
			break;

		app::context().menu.poll_hotkey( );
		this->synchronize_window_bounds( );
		this->synchronize_content_visibility( );
		game::render_poses( ).set_presentation_state(
			this->m_presentation_attached
				&& config::visual_settings.m_player.active( ),
			this->m_window_tracker.refresh_rate( ) );
		this->synchronize_menu_focus();
		this->apply_capture_policy();
		if ( !this->m_overlay_visible )
		{
			::MsgWaitForMultipleObjectsEx(
				0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
			continue;
		}

		if (this->m_cfg_ready.exchange(false))
		{
			std::string path;
			bool is_save{ false };
			bool is_lua_import{ false };
			{
				std::lock_guard<std::mutex> lock(this->m_cfg_mutex);
				path = this->m_cfg_path;
				is_save = this->m_cfg_save;
				is_lua_import = this->m_cfg_lua_import;
			}

			if (!path.empty())
			{
				if ( is_lua_import )
					(void)scripting::runtime().import_script( std::filesystem::u8path( path ) );
				else
				{
					const auto succeeded = is_save
						? config::storage.write_to(path)
						: config::storage.read_from(path);
					if (succeeded)
						config::storage.set_active_path(path);
				}
			}
		}

		if ( this->m_content_suppressed )
		{
			::MsgWaitForMultipleObjectsEx(
				0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
			continue;
		}

		if ( config::general_settings.limit_fps
			&& config::general_settings.fps_limit > 0
			&& static_cast<std::uint32_t>( config::general_settings.fps_limit )
				< this->m_window_tracker.refresh_rate( ) )
		{
			const auto period = std::chrono::nanoseconds(
				1'000'000'000ull / static_cast<std::uint32_t>(
					config::general_settings.fps_limit ) );
			const auto deadline = last_frame_started + period;
			const auto now = std::chrono::steady_clock::now( );
			if ( now < deadline && fps_timer )
			{
				const auto remaining = std::chrono::duration_cast<
					std::chrono::nanoseconds>( deadline - now ).count( );
				LARGE_INTEGER due{};
				due.QuadPart = -std::max<LONGLONG>(
					1, static_cast<LONGLONG>( remaining / 100 ) );
				::SetWaitableTimer( fps_timer, &due, 0, nullptr, nullptr, FALSE );
				const auto transition = this->m_window_tracker.transition_revision( );
				if ( !wait_for_signal( fps_timer, transition ) ) continue;
			}
		}
		last_frame_started = std::chrono::steady_clock::now( );
		if ( this->m_frame_latency_waitable )
		{
			const auto transition = this->m_window_tracker.transition_revision( );
			if ( !wait_for_signal(
				this->m_frame_latency_waitable, transition ) ) continue;
		}
		std::optional<platform::performance::scope> render_profile{};
		render_profile.emplace( platform::performance::zone::render_frame );
		const auto render_transition =
			this->m_window_tracker.transition_revision( );
		const auto render_generation = this->m_presentation_generation;

		this->m_context->OMSetRenderTargets(1, &this->m_rtv, nullptr);
		this->m_context->ClearRenderTargetView(this->m_rtv, clear);

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrameCached(
			static_cast<float>( this->m_render_width ),
			static_cast<float>( this->m_render_height ) );
		this->route_overlay_input( );

		ImGui::NewFrame();
		zdraw::draw_list draw_list{ ImGui::GetBackgroundDrawList() };
		chams::g_renderer.begin_2d_bloom_frame( );

		const auto spectator_suppressed =
			config::visual_settings.m_player.spectator_sync
			&& game::world().local_spectated();
		if (game::local_player().valid() && !spectator_suppressed)
		{

			const auto pose_frame = game::render_poses( ).latest( );

			constexpr auto fresh_pose_camera = std::chrono::milliseconds( 100 );
			if ( pose_frame && std::chrono::steady_clock::now( )
				- pose_frame->timestamp <= fresh_pose_camera )
			{
				game::camera( ).begin_presentation_frame(
					pose_frame->camera,
					this->m_render_width, this->m_render_height );
			}
			else
			{
				game::camera().update();
			}

			{
				VESTA_PERF_SCOPE( chams );
				chams::g_renderer.render_world_effects(
					this->m_rtv, this->m_render_width, this->m_render_height );
				chams::g_renderer.render_frame(
					this->m_rtv, this->m_render_width, this->m_render_height,
					pose_frame );
			}
			{
				VESTA_PERF_SCOPE( player_esp );
				features::visuals::player().render(draw_list, pose_frame);
			}
			{
				VESTA_PERF_SCOPE( world_visuals );
				features::visuals::sound().on_render(draw_list);
				features::visuals::items().on_render(draw_list);
				features::visuals::projectiles().on_render(draw_list);
				features::visuals::bomb().on_render(draw_list);
				features::visuals::radar().on_render(draw_list);
				features::visuals::crosshair().on_render(draw_list);
				features::visuals::grenade_prediction().on_render(draw_list);
				features::misc::nade_helper().on_render(draw_list);
				features::visuals::bullet_impacts().on_render(draw_list);
				features::aimbot::aim().on_render(draw_list);
			}
		}

		scripting::runtime().render( draw_list,
			this->m_render_width, this->m_render_height );

		{
			VESTA_PERF_SCOPE( overlay_panels );
			draw_watermark(app::context().menu.is_open());
			draw_spectator_list(app::context().menu.is_open());
			if ( !spectator_suppressed )
			{
				draw_event_log(app::context().menu.is_open());
				draw_keybind_list(app::context().menu.is_open());
				draw_bomb_info(app::context().menu.is_open());
			}
		}

		{
			VESTA_PERF_SCOPE( menu );
			app::context().menu.draw();
		}
		const auto menu_cursor = static_cast<int>( ImGui::GetMouseCursor( ) );
		const auto previous_menu_cursor = overlay_t::s_menu_cursor.exchange(
			menu_cursor, std::memory_order_acq_rel );
		if ( menu_cursor != previous_menu_cursor
			&& overlay_t::s_menu_hit_testing.load( std::memory_order_acquire ) )
		{
			::PostMessageW(
				this->m_hwnd, WM_SETCURSOR,
				reinterpret_cast<WPARAM>( this->m_hwnd ),
				MAKELPARAM( HTCLIENT, WM_MOUSEMOVE ) );
		}

		std::uint64_t config_id{};
		{
			VESTA_PERF_SCOPE( config_fingerprint );
			config_id = config_fingerprint();
		}
		if (config_id != observed_config_id)
		{
			observed_config_id = config_id;
			cache_revision.fetch_add(1, std::memory_order_relaxed);
		}

		{
			VESTA_PERF_SCOPE( imgui_render );
			ImGui::EndFrame();
			ImGui::Render();
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		if ( !app::context().menu.is_open( ) )
		{
			VESTA_PERF_SCOPE( bloom_2d );
			chams::g_renderer.render_2d_bloom(
				this->m_rtv, this->m_render_width, this->m_render_height );
		}

		render_profile.reset( );

		if ( !pump_messages( ) ) break;
		if ( this->m_window_tracker.transition_revision( ) != render_transition )
		{
			this->synchronize_window_bounds( );
			this->synchronize_content_visibility( );
			if ( !this->m_presentation_attached || !this->m_overlay_visible
				|| this->m_presentation_generation != render_generation )
			{
				continue;
			}
		}
		const auto frame_transition =
			this->m_window_tracker.transition_revision( );
		HRESULT present_result{};
		bool cancel_pending_frame{};
		while ( true )
		{
			VESTA_PERF_SCOPE( present );
			const UINT present_flags = ( this->m_present_tearing_enabled
				? DXGI_PRESENT_ALLOW_TEARING : 0 )
				| DXGI_PRESENT_DO_NOT_WAIT;
			present_result = this->m_swap_chain->Present( 0, present_flags );
			if ( present_result == DXGI_ERROR_WAS_STILL_DRAWING )
			{

				if ( !wait_for_signal(
					this->m_frame_latency_waitable, frame_transition )
					|| !this->m_presentation_attached )
				{
					cancel_pending_frame = true;
					break;
				}
				continue;
			}
			if ( FAILED( present_result ) && this->m_present_tearing_enabled )
			{
				this->m_present_tearing_enabled = false;
				continue;
			}
			break;
		}
		if ( cancel_pending_frame ) continue;
		if ( FAILED( present_result ) )
		{
			break;
		}
		platform::performance::flush_if_due( );
	}

	if ( fps_timer ) ::CloseHandle( fps_timer );
	this->m_render_thread_id.store( 0, std::memory_order_release );
	this->set_menu_hit_testing( false );
	platform::performance::flush_if_due( true );
	config::storage.write_cache();
}

void overlay_t::shutdown( ) noexcept
{
	if ( this->m_shutdown_active.exchange( true, std::memory_order_acq_rel ) )
		return;

	write_overlay_lifecycle_event( "shutdown.begin", this->m_hwnd );
	this->stop_config_dialog( );
	this->set_menu_hit_testing( false );
	if ( this->m_input_router_active )
	{
		this->m_input_router.shutdown( );
		this->m_input_router_active = false;
	}
	if ( this->m_hwnd )
	{
		::SetWindowDisplayAffinity( this->m_hwnd, WDA_NONE );
		::ShowWindow( this->m_hwnd, SW_HIDE );
	}
	this->m_overlay_visible = false;
	this->detach_presentation( );
	this->m_window_tracker.shutdown( );
	this->m_window_tracking_active = false;
	write_overlay_lifecycle_event( "window.hidden_hooks_removed", this->m_hwnd );

	if ( this->m_context )
	{
		this->m_context->OMSetRenderTargets( 0, nullptr, nullptr );
		this->m_context->ClearState( );
	}
	const HRESULT gpu_idle_result = wait_for_gpu_idle(
		this->m_device, this->m_context );
	write_overlay_lifecycle_event(
		"graphics.idle", this->m_hwnd, gpu_idle_result );

	if ( this->m_chams_preview_initialized )
	{
		chams::g_preview.shutdown( );
		this->m_chams_preview_initialized = false;
	}
	if ( this->m_chams_renderer_initialized )
	{
		chams::g_renderer.shutdown( );
		this->m_chams_renderer_initialized = false;
	}
	if ( this->m_imgui_dx11_active )
	{
		ImGui_ImplDX11_Shutdown( );
		this->m_imgui_dx11_active = false;
	}
	if ( this->m_imgui_win32_active )
	{
		ImGui_ImplWin32_Shutdown( );
		this->m_imgui_win32_active = false;
	}
	if ( this->m_imgui_context_active )
	{
		this->m_fonts = {};
		zdraw::shutdown_fonts( );
		ImGui::DestroyContext( );
		this->m_imgui_context_active = false;
	}

	const auto release = []( auto*& object )
	{
		if ( object )
		{
			object->Release( );
			object = nullptr;
		}
	};
	release( this->m_ct_preview_texture );
	for ( auto& [ steamid, avatar ] : this->m_steam_avatars ) release( avatar );
	this->m_steam_avatars.clear();
	this->m_missing_steam_avatars.clear();
	release( this->m_dsv );
	release( this->m_rtv );
	release( this->m_back_buffer );
	if ( this->m_frame_latency_waitable )
	{
		::CloseHandle( this->m_frame_latency_waitable );
		this->m_frame_latency_waitable = nullptr;
	}
	release( this->m_swap_chain );
	release( this->m_composition_visual );
	release( this->m_composition_target );
	release( this->m_composition_device );
	release( this->m_context );
	release( this->m_device );
	this->m_composition_active = false;
	this->m_presentation_attached = false;
	write_overlay_lifecycle_event( "graphics.released", this->m_hwnd );

	const HWND destroyed_window = this->m_hwnd;
	overlay_t* expected = this;
	overlay_t::s_active_instance.compare_exchange_strong(
		expected, nullptr, std::memory_order_acq_rel );
	if ( this->m_hwnd )
	{
		::DestroyWindow( this->m_hwnd );
		this->m_hwnd = nullptr;
	}
	if ( this->m_atom )
	{
		::UnregisterClassW( k_class_name, ::GetModuleHandleW( nullptr ) );
		this->m_atom = 0;
	}
	write_overlay_lifecycle_event( "window.destroyed", destroyed_window );
	write_overlay_lifecycle_event( "shutdown.complete", nullptr );
}

void overlay_t::apply_capture_policy()
{
	const auto enabled = config::general_settings.obs_bypass;
	if (this->m_obs_bypass_initialized && this->m_obs_bypass_observed == enabled)
	{
		return;
	}

	const auto affinity = enabled ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
	::SetWindowDisplayAffinity(this->m_hwnd, affinity);
	this->m_obs_bypass_observed = enabled;
	this->m_obs_bypass_initialized = true;
}

void overlay_t::synchronize_window_bounds( )
{
	if ( !this->m_window_tracking_active )
		return;

	window_tracker::update update{};
	const bool tracker_changed = this->m_window_tracker.poll( update );
	if ( !tracker_changed && !this->m_resize_pending
		&& !this->m_frame_latency_recovery_pending
		&& this->m_presentation_attached == this->m_window_tracker.visible( ) )
		return;

	const bool target_ready = this->m_window_tracker.visible( );

	if ( !target_ready )
	{
		this->detach_presentation( );
	}
	if ( !target_ready )
	{
		this->m_resize_pending = true;
		return;
	}

	const auto width = this->m_window_tracker.width( );
	const auto height = this->m_window_tracker.height( );
	if ( width < 64 || height < 64 )
	{
		this->m_resize_pending = true;
		return;
	}
	this->m_ui_fullscreen_canvas = this->m_window_tracker.covers_monitor( );
	if ( this->m_ui_fullscreen_canvas
		&& ( this->m_ui_reference_width == 0
			|| this->m_ui_reference_height == 0 ) )
	{

		auto reference_width = width;
		auto reference_height = height;
		const auto monitor = ::MonitorFromWindow(
			this->m_window_tracker.target( ), MONITOR_DEFAULTTONULL );
		MONITORINFOEXW information{};
		information.cbSize = sizeof( information );
		DEVMODEW desktop_mode{};
		desktop_mode.dmSize = sizeof( desktop_mode );
		if ( monitor && ::GetMonitorInfoW( monitor, &information )
			&& ::EnumDisplaySettingsW( information.szDevice,
				ENUM_REGISTRY_SETTINGS, &desktop_mode )
			&& desktop_mode.dmPelsWidth > 0 && desktop_mode.dmPelsHeight > 0 )
		{
			reference_width = desktop_mode.dmPelsWidth;
			reference_height = desktop_mode.dmPelsHeight;
		}
		this->m_ui_reference_width = reference_width;
		this->m_ui_reference_height = reference_height;
	}

	if ( !this->m_presentation_attached
		|| width != this->m_render_width || height != this->m_render_height
		|| this->m_frame_latency_recovery_pending )
	{
		this->detach_presentation( );
		const bool dimensions_changed = width != this->m_render_width
			|| height != this->m_render_height;
		const bool recover_frame_latency =
			this->m_frame_latency_recovery_pending;
		if ( dimensions_changed || recover_frame_latency )
		{
			if ( this->m_composition_active )
			{
				if ( !this->resize_graphics( width, height ) )
				{
					this->m_resize_pending = true;
					return;
				}
				this->m_frame_latency_recovery_pending = false;
			}
			else
			{

				this->m_render_width = width;
				this->m_render_height = height;
			}
		}
		if ( !this->attach_presentation( ) )
		{
			this->m_resize_pending = true;
			return;
		}
		this->m_resize_pending = false;
	}

	if ( this->m_presentation_attached
		&& ( update.geometry_changed || update.z_order_changed
			|| update.visibility_changed ) )
	{
		this->m_window_tracker.place_overlay( );
	}
}

void overlay_t::synchronize_content_visibility( )
{
	const bool content_requested = app::context().menu.is_open( )
		|| configured_overlay_content( );
	const bool suppressed = !content_requested;
	const bool target_visible = this->m_window_tracking_active
		&& this->m_window_tracker.visible( );
	this->m_combat_input_ready.store(
		target_visible && this->m_presentation_attached,
		std::memory_order_release );
	const bool should_show = content_requested && target_visible
		&& this->m_presentation_attached;

	if ( suppressed == this->m_content_suppressed
		&& should_show == this->m_overlay_visible )
	{
		return;
	}

	this->m_content_suppressed = suppressed;
	if ( should_show )
	{
		if ( this->m_window_tracking_active )
			this->m_window_tracker.place_overlay( );
		::ShowWindow( this->m_hwnd, SW_SHOWNOACTIVATE );
		write_overlay_lifecycle_event( "content.shown", this->m_hwnd );
	}
	else
	{
		::ShowWindow( this->m_hwnd, SW_HIDE );
		write_overlay_lifecycle_event( "content.hidden", this->m_hwnd );
	}
	this->m_overlay_visible = should_show;
}

void overlay_t::route_overlay_input( )
{
	if ( this->m_input_router_active
		&& this->m_input_router.capture_ready( ) )
	{
		const auto origin = this->m_window_tracking_active
			? this->m_window_tracker.client( )
			: RECT{};
		const auto menu_open = app::context().menu.is_open( );

		this->m_input_router.pump_imgui(
			origin.left, origin.top, !menu_open );
		if ( menu_open )
		{
			POINT cursor{};
			if ( ::GetCursorPos( &cursor )
				&& ::ScreenToClient( this->m_hwnd, &cursor ) )
			{
				auto x = static_cast<float>( cursor.x );
				auto y = static_cast<float>( cursor.y );
				const auto display = ImGui::GetIO( ).DisplaySize;
				app::context().menu.map_pointer_to_layout(
					x, y, display.x, display.y );
				ImGui::GetIO( ).AddMousePosEvent( x, y );
			}
		}
		return;
	}

	auto& io = ImGui::GetIO( );
	io.MouseDrawCursor = false;
	if ( !app::context().menu.is_open( ) )
	{
		return;
	}

	POINT cursor{};
	if ( !::GetCursorPos( &cursor ) )
	{
		return;
	}

	const HWND hit = ::WindowFromPoint( cursor );
	const HWND hit_root = hit ? ::GetAncestor( hit, GA_ROOT ) : nullptr;
	const HWND target = this->m_window_tracker.target( );
	if ( hit_root != target && hit_root != this->m_hwnd )
	{
		io.AddMouseButtonEvent( 0, false );
		io.AddMouseButtonEvent( 1, false );
		io.AddMouseButtonEvent( 2, false );
		return;
	}

	if ( ::ScreenToClient( this->m_hwnd, &cursor ) )
	{
		auto x = static_cast<float>( cursor.x );
		auto y = static_cast<float>( cursor.y );
		app::context().menu.map_pointer_to_layout(
			x, y, io.DisplaySize.x, io.DisplaySize.y );
		io.AddMousePosEvent( x, y );
	}
	io.AddMouseButtonEvent( 0, ( ::GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0 );
	io.AddMouseButtonEvent( 1, ( ::GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0 );
	io.AddMouseButtonEvent( 2, ( ::GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Ctrl, ( ::GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Shift, ( ::GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Alt, ( ::GetAsyncKeyState( VK_MENU ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Super,
		( ::GetAsyncKeyState( VK_LWIN ) & 0x8000 ) != 0
		|| ( ::GetAsyncKeyState( VK_RWIN ) & 0x8000 ) != 0 );

	for ( int key = 1; key < 256; ++key )
	{
		const auto imgui_key = overlay_input::key_from_virtual_key( key );
		if ( imgui_key == ImGuiKey_None )
			continue;
		const auto down = ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0;
		const auto was_down = this->m_overlay_key_states[ key ];
		if ( down != was_down )
		{
			io.AddKeyEvent( imgui_key, down );
			this->m_overlay_key_states[ key ] = down;
		}

		if ( down && !was_down
			&& ( ( key >= '0' && key <= '9' ) || ( key >= 'A' && key <= 'Z' )
				|| key == VK_SPACE || ( key >= VK_OEM_1 && key <= VK_OEM_3 )
				|| ( key >= VK_OEM_4 && key <= VK_OEM_7 )
				|| key == VK_OEM_PLUS || key == VK_OEM_COMMA
				|| key == VK_OEM_MINUS || key == VK_OEM_PERIOD ) )
		{
			BYTE keyboard_state[ 256 ]{};
			if ( ::GetKeyboardState( keyboard_state ) )
			{
				wchar_t characters[ 8 ]{};
				const auto scan_code = ::MapVirtualKeyW( key, MAPVK_VK_TO_VSC );
				const auto count = ::ToUnicodeEx(
					key, scan_code, keyboard_state, characters,
					static_cast<int>( std::size( characters ) ), 0, ::GetKeyboardLayout( 0 ) );
				for ( auto index = 0; index < count; ++index )
				{
					io.AddInputCharacterUTF16( characters[ index ] );
				}
			}
		}
	}
}

void overlay_t::synchronize_menu_focus()
{
	const auto menu_open = app::context().menu.is_open();
	const auto interactive = menu_open && this->m_overlay_visible;
	const auto target = this->m_window_tracking_active
		? this->m_window_tracker.target( ) : nullptr;
	const HWND foreground = ::GetForegroundWindow( );
	const HWND foreground_root = foreground
		? ::GetAncestor( foreground, GA_ROOT ) : nullptr;
	DWORD target_process{};
	DWORD foreground_process{};
	if ( target )
		::GetWindowThreadProcessId( target, &target_process );
	if ( foreground_root )
		::GetWindowThreadProcessId( foreground_root, &foreground_process );
	const bool target_is_active = target_process != 0
		&& target_process == foreground_process;
	const bool capture = interactive && target_is_active
		&& !overlay_t::s_modal_active.load( std::memory_order_relaxed );
	const bool interaction_started = interactive && !this->m_menu_interactive;
	this->m_menu_interactive = interactive;
	const bool escape_is_down = ( ::GetAsyncKeyState( VK_ESCAPE ) & 0x8000 ) != 0;
	const bool physical_escape_pressed = escape_is_down && !this->m_escape_was_down;
	this->m_escape_was_down = escape_is_down;

	const auto tap_escape = []( )
	{

		static_cast<void>( app::context().input.key( VK_ESCAPE, false ) );
		const std::array escape_tap{
			platform::windows::input_gateway::key_transition{ VK_ESCAPE, true },
			platform::windows::input_gateway::key_transition{ VK_ESCAPE, false },
		};
		const auto sent = app::context().input.keys( escape_tap );
		static_cast<void>( app::context().input.key( VK_ESCAPE, false ) );
		return sent;
	};
	CURSORINFO cursor{};
	cursor.cbSize = sizeof( cursor );
	const bool game_cursor_visible = ::GetCursorInfo( &cursor )
		&& ( cursor.flags & CURSOR_SHOWING ) != 0;
	if ( interaction_started && target_is_active && !game_cursor_visible )
	{

		this->m_game_menu_opened_by_overlay = tap_escape( );
	}
	if ( menu_open && this->m_game_menu_opened_by_overlay
		&& physical_escape_pressed )
	{

		this->m_game_menu_opened_by_overlay = false;
	}
	if ( !menu_open && this->m_game_menu_opened_by_overlay && target_is_active )
	{

		static_cast<void>( tap_escape( ) );
		this->m_game_menu_opened_by_overlay = false;
	}

	this->set_menu_hit_testing( false );
	if ( this->m_input_router_active )
	{
		this->m_input_router.set_capture(
			capture, capture ? foreground_root : nullptr );
	}
}

void overlay_t::set_menu_hit_testing( const bool enabled )
{
	if ( this->m_menu_hit_testing == enabled || !this->m_hwnd )
		return;

	const auto style = ::GetWindowLongPtrW( this->m_hwnd, GWL_EXSTYLE );
	const auto no_activate_style = style | static_cast<LONG_PTR>( WS_EX_NOACTIVATE );
	const auto desired = enabled
		? no_activate_style & ~static_cast<LONG_PTR>( WS_EX_TRANSPARENT )
		: no_activate_style | static_cast<LONG_PTR>( WS_EX_TRANSPARENT );
	if ( enabled )
		overlay_t::s_menu_hit_testing.store( true, std::memory_order_release );

	bool applied = true;
	if ( desired != style )
	{
		::SetLastError( ERROR_SUCCESS );
		const auto previous = ::SetWindowLongPtrW(
			this->m_hwnd, GWL_EXSTYLE, desired );
		applied = previous != 0 || ::GetLastError( ) == ERROR_SUCCESS;
	}

	if ( !enabled || !applied )
		overlay_t::s_menu_hit_testing.store( false, std::memory_order_release );
	this->m_menu_hit_testing = enabled && applied;
	if ( this->m_menu_hit_testing )
	{
		::PostMessageW(
			this->m_hwnd, WM_SETCURSOR,
			reinterpret_cast<WPARAM>( this->m_hwnd ),
			MAKELPARAM( HTCLIENT, WM_MOUSEMOVE ) );
	}
}

UINT_PTR CALLBACK overlay_t::config_dialog_hook(
	const HWND hwnd, const UINT msg, const WPARAM, const LPARAM lp )
{
	auto* self = reinterpret_cast<overlay_t*>(
		::GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
	if ( msg == WM_INITDIALOG )
	{
		const auto* ofn = reinterpret_cast<const OPENFILENAMEW*>( lp );
		self = ofn ? reinterpret_cast<overlay_t*>( ofn->lCustData ) : nullptr;
		::SetWindowLongPtrW( hwnd, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>( self ) );
		if ( self )
		{
			const auto dialog = ::GetParent( hwnd )
				? ::GetParent( hwnd ) : hwnd;
			self->m_cfg_dialog_hwnd.store(
				dialog, std::memory_order_release );
			if ( self->m_cfg_dialog_cancel.load( std::memory_order_acquire ) )
				::PostMessageW( dialog, WM_CLOSE, 0, 0 );
		}
	}
	else if ( msg == WM_NCDESTROY && self )
	{
		const auto dialog = ::GetParent( hwnd )
			? ::GetParent( hwnd ) : hwnd;
		auto expected = dialog;
		self->m_cfg_dialog_hwnd.compare_exchange_strong(
			expected, nullptr, std::memory_order_acq_rel );
	}
	return 0;
}

void overlay_t::stop_config_dialog( ) noexcept
{
	this->m_cfg_dialog_cancel.store( true, std::memory_order_release );
	if ( this->m_cfg_dialog_thread.joinable( ) )
	{
		this->m_cfg_dialog_thread.request_stop( );
		if ( const auto dialog = this->m_cfg_dialog_hwnd.load(
			std::memory_order_acquire ) )
		{
			::PostMessageW( dialog, WM_CLOSE, 0, 0 );
		}
		if ( const auto thread_id = this->m_cfg_dialog_thread_id.load(
			std::memory_order_acquire ) )
		{
			::EnumThreadWindows( thread_id,
				[]( const HWND window, LPARAM ) -> BOOL
				{
					::PostMessageW( window, WM_CLOSE, 0, 0 );
					return TRUE;
				}, 0 );
		}
		this->m_cfg_dialog_thread.join( );
	}
	this->m_cfg_dialog_hwnd.store( nullptr, std::memory_order_release );
	this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
	overlay_t::s_modal_active.store( false, std::memory_order_release );
}

void overlay_t::request_config_load()
{

	if (overlay_t::s_modal_active.exchange(true))
		return;
	if ( this->m_cfg_dialog_thread.joinable( ) )
		this->m_cfg_dialog_thread.join( );
	this->m_cfg_dialog_cancel.store( false, std::memory_order_release );

	this->m_cfg_dialog_thread = std::jthread([this]( const std::stop_token stop )
		{
			this->m_cfg_dialog_thread_id.store(
				::GetCurrentThreadId( ), std::memory_order_release );
			wchar_t path[MAX_PATH]{};
			if ( stop.stop_requested( ) )
			{
				this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
				overlay_t::s_modal_active.store( false, std::memory_order_release );
				return;
			}

			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = this->m_hwnd;
			ofn.lpstrFilter = L"vesta config (*.cfg)\0*.cfg\0All files (*.*)\0*.*\0";
			ofn.lpstrFile = path;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrTitle = L"Load vesta config";
			ofn.lCustData = reinterpret_cast<LPARAM>( this );
			ofn.lpfnHook = &overlay_t::config_dialog_hook;
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
				| OFN_EXPLORER | OFN_ENABLEHOOK;

			if (::GetOpenFileNameW(&ofn) && !stop.stop_requested( )
				&& !this->m_cfg_dialog_cancel.load( std::memory_order_acquire ))
			{
				if ( auto utf8 = utf8_from_wide( path ) )
				{
					std::lock_guard<std::mutex> lock(this->m_cfg_mutex);
					this->m_cfg_path = std::move(*utf8);
					this->m_cfg_save = false;
					this->m_cfg_lua_import = false;
					this->m_cfg_ready.store(true, std::memory_order_release);
				}
				else
				{
					::OutputDebugStringW( L"vesta: invalid UTF-16 config path\n" );
				}
			}

			this->m_cfg_dialog_hwnd.store( nullptr, std::memory_order_release );
			this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
			overlay_t::s_modal_active.store( false, std::memory_order_release );
		});
}

void overlay_t::request_lua_import()
{
	if ( overlay_t::s_modal_active.exchange( true ) ) return;
	if ( this->m_cfg_dialog_thread.joinable( ) ) this->m_cfg_dialog_thread.join( );
	this->m_cfg_dialog_cancel.store( false, std::memory_order_release );

	this->m_cfg_dialog_thread = std::jthread( [ this ]( const std::stop_token stop )
	{
		this->m_cfg_dialog_thread_id.store(
			::GetCurrentThreadId( ), std::memory_order_release );
		wchar_t path[ 32768 ]{};
		if ( stop.stop_requested( ) )
		{
			this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
			overlay_t::s_modal_active.store( false, std::memory_order_release );
			return;
		}

		OPENFILENAMEW ofn{};
		ofn.lStructSize = sizeof( ofn );
		ofn.hwndOwner = this->m_hwnd;
		ofn.lpstrFilter = L"Lua scripts (*.lua)\0*.lua\0";
		ofn.lpstrFile = path;
		ofn.nMaxFile = static_cast<DWORD>( std::size( path ) );
		ofn.lpstrTitle = L"Import Lua script into Vesta";
		ofn.lpstrDefExt = L"lua";
		ofn.lCustData = reinterpret_cast<LPARAM>( this );
		ofn.lpfnHook = &overlay_t::config_dialog_hook;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
			| OFN_EXPLORER | OFN_ENABLEHOOK | OFN_DONTADDTORECENT;

		if ( ::GetOpenFileNameW( &ofn ) && !stop.stop_requested( )
			&& !this->m_cfg_dialog_cancel.load( std::memory_order_acquire ) )
		{
			if ( auto utf8 = utf8_from_wide( path ) )
			{
				std::lock_guard<std::mutex> lock( this->m_cfg_mutex );
				this->m_cfg_path = std::move( *utf8 );
				this->m_cfg_save = false;
				this->m_cfg_lua_import = true;
				this->m_cfg_ready.store( true, std::memory_order_release );
			}
		}

		this->m_cfg_dialog_hwnd.store( nullptr, std::memory_order_release );
		this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
		overlay_t::s_modal_active.store( false, std::memory_order_release );
	} );
}

void overlay_t::request_config_save()
{
	if (overlay_t::s_modal_active.exchange(true))
		return;
	if ( this->m_cfg_dialog_thread.joinable( ) )
		this->m_cfg_dialog_thread.join( );
	this->m_cfg_dialog_cancel.store( false, std::memory_order_release );

	const auto initial = wide_from_utf8( config::storage.name_buffer )
		.value_or( std::wstring{} );
	this->m_cfg_dialog_thread = std::jthread(
		[this, initial]( const std::stop_token stop )
		{
			this->m_cfg_dialog_thread_id.store(
				::GetCurrentThreadId( ), std::memory_order_release );
			wchar_t path[MAX_PATH]{};
			if (!initial.empty())
				::wcsncpy_s(path, initial.c_str(), _TRUNCATE);
			if ( stop.stop_requested( ) )
			{
				this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
				overlay_t::s_modal_active.store( false, std::memory_order_release );
				return;
			}

			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = this->m_hwnd;
			ofn.lpstrFilter = L"vesta config (*.cfg)\0*.cfg\0All files (*.*)\0*.*\0";
			ofn.lpstrFile = path;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrTitle = L"Save vesta config";
			ofn.lpstrDefExt = L"cfg";
			ofn.lCustData = reinterpret_cast<LPARAM>( this );
			ofn.lpfnHook = &overlay_t::config_dialog_hook;
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR
				| OFN_EXPLORER | OFN_ENABLEHOOK;

			if (::GetSaveFileNameW(&ofn) && !stop.stop_requested( )
				&& !this->m_cfg_dialog_cancel.load( std::memory_order_acquire ))
			{
				if ( auto utf8 = utf8_from_wide( path ) )
				{
					std::lock_guard<std::mutex> lock(this->m_cfg_mutex);
					this->m_cfg_path = std::move(*utf8);
					this->m_cfg_save = true;
					this->m_cfg_lua_import = false;
					this->m_cfg_ready.store(true, std::memory_order_release);
				}
				else
				{
					::OutputDebugStringW( L"vesta: invalid UTF-16 config path\n" );
				}
			}

			this->m_cfg_dialog_hwnd.store( nullptr, std::memory_order_release );
			this->m_cfg_dialog_thread_id.store( 0, std::memory_order_release );
			overlay_t::s_modal_active.store( false, std::memory_order_release );
		});
}

bool overlay_t::open_composition_backend( )
{
	using Microsoft::WRL::ComPtr;
	const UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED
		| D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL selected{};
	ComPtr<ID3D11Device> device{};
	ComPtr<ID3D11DeviceContext> context{};
	if ( FAILED( ::D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
		levels, static_cast<UINT>( std::size( levels ) ), D3D11_SDK_VERSION,
		&device, &selected, &context ) ) )
	{
		return false;
	}

	ComPtr<IDXGIDevice> dxgi_device{};
	ComPtr<IDXGIAdapter> adapter{};
	ComPtr<IDXGIFactory2> factory{};
	if ( FAILED( device.As( &dxgi_device ) )
		|| FAILED( dxgi_device->GetAdapter( &adapter ) )
		|| FAILED( adapter->GetParent( IID_PPV_ARGS( &factory ) ) ) )
	{
		return false;
	}
	this->m_allow_tearing = false;
	ComPtr<IDXGIFactory5> factory5{};
	BOOL tearing_supported = FALSE;
	if ( SUCCEEDED( factory.As( &factory5 ) )
		&& SUCCEEDED( factory5->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&tearing_supported, sizeof( tearing_supported ) ) ) )
	{
		this->m_allow_tearing = tearing_supported == TRUE;
	}
	this->m_present_tearing_enabled = this->m_allow_tearing;

	ComPtr<IDCompositionDevice> composition_device{};
	if ( FAILED( ::DCompositionCreateDevice(
		dxgi_device.Get( ), IID_PPV_ARGS( &composition_device ) ) ) )
	{
		return false;
	}

	this->m_device = device.Detach( );
	this->m_context = context.Detach( );
	this->m_composition_device = composition_device.Detach( );
	return true;
}

bool overlay_t::open_composition_swap_chain( )
{
	if ( this->m_swap_chain )
		return true;
	if ( !this->m_device || !this->m_context
		|| this->m_render_width < 64 || this->m_render_height < 64 )
	{
		return false;
	}

	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIDevice> dxgi_device{};
	ComPtr<IDXGIAdapter> adapter{};
	ComPtr<IDXGIFactory2> factory{};
	if ( FAILED( this->m_device->QueryInterface( IID_PPV_ARGS( &dxgi_device ) ) )
		|| FAILED( dxgi_device->GetAdapter( &adapter ) )
		|| FAILED( adapter->GetParent( IID_PPV_ARGS( &factory ) ) ) )
	{
		return false;
	}

	DXGI_SWAP_CHAIN_DESC1 description{};
	description.Width = this->m_render_width;
	description.Height = this->m_render_height;
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.SampleDesc = { 1, 0 };
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.BufferCount = 2;
	description.Scaling = DXGI_SCALING_STRETCH;
	description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
	description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
		| ( this->m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u );

	ComPtr<IDXGISwapChain1> swap_chain1{};
	ComPtr<IDXGISwapChain2> swap_chain2{};
	ComPtr<IDXGISwapChain> swap_chain{};
	ComPtr<ID3D11Texture2D> back_buffer{};
	ComPtr<ID3D11RenderTargetView> render_target{};
	if ( FAILED( factory->CreateSwapChainForComposition(
		this->m_device, &description, nullptr, &swap_chain1 ) )
		|| FAILED( swap_chain1.As( &swap_chain2 ) )
		|| FAILED( swap_chain2->SetMaximumFrameLatency( 1 ) )
		|| FAILED( swap_chain1.As( &swap_chain ) )
		|| FAILED( swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) ) )
		|| FAILED( this->m_device->CreateRenderTargetView(
			back_buffer.Get( ), nullptr, &render_target ) ) )
	{
		return false;
	}

	const auto waitable = swap_chain2->GetFrameLatencyWaitableObject( );
	if ( !waitable )
		return false;

	this->m_swap_chain = swap_chain.Detach( );
	this->m_back_buffer = back_buffer.Detach( );
	this->m_rtv = render_target.Detach( );
	this->m_frame_latency_waitable = waitable;
	this->m_present_tearing_enabled = this->m_allow_tearing;

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>( this->m_render_width );
	viewport.Height = static_cast<float>( this->m_render_height );
	viewport.MaxDepth = 1.0f;
	this->m_context->RSSetViewports( 1, &viewport );
	write_overlay_lifecycle_event( "presentation.source_created", this->m_hwnd );
	return true;
}

void overlay_t::close_composition_swap_chain( ) noexcept
{
	if ( !this->m_composition_active )
		return;
	if ( this->m_context )
		this->m_context->OMSetRenderTargets( 0, nullptr, nullptr );
	if ( this->m_rtv )
	{
		this->m_rtv->Release( );
		this->m_rtv = nullptr;
	}
	if ( this->m_back_buffer )
	{
		this->m_back_buffer->Release( );
		this->m_back_buffer = nullptr;
	}
	if ( this->m_frame_latency_waitable )
	{
		::CloseHandle( this->m_frame_latency_waitable );
		this->m_frame_latency_waitable = nullptr;
	}
	if ( this->m_swap_chain )
	{
		this->m_swap_chain->Release( );
		this->m_swap_chain = nullptr;
		write_overlay_lifecycle_event( "presentation.source_destroyed", this->m_hwnd );
	}
}

bool overlay_t::attach_presentation( )
{
	if ( this->m_presentation_attached )
		return true;
	if ( !this->m_hwnd || !this->m_window_tracker.visible( ) )
	{
		return false;
	}
	if ( !this->m_composition_active )
	{

		const auto style = ::GetWindowLongPtrW( this->m_hwnd, GWL_EXSTYLE );
		::SetWindowLongPtrW( this->m_hwnd, GWL_EXSTYLE,
			style & ~static_cast<LONG_PTR>( WS_EX_NOREDIRECTIONBITMAP ) );
		this->m_window_tracker.place_overlay( );
		if ( !this->open_swap_chain_backend( ) )
		{
			::SetWindowLongPtrW( this->m_hwnd, GWL_EXSTYLE,
				style | static_cast<LONG_PTR>( WS_EX_NOREDIRECTIONBITMAP ) );
			return false;
		}
		this->m_presentation_attached = true;
		++this->m_presentation_generation;
		const bool should_show = !this->m_content_suppressed;
		if ( should_show ) ::ShowWindow( this->m_hwnd, SW_SHOWNOACTIVATE );
		this->m_overlay_visible = should_show;
		write_overlay_lifecycle_event(
			"presentation.hwnd_attached", this->m_hwnd );
		return true;
	}
	if ( !this->m_composition_device || !this->open_composition_swap_chain( ) )
		return false;

	IDCompositionTarget* target{};
	IDCompositionVisual* visual{};
	HRESULT result = this->m_composition_device->CreateVisual( &visual );
	if ( SUCCEEDED( result ) )
		result = this->m_composition_device->CreateTargetForHwnd(
		this->m_hwnd, TRUE, &target );
	if ( SUCCEEDED( result ) )
		result = visual->SetContent( this->m_swap_chain );
	if ( SUCCEEDED( result ) )
		result = target->SetRoot( visual );
	if ( SUCCEEDED( result ) )
		result = this->m_composition_device->Commit( );
	if ( FAILED( result ) )
	{
		if ( target ) target->SetRoot( nullptr );
		if ( visual ) visual->SetContent( nullptr );
		if ( this->m_composition_device )
		{
			this->m_composition_device->Commit( );
		}
		if ( target ) target->Release( );
		if ( visual ) visual->Release( );
		this->close_composition_swap_chain( );
		write_overlay_lifecycle_event( "presentation.attach_failed", this->m_hwnd, result );
		return false;
	}

	this->m_composition_target = target;
	this->m_composition_visual = visual;
	this->m_presentation_attached = true;
	++this->m_presentation_generation;
	this->m_window_tracker.place_overlay( );
	const bool should_show = !this->m_content_suppressed;
	if ( should_show )
		::ShowWindow( this->m_hwnd, SW_SHOWNOACTIVATE );
	this->m_overlay_visible = should_show;
	write_overlay_lifecycle_event( "presentation.attached", this->m_hwnd, result );
	return true;
}

void overlay_t::detach_presentation( ) noexcept
{
	this->m_combat_input_ready.store( false, std::memory_order_release );
	game::render_poses( ).set_presentation_state(
		false, this->m_window_tracker.refresh_rate( ) );
	if ( !this->m_presentation_attached && !this->m_composition_target )
		return;
	++this->m_presentation_generation;
	if ( this->m_hwnd )
		::ShowWindow( this->m_hwnd, SW_HIDE );
	this->m_overlay_visible = false;
	this->m_presentation_attached = false;
	if ( !this->m_composition_active )
	{
		if ( this->m_context )
			this->m_context->OMSetRenderTargets( 0, nullptr, nullptr );
		if ( this->m_rtv )
		{
			this->m_rtv->Release( );
			this->m_rtv = nullptr;
		}
		if ( this->m_back_buffer )
		{
			this->m_back_buffer->Release( );
			this->m_back_buffer = nullptr;
		}
		if ( this->m_frame_latency_waitable )
		{
			::CloseHandle( this->m_frame_latency_waitable );
			this->m_frame_latency_waitable = nullptr;
		}
		if ( this->m_swap_chain )
		{
			this->m_swap_chain->Release( );
			this->m_swap_chain = nullptr;
		}
		if ( this->m_hwnd )
		{
			const auto style = ::GetWindowLongPtrW(
				this->m_hwnd, GWL_EXSTYLE );
			::SetWindowLongPtrW( this->m_hwnd, GWL_EXSTYLE,
				style | static_cast<LONG_PTR>( WS_EX_NOREDIRECTIONBITMAP ) );
		}
		write_overlay_lifecycle_event(
			"presentation.hwnd_detached", this->m_hwnd );
		return;
	}

	HRESULT result = S_OK;
	if ( this->m_composition_target )
		result = this->m_composition_target->SetRoot( nullptr );
	if ( this->m_composition_visual )
	{
		const auto content_result = this->m_composition_visual->SetContent( nullptr );
		if ( SUCCEEDED( result ) ) result = content_result;
	}
	if ( this->m_composition_device )
	{
		const auto commit_result = this->m_composition_device->Commit( );
		if ( SUCCEEDED( result ) ) result = commit_result;
	}
	write_overlay_lifecycle_event( "presentation.detached", this->m_hwnd, result );
	if ( this->m_composition_target )
	{
		this->m_composition_target->Release( );
		this->m_composition_target = nullptr;
	}
	if ( this->m_composition_visual )
	{
		this->m_composition_visual->Release( );
		this->m_composition_visual = nullptr;
	}
	this->close_composition_swap_chain( );
}

bool overlay_t::open_device_backend( )
{
	D3D_FEATURE_LEVEL levels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL selected{};
	return SUCCEEDED( ::D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels, static_cast<UINT>( std::size( levels ) ), D3D11_SDK_VERSION,
		&this->m_device, &selected, &this->m_context ) );
}

bool overlay_t::open_swap_chain_backend( )
{
	if ( !this->m_device || !this->m_hwnd || this->m_swap_chain
		|| this->m_render_width < 64 || this->m_render_height < 64 )
	{
		return false;
	}

	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIDevice> dxgi_device{};
	ComPtr<IDXGIAdapter> adapter{};
	ComPtr<IDXGIFactory2> factory{};
	if ( FAILED( this->m_device->QueryInterface( IID_PPV_ARGS( &dxgi_device ) ) )
		|| FAILED( dxgi_device->GetAdapter( &adapter ) )
		|| FAILED( adapter->GetParent( IID_PPV_ARGS( &factory ) ) ) )
	{
		return false;
	}

	DXGI_SWAP_CHAIN_DESC1 description{};
	description.Width = this->m_render_width;
	description.Height = this->m_render_height;
	description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	description.SampleDesc = { 1, 0 };
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.BufferCount = 2;
	description.Scaling = DXGI_SCALING_STRETCH;
	description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	ComPtr<IDXGISwapChain1> swap_chain1{};
	if ( FAILED( factory->CreateSwapChainForHwnd(
		this->m_device, this->m_hwnd, &description,
		nullptr, nullptr, &swap_chain1 ) ) )
	{
		return false;
	}

	ComPtr<IDXGISwapChain> swap_chain{};
	if ( FAILED( swap_chain1.As( &swap_chain ) ) ) return false;
	this->m_swap_chain = swap_chain.Detach( );
	ComPtr<IDXGISwapChain2> swap_chain2{};
	if ( FAILED( swap_chain1.As( &swap_chain2 ) )
		|| FAILED( swap_chain2->SetMaximumFrameLatency( 1 ) ) )
	{
		this->m_swap_chain->Release( );
		this->m_swap_chain = nullptr;
		return false;
	}
	this->m_frame_latency_waitable =
		swap_chain2->GetFrameLatencyWaitableObject( );
	if ( !this->m_frame_latency_waitable || !this->create_color_target( ) )
	{
		if ( this->m_frame_latency_waitable )
			::CloseHandle( this->m_frame_latency_waitable );
		this->m_frame_latency_waitable = nullptr;
		if ( this->m_rtv ) this->m_rtv->Release( );
		this->m_rtv = nullptr;
		if ( this->m_back_buffer ) this->m_back_buffer->Release( );
		this->m_back_buffer = nullptr;
		this->m_swap_chain->Release( );
		this->m_swap_chain = nullptr;
		return false;
	}

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>( this->m_render_width );
	viewport.Height = static_cast<float>( this->m_render_height );
	viewport.MaxDepth = 1.0f;
	this->m_context->RSSetViewports( 1, &viewport );
	this->m_present_tearing_enabled = false;
	return true;
}

bool overlay_t::initialize_graphics()
{
	this->m_composition_active = k_use_composition_backend
		&& this->open_composition_backend( );
	if ( !this->m_composition_active && !this->open_device_backend( ) )
	{
		return false;
	}

	IDXGIDevice* dxgi_device{};
	if ( SUCCEEDED( this->m_device->QueryInterface( IID_PPV_ARGS( &dxgi_device ) ) ) )
	{

		dxgi_device->SetGPUThreadPriority( -2 );
		dxgi_device->Release( );
	}
	IDXGIDevice1* dxgi_device1{};
	if ( SUCCEEDED( this->m_device->QueryInterface( IID_PPV_ARGS( &dxgi_device1 ) ) ) )
	{
		dxgi_device1->SetMaximumFrameLatency( 1 );
		dxgi_device1->Release( );
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	this->m_imgui_context_active = true;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = 0;
	io.IniFilename = nullptr;

	if (!ImGui_ImplWin32_Init(this->m_hwnd))
	{
		return false;
	}
	this->m_imgui_win32_active = true;
	if (!ImGui_ImplDX11_Init(this->m_device, this->m_context))
	{
		return false;
	}
	this->m_imgui_dx11_active = true;

	if ( !this->load_preview_asset( ) )
	{
		app::context().diagnostics.warning( "failed to load embedded CT preview texture." );
	}

	{
		auto* atlas = ImGui::GetIO().Fonts;
		if ( !atlas )
		{
			app::context().diagnostics.warning( "ImGui font atlas is unavailable." );
			return false;
		}
		this->m_fonts.notosans_medium_12 = zdraw::add_font_from_memory(
			resources::fonts::notosans_medium, static_cast<int>(sizeof(resources::fonts::notosans_medium)),
			12.0f, 512, 512, zdraw::font_raster_profile::smooth,
			atlas->GetGlyphRangesCyrillic( ) );
		this->m_fonts.esp_text_11 = zdraw::add_font_from_memory(
			resources::fonts::notosans_medium, static_cast<int>(sizeof(resources::fonts::notosans_medium)),
			11.0f, 512, 512, zdraw::font_raster_profile::esp_text,
			atlas->GetGlyphRangesCyrillic( ) );

		static constexpr ImWchar no_fallback_preload[]{ 0 };
		if ( std::filesystem::exists( "C:/Windows/Fonts/seguisym.ttf" ) )
		{
			if ( !zdraw::merge_font_from_file( this->m_fonts.esp_text_11,
				"C:/Windows/Fonts/seguisym.ttf", 11.0f,
				zdraw::font_raster_profile::esp_text, no_fallback_preload ) )
			{
				app::context().diagnostics.warning(
					"failed to merge Segoe UI Symbol ESP fallback." );
			}
		}
		if ( std::filesystem::exists( "C:/Windows/Fonts/seguiemj.ttf" ) )
		{
			if ( !zdraw::merge_font_from_file( this->m_fonts.esp_text_11,
				"C:/Windows/Fonts/seguiemj.ttf", 11.0f,
				zdraw::font_raster_profile::esp_text, no_fallback_preload ) )
			{
				app::context().diagnostics.warning(
					"failed to merge Segoe UI Emoji ESP fallback." );
			}
		}
		this->m_fonts.weapons_15 = zdraw::add_font_from_memory(
			resources::fonts::weapons, static_cast<int>(sizeof(resources::fonts::weapons)),
			16.0f, 512, 512, zdraw::font_raster_profile::smooth );
		this->m_fonts.weapons_esp_15 = zdraw::add_font_from_memory(
			resources::fonts::weapons, static_cast<int>(sizeof(resources::fonts::weapons)),
			16.0f, 512, 512, zdraw::font_raster_profile::esp_icon );
		if ( this->m_fonts.weapons_esp_15 && this->m_fonts.weapons_15 )
		{
			this->m_fonts.weapons_esp_15->plain_im_font = this->m_fonts.weapons_15->im_font;
		}
		const auto dpi_window = this->m_window_tracker.target( )
			? this->m_window_tracker.target( ) : this->m_hwnd;
		this->m_ui_dpi_scale = std::clamp(
			static_cast<float>( ::GetDpiForWindow( dpi_window ) ) / 96.0f,
			1.0f, 1.5f );
		const auto menu_dpi_scale = this->m_ui_dpi_scale;
		this->m_fonts.menu_regular_12 = zdraw::add_font_from_file("C:/Windows/Fonts/segoeui.ttf", 16.0f * menu_dpi_scale);
		this->m_fonts.menu_semibold_13 = zdraw::add_font_from_file("C:/Windows/Fonts/seguisb.ttf", 16.0f * menu_dpi_scale);
		this->m_fonts.menu_brand_30 = zdraw::add_font_from_file("C:/Windows/Fonts/segoeuib.ttf", 38.0f * menu_dpi_scale);
		const auto font_ready = []( const zdraw::font* font )
		{
			return font && font->im_font;
		};
		if ( !font_ready( this->m_fonts.notosans_medium_12 )
			|| !font_ready( this->m_fonts.esp_text_11 )
			|| !font_ready( this->m_fonts.weapons_15 )
			|| !font_ready( this->m_fonts.weapons_esp_15 )
			|| !font_ready( this->m_fonts.menu_regular_12 )
			|| !font_ready( this->m_fonts.menu_semibold_13 )
			|| !font_ready( this->m_fonts.menu_brand_30 ) )
		{
			app::context().diagnostics.warning(
				"one or more required ImGui fonts failed to load." );
			return false;
		}
		if ( !atlas->Build( ) )
		{
			app::context().diagnostics.warning( "failed to build ImGui font atlas." );
			return false;
		}
		if ( !ImGui_ImplDX11_CreateDeviceObjects() )
		{
			app::context().diagnostics.warning(
				"failed to create ImGui DX11 font/device resources." );
			return false;
		}
	}

	return true;
}

bool overlay_t::create_color_target( )
{
	if ( FAILED( this->m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &this->m_back_buffer ) ) ) )
	{
		return false;
	}

	if ( FAILED( this->m_device->CreateRenderTargetView(
		this->m_back_buffer, nullptr, &this->m_rtv ) ) )
	{
		this->m_back_buffer->Release( );
		this->m_back_buffer = nullptr;
		return false;
	}
	return true;
}

bool overlay_t::resize_graphics( const std::uint32_t width, const std::uint32_t height )
{
	if ( width < 64 || height < 64 )
	{
		return false;
	}
	if ( this->m_composition_active )
	{
		if ( this->m_presentation_attached )
			return false;
		this->close_composition_swap_chain( );
		this->m_render_width = width;
		this->m_render_height = height;
		return true;
	}
	if ( !this->m_swap_chain )
		return false;

	this->m_context->OMSetRenderTargets( 0, nullptr, nullptr );
	this->m_context->ClearState( );
	this->m_context->Flush( );
	if ( this->m_rtv )
	{
		this->m_rtv->Release( );
		this->m_rtv = nullptr;
	}
	if ( this->m_back_buffer )
	{
		this->m_back_buffer->Release( );
		this->m_back_buffer = nullptr;
	}

	if ( FAILED( this->m_swap_chain->ResizeBuffers(
		0, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0 ) ) )
	{
		this->create_color_target( );
		return false;
	}
	if ( !this->create_color_target( ) )
	{
		return false;
	}

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>( width );
	viewport.Height = static_cast<float>( height );
	viewport.MaxDepth = 1.0f;
	this->m_context->RSSetViewports( 1, &viewport );
	this->m_render_width = width;
	this->m_render_height = height;
	return true;
}

bool overlay_t::load_preview_asset( )
{
	const auto com_result = ::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
	if ( FAILED( com_result ) && com_result != RPC_E_CHANGED_MODE )
	{
		return false;
	}
	const auto uninitialize_com = SUCCEEDED( com_result );

	IWICImagingFactory* factory{};
	IWICStream* stream{};
	IWICBitmapDecoder* decoder{};
	IWICBitmapFrameDecode* frame{};
	IWICFormatConverter* converter{};
	ID3D11Texture2D* texture{};
	bool loaded{};

	do
	{
		if ( FAILED( ::CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) ) ) break;
		if ( FAILED( factory->CreateStream( &stream ) ) ) break;
		if ( FAILED( stream->InitializeFromMemory(
			const_cast<BYTE*>( reinterpret_cast<const BYTE*>( resources::images::ct_png ) ),
			static_cast<DWORD>( resources::images::ct_png_size ) ) ) ) break;
		if ( FAILED( factory->CreateDecoderFromStream( stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder ) ) ) break;
		if ( FAILED( decoder->GetFrame( 0, &frame ) ) ) break;
		if ( FAILED( factory->CreateFormatConverter( &converter ) ) ) break;
		if ( FAILED( converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) ) break;

		UINT width{}, height{};
		if ( FAILED( converter->GetSize( &width, &height ) ) || width == 0 || height == 0 ) break;
		const auto stride = width * 4u;
		std::vector<std::uint8_t> pixels( static_cast<std::size_t>( stride ) * height );
		if ( FAILED( converter->CopyPixels( nullptr, stride, static_cast<UINT>( pixels.size( ) ), pixels.data( ) ) ) ) break;

		D3D11_TEXTURE2D_DESC description{};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_IMMUTABLE;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initial_data{};
		initial_data.pSysMem = pixels.data( );
		initial_data.SysMemPitch = stride;
		if ( FAILED( this->m_device->CreateTexture2D( &description, &initial_data, &texture ) ) ) break;
		if ( FAILED( this->m_device->CreateShaderResourceView( texture, nullptr, &this->m_ct_preview_texture ) ) ) break;

		this->m_ct_preview_width = width;
		this->m_ct_preview_height = height;
		loaded = true;
	} while ( false );

	if ( texture ) texture->Release( );
	if ( converter ) converter->Release( );
	if ( frame ) frame->Release( );
	if ( decoder ) decoder->Release( );
	if ( stream ) stream->Release( );
	if ( factory ) factory->Release( );
	if ( uninitialize_com ) ::CoUninitialize( );
	return loaded;
}

ID3D11ShaderResourceView* overlay_t::steam_avatar_texture(
	const std::uint64_t steamid, const std::string_view display_name )
{
	if ( !this->m_device ) return nullptr;
	const auto avatar_key = steamid ? steamid : ( 0x8000000000000000ull
		| ( static_cast<std::uint64_t>( std::hash<std::string_view>{}( display_name ) % 15u ) + 1u ) );
	if ( const auto found = this->m_steam_avatars.find( avatar_key );
		found != this->m_steam_avatars.end() ) return found->second;
	const auto now = std::chrono::steady_clock::now();
	if ( const auto missing = this->m_missing_steam_avatars.find( avatar_key );
		missing != this->m_missing_steam_avatars.end() )
	{
		if ( now < missing->second ) return nullptr;
		this->m_missing_steam_avatars.erase( missing );
	}
	const auto retry_at = now + std::chrono::seconds( 30 );

	if ( !steamid )
	{
		auto& renderer = chams::g_renderer;
		if ( !renderer.ensure_vpk() ) return nullptr;
		const auto variant = static_cast<unsigned>( avatar_key & 0xffu );
		const auto path = std::format(
			"panorama/images/avatars/avatar_sub_{:02}_psd.vtex_c", variant );
		const auto data = chams::load_texture( renderer.vpk(), path );
		if ( !data.valid() )
		{
			this->m_missing_steam_avatars[ avatar_key ] = retry_at;
			return nullptr;
		}
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = data.width;
		desc.Height = data.height;
		desc.MipLevels = data.mip_count;
		desc.ArraySize = 1;
		desc.Format = static_cast<DXGI_FORMAT>( data.dxgi_format );
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		std::vector<D3D11_SUBRESOURCE_DATA> subresources( data.mip_count );
		for ( std::uint32_t level = 0; level < data.mip_count; ++level )
		{
			const auto width = std::max<std::uint32_t>( 1, data.width >> level );
			const auto blocks = std::max<std::uint32_t>( 1, ( width + 3 ) / 4 );
			subresources[level].pSysMem = data.mips[level].data();
			subresources[level].SysMemPitch = data.block_size
				? blocks * data.block_size : width * 4;
		}
		ID3D11Texture2D* texture{};
		ID3D11ShaderResourceView* view{};
		if ( FAILED( this->m_device->CreateTexture2D( &desc, subresources.data(), &texture ) )
			|| FAILED( this->m_device->CreateShaderResourceView( texture, nullptr, &view ) ) )
		{
			if ( texture ) texture->Release();
			this->m_missing_steam_avatars[ avatar_key ] = retry_at;
			return nullptr;
		}
		texture->Release();
		this->m_steam_avatars.emplace( avatar_key, view );
		return view;
	}
	const auto com_result = ::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
	struct com_scope
	{
		bool owned{};
		~com_scope() { if ( owned ) ::CoUninitialize(); }
	} com{ SUCCEEDED( com_result ) };
	if ( FAILED( com_result ) && com_result != RPC_E_CHANGED_MODE ) return nullptr;

	wchar_t steam_path[ 1024 ]{};
	DWORD steam_path_bytes = sizeof( steam_path );
	HKEY key{};
	if ( ::RegOpenKeyExW( HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0,
		KEY_READ, &key ) != ERROR_SUCCESS
		|| ::RegQueryValueExW( key, L"SteamPath", nullptr, nullptr,
			reinterpret_cast<BYTE*>( steam_path ), &steam_path_bytes ) != ERROR_SUCCESS )
	{
		if ( key ) ::RegCloseKey( key );
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}
	::RegCloseKey( key );
	const auto base = std::filesystem::path( steam_path ) / L"config" / L"avatarcache";
	std::filesystem::path avatar{};
	for ( const auto* extension : { L".png", L".jpg", L".jpeg" } )
	{
		auto candidate = base / ( std::to_wstring( steamid ) + extension );
		std::error_code error{};
		if ( std::filesystem::is_regular_file( candidate, error ) )
		{
			avatar = std::move( candidate );
			break;
		}
	}
	if ( avatar.empty() )
	{
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}

	Microsoft::WRL::ComPtr<IWICImagingFactory> factory{};
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder{};
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame{};
	Microsoft::WRL::ComPtr<IWICFormatConverter> converter{};
	if ( FAILED( ::CoCreateInstance( CLSID_WICImagingFactory, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) )
		|| FAILED( factory->CreateDecoderFromFilename( avatar.c_str(), nullptr,
			GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder ) )
		|| FAILED( decoder->GetFrame( 0, &frame ) )
		|| FAILED( factory->CreateFormatConverter( &converter ) )
		|| FAILED( converter->Initialize( frame.Get(), GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) )
	{
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}
	UINT width{}, height{};
	if ( FAILED( converter->GetSize( &width, &height ) ) || !width || !height
		|| width > 1024 || height > 1024 )
	{
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}
	const auto stride = width * 4u;
	std::vector<std::uint8_t> pixels( static_cast<std::size_t>( stride ) * height );
	if ( FAILED( converter->CopyPixels( nullptr, stride,
		static_cast<UINT>( pixels.size() ), pixels.data() ) ) )
	{
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}
	D3D11_TEXTURE2D_DESC description{};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA initial{};
	initial.pSysMem = pixels.data();
	initial.SysMemPitch = stride;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
	ID3D11ShaderResourceView* view{};
	if ( FAILED( this->m_device->CreateTexture2D( &description, &initial, &texture ) )
		|| FAILED( this->m_device->CreateShaderResourceView( texture.Get(), nullptr, &view ) ) )
	{
		this->m_missing_steam_avatars[ avatar_key ] = retry_at;
		return nullptr;
	}
	this->m_steam_avatars.emplace( avatar_key, view );
	return view;
}

LRESULT CALLBACK overlay_t::window_callback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch ( msg )
	{
	case WM_DISPLAYCHANGE:
		if ( auto* overlay = overlay_t::s_active_instance.load( std::memory_order_acquire ) )
			overlay->m_window_tracker.notify_display_change( );
		return ::DefWindowProcW( hwnd, msg, wp, lp );
	case WM_MOUSEACTIVATE:
		return overlay_t::s_menu_hit_testing.load( std::memory_order_acquire )
			? MA_NOACTIVATEANDEAT : MA_NOACTIVATE;
	case WM_NCHITTEST:
		return overlay_t::s_menu_hit_testing.load( std::memory_order_acquire )
			? HTCLIENT : HTTRANSPARENT;
	case WM_SETCURSOR:
		if ( overlay_t::s_menu_hit_testing.load( std::memory_order_acquire )
			&& LOWORD( lp ) == HTCLIENT )
		{
			LPCSTR cursor_id = IDC_ARROW;
			switch ( static_cast<ImGuiMouseCursor>(
				overlay_t::s_menu_cursor.load( std::memory_order_relaxed ) ) )
			{
			case ImGuiMouseCursor_TextInput: cursor_id = IDC_IBEAM; break;
			case ImGuiMouseCursor_ResizeAll: cursor_id = IDC_SIZEALL; break;
			case ImGuiMouseCursor_ResizeNS: cursor_id = IDC_SIZENS; break;
			case ImGuiMouseCursor_ResizeEW: cursor_id = IDC_SIZEWE; break;
			case ImGuiMouseCursor_ResizeNESW: cursor_id = IDC_SIZENESW; break;
			case ImGuiMouseCursor_ResizeNWSE: cursor_id = IDC_SIZENWSE; break;
			case ImGuiMouseCursor_Hand: cursor_id = IDC_HAND; break;
			case ImGuiMouseCursor_NotAllowed: cursor_id = IDC_NO; break;
			default: break;
			}
			::SetCursor( ::LoadCursorA( nullptr, cursor_id ) );
			return TRUE;
		}
		return ::DefWindowProcW( hwnd, msg, wp, lp );
	case WM_DESTROY:
		overlay_t::s_menu_hit_testing.store( false, std::memory_order_release );
		::PostQuitMessage( 0 );
		return 0;
	default:
		return ::DefWindowProcW( hwnd, msg, wp, lp );
	}
	return ::DefWindowProcW( hwnd, msg, wp, lp );
}
