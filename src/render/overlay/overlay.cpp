#include <stdafx.hpp>
#include <system/frame_schedule.hpp>
#include <system/object_watch.hpp>
#include <render/overlay/present_policy.hpp>
#include <render/overlay/hud.hpp>
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
		const auto path = directory / L"overlay_lifecycle.log";
        const auto size = std::filesystem::file_size(path, error);
        const auto mode = !error && size > 1024 * 1024 ? std::ios::trunc : std::ios::app;
        std::ofstream stream(path, mode);
		stream << "tick=" << ::GetTickCount64( )
			<< " pid=" << ::GetCurrentProcessId( )
			<< " event=" << event
			<< " hwnd=0x" << std::hex
			<< reinterpret_cast<std::uintptr_t>( overlay )
			<< " hr=0x" << static_cast<unsigned long>( result )
			<< std::dec << '\n';
		stream.flush( );
	}

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

    class config_watch
    {
	  public:
	    [[nodiscard]] bool update() noexcept
	    {
		    const bool combat = m_combat.update(config::combat_settings);
		    const bool visual = m_visual.update(config::visual_settings);
		    const bool general = m_general.update(config::general_settings);
		    return combat || visual || general;
	    }

	  private:
	    foundation::object_watch<config::combat_profile> m_combat{config::combat_settings};
	    foundation::object_watch<config::visual_profile> m_visual{config::visual_settings};
	    foundation::object_watch<config::general_profile> m_general{config::general_settings};
    };

} // namespace

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
	this->m_ui_reference_width = 0;
	this->m_ui_reference_height = 0;
	this->m_ui_fullscreen_canvas = false;

	const DWORD extended_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
		| WS_EX_NOACTIVATE
		| ( k_use_composition_backend
			? WS_EX_NOREDIRECTIONBITMAP : 0 );
	// This HWND has no DirectComposition target/root while CS2 is backgrounded.
	// WS_EX_NOREDIRECTIONBITMAP and its 1x1 size keep it out of presentation.
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
		// Poll once after binding. Readiness may already be true; attachment still
		// happens only from synchronize_window_bounds() after graphics preparation.
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

	const auto completed = this->run();
	write_overlay_lifecycle_event(completed ? "run.end" : "run.failed", this->m_hwnd);
	this->shutdown( );

	return completed;
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

bool overlay_t::run()
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
	foundation::frame_schedule frame_clock;

	std::atomic<std::uint64_t> cache_revision{ 0 };
	config_watch observed_config;
	std::jthread cache_writer([&cache_revision](std::stop_token stop)
	{
		// JSON serialization and durable cache writes are background maintenance.
		// They must yield to the frame/game workers when both become runnable.
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
		const bool frame_latency_signal = signal == this->m_frame_latency_waitable;
		std::optional<platform::performance::scope> wait_profile{};
		if ( frame_latency_signal )
			wait_profile.emplace(
				platform::performance::zone::wait_frame_latency );

		const ULONGLONG frame_latency_deadline = frame_latency_signal
			? ::GetTickCount64( ) + 50u : 0u;
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
				if ( !this->m_frame_latency_unreliable )
				{
					this->m_frame_latency_unreliable = true;
					write_overlay_lifecycle_event(
						"presentation.frame_latency_stalled", this->m_hwnd );
				}
				return false;
			}
			return false;
		}
	};

	bool end_was_down = false;
	bool completed = true;
	render::present_recovery present_recovery;
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
				&& (config::visual_settings.m_player.active( ) || config::visual_settings.m_chams.enabled),
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

        const auto rate = config::general_settings.limit_fps && config::general_settings.fps_limit > 0
            ? static_cast<std::uint32_t>(config::general_settings.fps_limit)
            : (m_frame_latency_unreliable ? std::max(m_window_tracker.refresh_rate(), 60u) : 0u);
        frame_clock.set_rate(rate, std::chrono::steady_clock::now());
        if (m_frame_latency_waitable && !m_frame_latency_unreliable)
        {
            const auto transition = m_window_tracker.transition_revision();
            if (!wait_for_signal(m_frame_latency_waitable, transition)) continue;
        }
        const auto now = std::chrono::steady_clock::now();
        if (rate && now < frame_clock.deadline())
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
                frame_clock.deadline() - now).count();
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<LONGLONG>(1, (remaining + 99) / 100);
            if (fps_timer && ::SetWaitableTimer(fps_timer, &due, 0, nullptr, nullptr, FALSE))
            {
                if (!wait_for_signal(fps_timer, m_window_tracker.transition_revision())) continue;
            }
            else std::this_thread::sleep_until(frame_clock.deadline());
        }
        frame_clock.advance(std::chrono::steady_clock::now());
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
			// Performance zones below are compile-time no-ops in production Release.
			const auto pose_frame = game::render_poses( ).latest( );
            const auto& presentation_pose = pose_frame;
            game::presentation_camera_sample camera{};
            if (game::camera().sample_presentation(camera))
                game::camera().begin_presentation_frame(camera, m_render_width, m_render_height);
            else if (pose_frame)
                game::camera().begin_presentation_frame(pose_frame->camera, m_render_width, m_render_height);

			{
				VESTA_PERF_SCOPE( chams );
				chams::g_renderer.render_world_effects(
					this->m_rtv, this->m_render_width, this->m_render_height );
				chams::g_renderer.render_frame(
					this->m_rtv, this->m_render_width, this->m_render_height,
					presentation_pose );
			}
			{
				VESTA_PERF_SCOPE( player_esp );
				features::visuals::player().render(draw_list, presentation_pose);
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
			render::hud::draw_watermark(app::context().menu.is_open());
			render::hud::draw_spectator_list(app::context().menu.is_open());
			if ( !spectator_suppressed )
			{
				render::hud::draw_event_log(app::context().menu.is_open());
				render::hud::draw_keybind_list(app::context().menu.is_open());
				render::hud::draw_bomb_info(app::context().menu.is_open());
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

		bool config_changed{};
		{
			VESTA_PERF_SCOPE( config_fingerprint );
			config_changed = observed_config.update();
		}
		if (config_changed)
		{
			config::publish_runtime_snapshot();
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
		// Keep CPU/GPU command construction separate from message dispatch and
		// Present retry waits in the telemetry.
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
                // Discard stale CPU work instead of retrying the same image indefinitely.
                if (m_frame_latency_unreliable)
                    ::MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                cancel_pending_frame = true;
                break;
			}
			if ( present_result == DXGI_ERROR_INVALID_CALL && this->m_present_tearing_enabled )
			{
				this->m_present_tearing_enabled = false;
				continue;
			}
			break;
		}
		if ( cancel_pending_frame ) continue;
		if ( FAILED( present_result ) )
		{
			const auto device_result = m_device->GetDeviceRemovedReason();
			write_overlay_lifecycle_event("presentation.present_failed", m_hwnd, present_result);
			write_overlay_lifecycle_event("presentation.device_status", m_hwnd, device_result);
			const auto now = std::chrono::steady_clock::now();
			if (present_recovery.retry_surface(present_result, device_result, now))
			{
				detach_presentation();
				m_presentation_retry.failed(now);
				m_resize_pending = true;
				write_overlay_lifecycle_event("presentation.recovery_scheduled", m_hwnd);
				continue;
			}
			completed = false;
			break;
		}
		if (present_result == S_OK)
			platform::performance::record_present();
		platform::performance::flush_if_due( );
	}

	if ( fps_timer ) ::CloseHandle( fps_timer );
	this->m_render_thread_id.store( 0, std::memory_order_release );
	this->set_menu_hit_testing( false );
	platform::performance::flush_if_due( true );
	config::storage.write_cache();
	return completed;
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

	// Retire every command that can still reference a composition back buffer
	// before removing that buffer from the visual tree.
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

	// WDA_EXCLUDEFROMCAPTURE keeps the layered overlay fully visible locally while
	// presenting it as transparent to supported desktop/window capture paths.
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
		&& this->m_presentation_attached == this->m_window_tracker.visible( ) )
		return;

	const bool target_ready = this->m_window_tracker.visible( );
	const auto recovery_now = std::chrono::steady_clock::now();
	if (target_ready && !m_presentation_attached && !m_presentation_retry.ready(recovery_now)) return;

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
		|| width != this->m_render_width || height != this->m_render_height )
	{
		this->detach_presentation( );
		const bool dimensions_changed = width != this->m_render_width
			|| height != this->m_render_height;
		if ( dimensions_changed )
		{
			if ( this->m_composition_active )
			{
				if ( !this->resize_graphics( width, height ) )
				{
					this->m_resize_pending = true;
					return;
				}
						}
			else
			{
				// No HWND-bound swap chain exists while detached; the dimensions
				// become the creation parameters for attach_presentation().
				this->m_render_width = width;
				this->m_render_height = height;
			}
		}
		if ( !this->attach_presentation( ) )
		{
			m_presentation_retry.failed(recovery_now);
			this->m_resize_pending = true;
			return;
		}
		m_presentation_retry.succeeded();
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
    ComPtr<ID3D11Device> device{};
    ComPtr<ID3D11DeviceContext> context{};
    if (FAILED(render::create_overlay_device(
        ::MonitorFromWindow(m_window_tracker.target(), MONITOR_DEFAULTTOPRIMARY), &device, &context))) return false;

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

    auto description = render::composition_description(m_render_width, m_render_height, m_allow_tearing);

	ComPtr<IDXGISwapChain1> swap_chain1{};
	ComPtr<IDXGISwapChain2> swap_chain2{};
	ComPtr<IDXGISwapChain> swap_chain{};
	ComPtr<ID3D11Texture2D> back_buffer{};
	ComPtr<ID3D11RenderTargetView> render_target{};
    auto creation = factory->CreateSwapChainForComposition(m_device, &description, nullptr, &swap_chain1);
    if (FAILED(creation) && m_allow_tearing)
    {
        swap_chain1.Reset();
        description.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        creation = factory->CreateSwapChainForComposition(m_device, &description, nullptr, &swap_chain1);
        if (SUCCEEDED(creation)) m_allow_tearing = false;
    }
    if (FAILED(creation))
        write_overlay_lifecycle_event("presentation.create_failed", m_hwnd, creation);
    if ( FAILED(creation)
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
	this->m_frame_latency_unreliable = false;
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
		// The HWND-bound swap chain is itself the presentation attachment. It is
		// created only after readiness and destroyed on every detach.
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
    return SUCCEEDED(render::create_overlay_device(
        ::MonitorFromWindow(m_window_tracker.target(), MONITOR_DEFAULTTOPRIMARY), &m_device, &m_context));
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
		// The overlay is presentation work, never more important than CS2's own
		// command queue. A positive priority here starved the game under GPU load.
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
