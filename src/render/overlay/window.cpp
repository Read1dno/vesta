#include <render/overlay/window.hpp>

#include <dwmapi.h>

#include <algorithm>

namespace
{
	struct window_search
	{
		DWORD process_id{};
		HWND best{};
		std::int64_t best_area{ -1 };
	};

	[[nodiscard]] bool is_game_window( const HWND window, const DWORD process_id )
	{
		if ( !window || ::GetWindow( window, GW_OWNER ) )
			return false;

		DWORD owner_process{};
		::GetWindowThreadProcessId( window, &owner_process );
		if ( owner_process != process_id )
			return false;

		wchar_t class_name[ 64 ]{};
		return ::GetClassNameW(
			window, class_name, static_cast<int>( std::size( class_name ) ) ) > 0
			&& ::lstrcmpW( class_name, L"SDL_app" ) == 0;
	}

	[[nodiscard]] bool is_dxgi_proxy_window(
		const HWND window, const DWORD process_id )
	{
		if ( !window )
			return false;

		DWORD owner_process{};
		::GetWindowThreadProcessId( window, &owner_process );
		if ( owner_process != process_id )
			return false;

		wchar_t class_name[ 64 ]{};
		return ::GetClassNameW(
			window, class_name, static_cast<int>( std::size( class_name ) ) ) > 0
			&& ::lstrcmpW( class_name, L"D3DProxyWindow" ) == 0;
	}

	struct proxy_search
	{
		DWORD process_id{};
		HWND window{};
	};

	BOOL CALLBACK enum_dxgi_proxy( const HWND window, const LPARAM parameter )
	{
		auto& search = *reinterpret_cast<proxy_search*>( parameter );
		if ( is_dxgi_proxy_window( window, search.process_id ) )
		{
			search.window = window;
			return FALSE;
		}
		return TRUE;
	}

	[[nodiscard]] HWND find_dxgi_proxy_window( const DWORD process_id )
	{
		proxy_search search{ process_id };
		::EnumWindows( enum_dxgi_proxy, reinterpret_cast<LPARAM>( &search ) );
		return search.window;
	}

	BOOL CALLBACK enum_window( const HWND window, const LPARAM parameter )
	{
		auto& search = *reinterpret_cast<window_search*>( parameter );

		if ( !is_game_window( window, search.process_id ) )
		{
			return TRUE;
		}

		BOOL cloaked{};
		if ( SUCCEEDED( ::DwmGetWindowAttribute(
			window, DWMWA_CLOAKED, &cloaked, sizeof( cloaked ) ) ) && cloaked )
		{
			return TRUE;
		}

		RECT client{};
		if ( !::GetClientRect( window, &client ) )
			return TRUE;
		const auto width = static_cast<std::int64_t>( client.right - client.left );
		const auto height = static_cast<std::int64_t>( client.bottom - client.top );
		const auto area = width * height;
		if ( area > search.best_area )
		{
			search.best = window;
			search.best_area = area;
		}
		return TRUE;
	}

	[[nodiscard]] bool same_rect( const RECT& left, const RECT& right )
	{
		return left.left == right.left && left.top == right.top
			&& left.right == right.right && left.bottom == right.bottom;
	}

}

window_tracker::~window_tracker( )
{
	shutdown( );
}

bool window_tracker::covers_monitor( ) const
{
	if ( !m_target || width( ) == 0 || height( ) == 0 )
		return false;
	const auto monitor = ::MonitorFromRect( &m_client, MONITOR_DEFAULTTONULL );
	MONITORINFO information{ .cbSize = sizeof( information ) };
	if ( !monitor || !::GetMonitorInfoW( monitor, &information ) )
		return false;
	constexpr LONG tolerance = 2;
	return std::abs( m_client.left - information.rcMonitor.left ) <= tolerance
		&& std::abs( m_client.top - information.rcMonitor.top ) <= tolerance
		&& std::abs( m_client.right - information.rcMonitor.right ) <= tolerance
		&& std::abs( m_client.bottom - information.rcMonitor.bottom ) <= tolerance;
}

bool window_tracker::initialize( const std::uint32_t process_id )
{
	shutdown( );
	m_failure_code = ERROR_SUCCESS;
	m_failure_stage = 0;
	m_process_id = process_id;
	m_dxgi_proxy = find_dxgi_proxy_window( m_process_id );
	m_target = find_target( );
	if ( !m_target )
	{
		m_failure_stage = 1;
		m_failure_code = ERROR_NOT_FOUND;
		m_next_reacquire_tick = ::GetTickCount64( ) + 100;
	}
	else
	{
		bool changed{};
		if ( !refresh_geometry( changed ) )
		{
			m_failure_stage = 2;
			m_failure_code = ::GetLastError( );
			m_target = nullptr;
			m_next_reacquire_tick = ::GetTickCount64( ) + 100;
		}
	}

	m_visible = calculate_visibility( );
	m_transition_revision.fetch_add( 1, std::memory_order_release );
	s_active.store( this, std::memory_order_release );
	install_hooks( );
	if ( !m_location_hook )
	{
		m_failure_stage = 32;
		m_failure_code = ::GetLastError( );
	}
	return true;
}

void window_tracker::bind_overlay( const HWND overlay )
{
	m_overlay = overlay;
	m_visibility_pending.store( true, std::memory_order_relaxed );
	m_z_order_pending.store( true, std::memory_order_relaxed );
}

void window_tracker::notify_display_change( )
{
	m_geometry_pending.store( true, std::memory_order_relaxed );
	m_visibility_pending.store( true, std::memory_order_relaxed );
	m_z_order_pending.store( true, std::memory_order_relaxed );
	m_transition_revision.fetch_add( 1, std::memory_order_release );
}

bool window_tracker::poll( update& result )
{
	result = {};
	const auto now = ::GetTickCount64( );
	const bool force_hidden =
		m_force_hidden_pending.exchange( false, std::memory_order_acq_rel );
	const auto transition_revision =
		m_transition_revision.load( std::memory_order_acquire );
	result.transition_revision = transition_revision;
	result.transition_changed = transition_revision != m_observed_transition_revision;
	if ( result.transition_changed )
	{
		m_observed_transition_revision = transition_revision;
		m_visibility_pending.store( true, std::memory_order_relaxed );
	}
	if ( m_target && now >= m_next_geometry_refresh_tick )
	{
		m_geometry_pending.store( true, std::memory_order_relaxed );
		m_next_geometry_refresh_tick = now + 500;
	}
	if ( m_target && now >= m_next_visibility_refresh_tick )
	{
		m_visibility_pending.store( true, std::memory_order_relaxed );
		m_next_visibility_refresh_tick = now + 250;
	}
	const bool reacquire_requested =
		m_reacquire_pending.exchange( false, std::memory_order_acq_rel );
	if ( reacquire_requested || ( !m_target && now >= m_next_reacquire_tick ) )
	{
		m_target = find_target( );
		if ( m_target )
		{
			m_failure_stage = 0;
			m_failure_code = ERROR_SUCCESS;
			m_geometry_pending.store( true, std::memory_order_relaxed );
			m_visibility_pending.store( true, std::memory_order_relaxed );
			m_z_order_pending.store( true, std::memory_order_relaxed );
		}
		else
		{
			m_next_reacquire_tick = now + 100;
		}
	}

	if ( m_geometry_pending.exchange( false, std::memory_order_acq_rel ) )
	{
		bool changed{};
		if ( refresh_geometry( changed ) )
			result.geometry_changed = changed;
		else
		{
			m_visible = false;
			m_target = nullptr;
			m_client = {};
			m_next_reacquire_tick = now + 100;
		}
		m_visibility_pending.store( true, std::memory_order_relaxed );
	}

	if ( m_visibility_pending.exchange( false, std::memory_order_acq_rel ) )
	{

		const bool visible = !force_hidden && calculate_visibility( );
		result.visibility_changed = visible != m_visible;
		m_visible = visible;
	}

	const bool z_order_audit =
		m_z_order_pending.exchange( false, std::memory_order_acq_rel );
	result.z_order_changed = z_order_audit && m_target && m_overlay && m_visible
		&& ::GetWindow( m_target, GW_HWNDPREV ) != m_overlay;
	if ( m_target && m_visible && now >= m_next_z_order_repair_tick )
	{

		result.z_order_changed = result.z_order_changed
			|| ( m_overlay && ::GetWindow( m_target, GW_HWNDPREV ) != m_overlay );
		m_next_z_order_repair_tick = now + 1000;
	}
	result.client = m_client;
	result.visible = m_visible;
	return result.geometry_changed || result.visibility_changed || result.z_order_changed
		|| result.transition_changed;
}

void window_tracker::place_overlay( ) const
{
	if ( !m_target || !m_overlay || width( ) == 0 || height( ) == 0 )
		return;

	HWND insert_after = ::GetWindow( m_target, GW_HWNDPREV );
	const bool z_order_is_current = insert_after == m_overlay;
	if ( !insert_after )
	{

		insert_after = HWND_TOP;
	}
	UINT flags = SWP_NOACTIVATE;
	if ( z_order_is_current )
		flags |= SWP_NOZORDER;
	::SetWindowPos(
		m_overlay,
		insert_after,
		m_client.left,
		m_client.top,
		static_cast<int>( width( ) ),
		static_cast<int>( height( ) ),
		flags );
}

void window_tracker::shutdown( )
{
	window_tracker* expected = this;
	s_active.compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel );
	remove_hooks( );
	m_process_id = 0;
	m_target = nullptr;
	m_dxgi_proxy = nullptr;
	m_overlay = nullptr;
	m_client = {};
	m_visible = false;
	m_next_reacquire_tick = 0;
	m_next_geometry_refresh_tick = 0;
	m_next_visibility_refresh_tick = 0;
	m_next_z_order_repair_tick = 0;
	m_force_hidden_pending.store( false, std::memory_order_relaxed );
	m_transition_revision.fetch_add( 1, std::memory_order_release );
	m_observed_transition_revision = 0;
}

std::uint32_t window_tracker::width( ) const
{
	return static_cast<std::uint32_t>( std::max<LONG>( 0, m_client.right - m_client.left ) );
}

std::uint32_t window_tracker::height( ) const
{
	return static_cast<std::uint32_t>( std::max<LONG>( 0, m_client.bottom - m_client.top ) );
}

void CALLBACK window_tracker::event_callback(
	HWINEVENTHOOK,
	const DWORD event,
	const HWND window,
	const LONG object_id,
	const LONG child_id,
	DWORD,
	DWORD )
{
	auto* tracker = s_active.load( std::memory_order_acquire );
	if ( !tracker )
		return;
	if ( event == EVENT_SYSTEM_FOREGROUND )
	{
		DWORD foreground_process{};
		if ( window )
			::GetWindowThreadProcessId( window, &foreground_process );
		if ( foreground_process != tracker->m_process_id )
			tracker->m_force_hidden_pending.store( true, std::memory_order_release );
		tracker->m_visibility_pending.store( true, std::memory_order_relaxed );
		tracker->m_z_order_pending.store( true, std::memory_order_relaxed );
		tracker->m_transition_revision.fetch_add( 1, std::memory_order_release );
		return;
	}

	if ( object_id != OBJID_WINDOW || child_id != CHILDID_SELF )
		return;
	const bool dxgi_proxy = window && ( window == tracker->m_dxgi_proxy
		|| is_dxgi_proxy_window( window, tracker->m_process_id ) );
	if ( dxgi_proxy )
	{
		if ( event == EVENT_OBJECT_DESTROY )
			tracker->m_dxgi_proxy = nullptr;
		else
			tracker->m_dxgi_proxy = window;
		tracker->m_geometry_pending.store( true, std::memory_order_relaxed );
		tracker->m_visibility_pending.store( true, std::memory_order_relaxed );
		tracker->m_z_order_pending.store( true, std::memory_order_relaxed );
		tracker->m_transition_revision.fetch_add( 1, std::memory_order_release );
		return;
	}

	if ( event == EVENT_OBJECT_DESTROY && window == tracker->m_target )
	{
		tracker->m_force_hidden_pending.store( true, std::memory_order_release );
		tracker->m_visibility_pending.store( true, std::memory_order_relaxed );
		tracker->m_reacquire_pending.store( true, std::memory_order_relaxed );
		tracker->m_transition_revision.fetch_add( 1, std::memory_order_release );
		return;
	}
	if ( ( event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW )
		&& window != tracker->m_target )
	{
		tracker->m_reacquire_pending.store( true, std::memory_order_relaxed );
	}
	if ( window != tracker->m_target )
		return;
	if ( event == EVENT_SYSTEM_MINIMIZESTART
		|| event == EVENT_OBJECT_HIDE || event == EVENT_OBJECT_CLOAKED )
	{
		tracker->m_force_hidden_pending.store( true, std::memory_order_release );
	}

	tracker->m_geometry_pending.store( true, std::memory_order_relaxed );
	tracker->m_visibility_pending.store( true, std::memory_order_relaxed );
	tracker->m_z_order_pending.store( true, std::memory_order_relaxed );
	tracker->m_transition_revision.fetch_add( 1, std::memory_order_release );
}

HWND window_tracker::find_target( ) const
{
	window_search search{ m_process_id };
	::EnumWindows( enum_window, reinterpret_cast<LPARAM>( &search ) );
	if ( search.best )
		return search.best;

	const HWND fallback = ::FindWindowW( L"SDL_app", nullptr );
	if ( is_game_window( fallback, m_process_id ) )
	{
		return fallback;
	}
	return nullptr;
}

bool window_tracker::refresh_geometry( bool& changed )
{
	changed = false;
	if ( !m_target || !::IsWindow( m_target ) )
		return false;
	if ( ::IsIconic( m_target ) )
	{
		changed = width( ) != 0 || height( ) != 0;
		m_client = {};
		return true;
	}

	RECT client{};
	if ( !::GetClientRect( m_target, &client ) )
		return false;
	POINT origin{ client.left, client.top };
	if ( !::ClientToScreen( m_target, &origin ) )
		return false;

	const LONG width = client.right - client.left;
	const LONG height = client.bottom - client.top;
	if ( width <= 0 || height <= 0 )
	{
		changed = this->width( ) != 0 || this->height( ) != 0;
		m_client = {};
		return true;
	}
	RECT screen{ origin.x, origin.y, origin.x + width, origin.y + height };
	changed = !same_rect( m_client, screen );
	m_client = screen;
	if ( const auto monitor = ::MonitorFromRect( &screen, MONITOR_DEFAULTTONULL ) )
	{
		MONITORINFOEXW information{};
		information.cbSize = sizeof( information );
		if ( ::GetMonitorInfoW( monitor, &information ) )
		{
			DEVMODEW mode{};
			mode.dmSize = sizeof( mode );
			if ( ::EnumDisplaySettingsW(
				information.szDevice, ENUM_CURRENT_SETTINGS, &mode )
				&& mode.dmDisplayFrequency > 1 )
			{
				m_refresh_rate = mode.dmDisplayFrequency;
			}
		}
	}
	return true;
}

bool window_tracker::calculate_visibility( ) const
{
	if ( !m_target || width( ) == 0 || height( ) == 0 )
		return false;
	if ( !::IsWindowVisible( m_target ) || ::IsIconic( m_target ) )
		return false;
	WINDOWPLACEMENT placement{ .length = sizeof( placement ) };
	if ( !::GetWindowPlacement( m_target, &placement )
		|| placement.showCmd == SW_SHOWMINIMIZED
		|| placement.showCmd == SW_MINIMIZE
		|| placement.showCmd == SW_SHOWMINNOACTIVE )
	{
		return false;
	}
	BOOL cloaked{};
	if ( SUCCEEDED( ::DwmGetWindowAttribute(
		m_target, DWMWA_CLOAKED, &cloaked, sizeof( cloaked ) ) ) && cloaked )
	{
		return false;
	}

	const HWND foreground = ::GetForegroundWindow( );
	DWORD foreground_process{};
	if ( foreground )
		::GetWindowThreadProcessId( foreground, &foreground_process );
	if ( foreground_process != m_process_id )
		return false;

	constexpr LONG minimized_sentinel = -30000;
	if ( m_client.left <= minimized_sentinel || m_client.top <= minimized_sentinel )
		return false;

	const auto monitor = ::MonitorFromWindow( m_target, MONITOR_DEFAULTTONULL );
	MONITORINFOEXW information{};
	information.cbSize = sizeof( information );
	if ( !monitor || !::GetMonitorInfoW( monitor, &information ) )
		return false;
	RECT intersection{};
	if ( !::IntersectRect( &intersection, &m_client, &information.rcMonitor )
		|| intersection.right <= intersection.left
		|| intersection.bottom <= intersection.top )
	{
		return false;
	}

	const auto extended_style = static_cast<DWORD_PTR>(
		::GetWindowLongPtrW( m_target, GWL_EXSTYLE ) );
	const bool has_dxgi_proxy =
		( m_dxgi_proxy && ::IsWindow( m_dxgi_proxy ) )
		|| find_dxgi_proxy_window( m_process_id );
	const bool exclusive_transition =
		( extended_style & WS_EX_TOPMOST ) != 0 || has_dxgi_proxy;
	if ( exclusive_transition )
	{
		DEVMODEW mode{ .dmSize = sizeof( mode ) };
		if ( !::EnumDisplaySettingsW(
			information.szDevice, ENUM_CURRENT_SETTINGS, &mode ) )
		{
			return false;
		}
		constexpr LONG tolerance = 2;
		const auto client_width = static_cast<LONG>( width( ) );
		const auto client_height = static_cast<LONG>( height( ) );
		if ( std::abs( client_width - static_cast<LONG>( mode.dmPelsWidth ) ) > tolerance
			|| std::abs( client_height - static_cast<LONG>( mode.dmPelsHeight ) ) > tolerance )
		{
			return false;
		}

		if ( !( extended_style & WS_EX_TOPMOST ) || !has_dxgi_proxy )
		{
			return false;
		}
	}
	return true;
}

void window_tracker::install_hooks( )
{
	constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
	m_foreground_hook = ::SetWinEventHook(
		EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
		nullptr, event_callback, 0, 0, flags );
	m_location_hook = ::SetWinEventHook(
		EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
		nullptr, event_callback, m_process_id, 0, flags );
	m_lifetime_hook = ::SetWinEventHook(
		EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
		nullptr, event_callback, m_process_id, 0, flags );
	m_minimize_hook = ::SetWinEventHook(
		EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
		nullptr, event_callback, m_process_id, 0, flags );
	m_cloak_hook = ::SetWinEventHook(
		EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED,
		nullptr, event_callback, m_process_id, 0, flags );
}

void window_tracker::remove_hooks( )
{
	if ( m_foreground_hook ) ::UnhookWinEvent( m_foreground_hook );
	if ( m_location_hook ) ::UnhookWinEvent( m_location_hook );
	if ( m_lifetime_hook ) ::UnhookWinEvent( m_lifetime_hook );
	if ( m_minimize_hook ) ::UnhookWinEvent( m_minimize_hook );
	if ( m_cloak_hook ) ::UnhookWinEvent( m_cloak_hook );
	m_foreground_hook = nullptr;
	m_location_hook = nullptr;
	m_lifetime_hook = nullptr;
	m_minimize_hook = nullptr;
	m_cloak_hook = nullptr;
}
