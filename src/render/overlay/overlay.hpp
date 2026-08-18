#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <dxgi1_5.h>
#include <dcomp.h>
#include <render/draw.hpp>
#include <render/overlay/window.hpp>
#include <render/overlay/input.hpp>
#include <thread>
#include <chrono>
#include <unordered_map>

class overlay_t
{
public:
	struct loaded_fonts
	{
		zdraw::font* weapons_15{};
		zdraw::font* weapons_esp_15{};
		zdraw::font* notosans_medium_12{};
		zdraw::font* esp_text_11{};
		zdraw::font* menu_regular_12{};
		zdraw::font* menu_semibold_13{};
		zdraw::font* menu_brand_30{};
	};

	ID3D11Device* device( ) const { return m_device; }
	ID3D11DeviceContext* context( ) const { return m_context; }

	ID3D11RenderTargetView* rtv( ) const { return m_rtv; }
	ID3D11DepthStencilView* dsv( ) const { return m_dsv; }
	ID3D11ShaderResourceView* ct_preview_texture( ) const { return m_ct_preview_texture; }
	std::uint32_t ct_preview_width( ) const { return m_ct_preview_width; }
	std::uint32_t ct_preview_height( ) const { return m_ct_preview_height; }
	ID3D11ShaderResourceView* steam_avatar_texture( std::uint64_t steamid,
		std::string_view display_name = {} );

	bool launch( );
	void request_shutdown( ) noexcept
	{
		this->m_shutdown_requested.store( true, std::memory_order_release );

		if ( const auto thread = this->m_render_thread_id.load(
			std::memory_order_acquire ) )
		{
			::PostThreadMessageW( thread, WM_NULL, 0, 0 );
		}
	}

	[[nodiscard]] const loaded_fonts& fonts( ) const { return this->m_fonts; }
	[[nodiscard]] float ui_dpi_scale( ) const noexcept { return this->m_ui_dpi_scale; }
	[[nodiscard]] std::uint32_t ui_reference_width( ) const noexcept { return this->m_ui_reference_width; }
	[[nodiscard]] std::uint32_t ui_reference_height( ) const noexcept { return this->m_ui_reference_height; }
	[[nodiscard]] bool ui_uses_fullscreen_canvas( ) const noexcept { return this->m_ui_fullscreen_canvas; }
	[[nodiscard]] HWND hwnd( ) const { return this->m_hwnd; }
	[[nodiscard]] bool combat_input_ready( ) const noexcept
	{
		return this->m_combat_input_ready.load( std::memory_order_acquire );
	}
	[[nodiscard]] explicit operator bool( ) const { return this->m_hwnd != nullptr; }

	static inline std::atomic<bool> s_modal_active{ false };

	void request_config_load( );
	void request_config_save( );
	void request_lua_import( );

private:
	void run( );
	void shutdown( ) noexcept;
	void synchronize_menu_focus( );
	void set_menu_hit_testing( bool enabled );
	void synchronize_content_visibility( );
	void apply_capture_policy( );
	void synchronize_window_bounds( );
	void route_overlay_input( );
	bool initialize_graphics( );
	bool open_device_backend( );
	bool open_composition_backend( );
	bool open_swap_chain_backend( );
	bool attach_presentation( );
	void detach_presentation( ) noexcept;
	bool resize_graphics( std::uint32_t width, std::uint32_t height );
	bool create_color_target( );
	bool load_preview_asset( );
	bool register_overlay_class( );
	void stop_config_dialog( ) noexcept;

	static LRESULT CALLBACK window_callback( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp );
	static UINT_PTR CALLBACK config_dialog_hook( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp );
	static inline std::atomic<bool> s_menu_hit_testing{};
	static inline std::atomic<int> s_menu_cursor{};
	static inline std::atomic<overlay_t*> s_active_instance{};

	std::atomic<bool> m_cfg_ready{ false };
	std::mutex m_cfg_mutex{};
	std::string m_cfg_path{};
	bool m_cfg_save{ false };
	bool m_cfg_lua_import{ false };
	std::jthread m_cfg_dialog_thread{};
	std::atomic<HWND> m_cfg_dialog_hwnd{};
	std::atomic<DWORD> m_cfg_dialog_thread_id{};
	std::atomic<bool> m_cfg_dialog_cancel{};

	HWND m_hwnd{};
	ATOM m_atom{};
	bool m_input_visible{ false };
	bool m_obs_bypass_observed{};
	bool m_obs_bypass_initialized{};
	bool m_overlay_visible{};
	bool m_content_suppressed{};
	bool m_window_tracking_active{};
	bool m_input_router_active{};
	bool m_menu_interactive{};
	bool m_menu_hit_testing{};
	bool m_game_menu_opened_by_overlay{};
	bool m_escape_was_down{};
	bool m_imgui_context_active{};
	bool m_imgui_win32_active{};
	bool m_imgui_dx11_active{};
	bool m_chams_renderer_initialized{};
	bool m_chams_preview_initialized{};
	std::atomic<bool> m_shutdown_requested{};
	std::atomic<bool> m_shutdown_active{};
	std::atomic<bool> m_combat_input_ready{};
	std::atomic<DWORD> m_render_thread_id{};
	std::array<bool, 256> m_overlay_key_states{};

	ID3D11Device* m_device{};
	ID3D11DeviceContext* m_context{};
	IDXGISwapChain* m_swap_chain{};
	IDCompositionDevice* m_composition_device{};
	IDCompositionTarget* m_composition_target{};
	IDCompositionVisual* m_composition_visual{};
	HANDLE m_frame_latency_waitable{};
	bool m_composition_active{};
	bool m_presentation_attached{};
	bool m_allow_tearing{};
	bool m_present_tearing_enabled{};
	bool m_resize_pending{};
	bool m_frame_latency_recovery_pending{};
	bool m_composition_swap_chain_ever_attached{};
	std::uint64_t m_presentation_generation{};
	ID3D11Texture2D* m_back_buffer{};
	ID3D11RenderTargetView* m_rtv{};
	ID3D11DepthStencilView* m_dsv{};
	ID3D11ShaderResourceView* m_ct_preview_texture{};
	std::unordered_map<std::uint64_t, ID3D11ShaderResourceView*> m_steam_avatars{};
	std::unordered_map<std::uint64_t,
		std::chrono::steady_clock::time_point> m_missing_steam_avatars{};
	std::uint32_t m_ct_preview_width{};
	std::uint32_t m_ct_preview_height{};
	std::uint32_t m_render_width{};
	std::uint32_t m_render_height{};
	float m_ui_dpi_scale{ 1.0f };
	std::uint32_t m_ui_reference_width{};
	std::uint32_t m_ui_reference_height{};
	bool m_ui_fullscreen_canvas{};

	loaded_fonts m_fonts{};
	window_tracker m_window_tracker{};
	input_router m_input_router{};

	static constexpr const wchar_t* k_class_name{ L"vesta.overlay" };
};
