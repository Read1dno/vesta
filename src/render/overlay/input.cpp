#include <stdafx.hpp>

#include <render/overlay/input.hpp>

#include "imgui.h"

namespace
{
	[[nodiscard]] HWND active_root_window( )
	{
		const HWND foreground = ::GetForegroundWindow( );
		return foreground ? ::GetAncestor( foreground, GA_ROOT ) : nullptr;
	}

	constexpr UINT k_capture_changed_message = WM_APP + 0x41;

	ImGuiKey map_virtual_key( const int key )
	{
		if ( key >= '0' && key <= '9' )
			return static_cast<ImGuiKey>( ImGuiKey_0 + key - '0' );
		if ( key >= 'A' && key <= 'Z' )
			return static_cast<ImGuiKey>( ImGuiKey_A + key - 'A' );
		if ( key >= VK_F1 && key <= VK_F24 )
			return static_cast<ImGuiKey>( ImGuiKey_F1 + key - VK_F1 );
		switch ( key )
		{
		case VK_TAB: return ImGuiKey_Tab;
		case VK_LEFT: return ImGuiKey_LeftArrow;
		case VK_RIGHT: return ImGuiKey_RightArrow;
		case VK_UP: return ImGuiKey_UpArrow;
		case VK_DOWN: return ImGuiKey_DownArrow;
		case VK_PRIOR: return ImGuiKey_PageUp;
		case VK_NEXT: return ImGuiKey_PageDown;
		case VK_HOME: return ImGuiKey_Home;
		case VK_END: return ImGuiKey_End;
		case VK_INSERT: return ImGuiKey_Insert;
		case VK_DELETE: return ImGuiKey_Delete;
		case VK_BACK: return ImGuiKey_Backspace;
		case VK_SPACE: return ImGuiKey_Space;
		case VK_RETURN: return ImGuiKey_Enter;
		case VK_ESCAPE: return ImGuiKey_Escape;
		case VK_OEM_7: return ImGuiKey_Apostrophe;
		case VK_OEM_COMMA: return ImGuiKey_Comma;
		case VK_OEM_MINUS: return ImGuiKey_Minus;
		case VK_OEM_PERIOD: return ImGuiKey_Period;
		case VK_OEM_2: return ImGuiKey_Slash;
		case VK_OEM_1: return ImGuiKey_Semicolon;
		case VK_OEM_PLUS: return ImGuiKey_Equal;
		case VK_OEM_4: return ImGuiKey_LeftBracket;
		case VK_OEM_5: return ImGuiKey_Backslash;
		case VK_OEM_6: return ImGuiKey_RightBracket;
		case VK_OEM_3: return ImGuiKey_GraveAccent;
		case VK_CAPITAL: return ImGuiKey_CapsLock;
		case VK_SCROLL: return ImGuiKey_ScrollLock;
		case VK_NUMLOCK: return ImGuiKey_NumLock;
		case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
		case VK_PAUSE: return ImGuiKey_Pause;
		case VK_NUMPAD0: return ImGuiKey_Keypad0;
		case VK_NUMPAD1: return ImGuiKey_Keypad1;
		case VK_NUMPAD2: return ImGuiKey_Keypad2;
		case VK_NUMPAD3: return ImGuiKey_Keypad3;
		case VK_NUMPAD4: return ImGuiKey_Keypad4;
		case VK_NUMPAD5: return ImGuiKey_Keypad5;
		case VK_NUMPAD6: return ImGuiKey_Keypad6;
		case VK_NUMPAD7: return ImGuiKey_Keypad7;
		case VK_NUMPAD8: return ImGuiKey_Keypad8;
		case VK_NUMPAD9: return ImGuiKey_Keypad9;
		case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
		case VK_DIVIDE: return ImGuiKey_KeypadDivide;
		case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
		case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
		case VK_ADD: return ImGuiKey_KeypadAdd;
		case VK_SHIFT: return ImGuiKey_LeftShift;
		case VK_CONTROL: return ImGuiKey_LeftCtrl;
		case VK_MENU: return ImGuiKey_LeftAlt;
		case VK_LSHIFT: return ImGuiKey_LeftShift;
		case VK_LCONTROL: return ImGuiKey_LeftCtrl;
		case VK_LMENU: return ImGuiKey_LeftAlt;
		case VK_LWIN: return ImGuiKey_LeftSuper;
		case VK_RSHIFT: return ImGuiKey_RightShift;
		case VK_RCONTROL: return ImGuiKey_RightCtrl;
		case VK_RMENU: return ImGuiKey_RightAlt;
		case VK_RWIN: return ImGuiKey_RightSuper;
		default: return ImGuiKey_None;
		}
	}

	[[nodiscard]] bool text_key( const int key )
	{
		return ( key >= '0' && key <= '9' ) || ( key >= 'A' && key <= 'Z' )
			|| key == VK_SPACE || ( key >= VK_OEM_1 && key <= VK_OEM_3 )
			|| ( key >= VK_OEM_4 && key <= VK_OEM_7 )
			|| key == VK_OEM_PLUS || key == VK_OEM_COMMA
			|| key == VK_OEM_MINUS || key == VK_OEM_PERIOD;
	}
}

ImGuiKey overlay_input::key_from_virtual_key( const int key )
{
	return map_virtual_key( key );
}

input_router::~input_router( )
{
	shutdown( );
}

bool input_router::initialize( )
{
	if ( m_thread.joinable( ) )
		return m_ready.load( std::memory_order_acquire );
	s_active.store( this, std::memory_order_release );
	m_thread = std::jthread( [this]( const std::stop_token stop ) { thread_main( stop ); } );
	for ( int attempt = 0; attempt < 500 && !m_ready.load( std::memory_order_acquire ); ++attempt )
		::Sleep( 1 );
	return m_ready.load( std::memory_order_acquire );
}

void input_router::shutdown( )
{
	set_capture( false, nullptr );
	if ( m_thread.joinable( ) )
	{
		m_thread.request_stop( );
		const DWORD thread_id = m_thread_id.load( std::memory_order_acquire );
		if ( thread_id )
			::PostThreadMessageW( thread_id, WM_QUIT, 0, 0 );
		m_thread.join( );
	}
	input_router* expected = this;
	s_active.compare_exchange_strong( expected, nullptr, std::memory_order_acq_rel );
}

void input_router::set_capture(
	const bool enabled, const HWND foreground )
{
	m_capture_foreground.store( foreground, std::memory_order_relaxed );
	const bool previous = m_capture.exchange( enabled, std::memory_order_acq_rel );
	if ( previous && !enabled )
	{
		for ( auto& key : m_keys )
			key.store( false, std::memory_order_relaxed );
		for ( auto& pressed : m_key_pressed )
			pressed.store( false, std::memory_order_relaxed );
		for ( auto& button : m_mouse_buttons )
			button.store( false, std::memory_order_relaxed );
		for ( auto& pressed : m_mouse_pressed )
			pressed.store( false, std::memory_order_relaxed );
		m_wheel.store( 0, std::memory_order_relaxed );
		m_wheel_horizontal.store( 0, std::memory_order_relaxed );
	}
	if ( previous != enabled )
	{
		if ( const auto thread_id = m_thread_id.load( std::memory_order_acquire ) )
			::PostThreadMessageW( thread_id, k_capture_changed_message, 0, 0 );
	}
}

void input_router::pump_imgui(
	const int origin_x, const int origin_y, const bool pump_pointer )
{
	auto& io = ImGui::GetIO( );
	io.MouseDrawCursor = false;
	if ( !m_capture.load( std::memory_order_relaxed ) )
		return;

	POINT cursor{};
	if ( pump_pointer && ::GetCursorPos( &cursor ) )
	{
		io.AddMousePosEvent(
			static_cast<float>( cursor.x - origin_x ),
			static_cast<float>( cursor.y - origin_y ) );
	}
	for ( int button = 0; button < static_cast<int>( m_mouse_buttons.size( ) ); ++button )
	{
		const bool down = m_mouse_buttons[ button ].load( std::memory_order_relaxed );
		const bool pressed = m_mouse_pressed[ button ].exchange( false, std::memory_order_acq_rel );
		if ( down != m_reported_mouse_buttons[ button ] )
		{
			io.AddMouseButtonEvent( button, down );
			m_reported_mouse_buttons[ button ] = down;
		}
		else if ( pressed && !down )
		{

			io.AddMouseButtonEvent( button, true );
			io.AddMouseButtonEvent( button, false );
		}
	}
	const LONG wheel = m_wheel.exchange( 0, std::memory_order_acq_rel );
	const LONG horizontal = m_wheel_horizontal.exchange( 0, std::memory_order_acq_rel );
	if ( wheel || horizontal )
	{
		io.AddMouseWheelEvent(
			static_cast<float>( horizontal ) / WHEEL_DELTA,
			static_cast<float>( wheel ) / WHEEL_DELTA );
	}

	BYTE keyboard_state[ 256 ]{};
	for ( int key = 0; key < 256; ++key )
	{
		const bool down = m_keys[ key ].load( std::memory_order_relaxed );
		const bool pressed = m_key_pressed[ key ].exchange( false, std::memory_order_acq_rel );
		if ( down ) keyboard_state[ key ] = 0x80;
		const bool was_down = m_reported_keys[ key ];
		if ( down != was_down )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None )
				io.AddKeyEvent( imgui_key, down );
			m_reported_keys[ key ] = down;
			if ( down && text_key( key ) )
			{
				wchar_t characters[ 8 ]{};
				const auto scan = ::MapVirtualKeyW( key, MAPVK_VK_TO_VSC );
				const auto count = ::ToUnicodeEx(
					key, scan, keyboard_state, characters,
					static_cast<int>( std::size( characters ) ), 0,
					::GetKeyboardLayout( 0 ) );
				for ( int index = 0; index < count; ++index )
					io.AddInputCharacterUTF16( characters[ index ] );
			}
		}
		else if ( pressed && !down )
		{
			const auto imgui_key = overlay_input::key_from_virtual_key( key );
			if ( imgui_key != ImGuiKey_None )
			{
				io.AddKeyEvent( imgui_key, true );
				io.AddKeyEvent( imgui_key, false );
			}
		}
	}
	io.AddKeyEvent( ImGuiMod_Ctrl,
		m_keys[ VK_CONTROL ].load( ) || m_keys[ VK_LCONTROL ].load( ) || m_keys[ VK_RCONTROL ].load( ) );
	io.AddKeyEvent( ImGuiMod_Shift,
		m_keys[ VK_SHIFT ].load( ) || m_keys[ VK_LSHIFT ].load( ) || m_keys[ VK_RSHIFT ].load( ) );
	io.AddKeyEvent( ImGuiMod_Alt,
		m_keys[ VK_MENU ].load( ) || m_keys[ VK_LMENU ].load( ) || m_keys[ VK_RMENU ].load( ) );
}

LRESULT CALLBACK input_router::keyboard_proc(
	const int code, const WPARAM message, const LPARAM parameter )
{
	auto* router = s_active.load( std::memory_order_acquire );
	if ( code < HC_ACTION || !router )
		return ::CallNextHookEx( nullptr, code, message, parameter );

	const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>( parameter );
	const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
	const bool capture = router->m_capture.load( std::memory_order_relaxed )
		&& router->m_capture_foreground.load( std::memory_order_relaxed )
			== active_root_window( );
	const bool injected = ( event.flags & LLKHF_INJECTED ) != 0;
	if ( capture && !injected && ( down || up )
		&& event.vkCode < router->m_keys.size( ) )
	{
		const bool previous = router->m_keys[ event.vkCode ].exchange(
			down, std::memory_order_acq_rel );
		if ( down && !previous )
			router->m_key_pressed[ event.vkCode ].store( true, std::memory_order_release );
	}
	if ( !capture
		|| injected
		|| passthrough_key( event.vkCode ) )
	{
		return ::CallNextHookEx( nullptr, code, message, parameter );
	}
	return 1;
}

LRESULT CALLBACK input_router::mouse_proc(
	const int code, const WPARAM message, const LPARAM parameter )
{
	auto* router = s_active.load( std::memory_order_acquire );
	if ( code < HC_ACTION || !router )
		return ::CallNextHookEx( nullptr, code, message, parameter );

	const auto& event = *reinterpret_cast<const MSLLHOOKSTRUCT*>( parameter );
	const bool capture = router->m_capture.load( std::memory_order_relaxed )
		&& router->m_capture_foreground.load( std::memory_order_relaxed )
			== active_root_window( );
	const bool injected = ( event.flags & LLMHF_INJECTED ) != 0;
	if ( capture && !injected )
	{
		switch ( message )
		{
		case WM_LBUTTONDOWN:
			if ( !router->m_mouse_buttons[ 0 ].exchange( true ) ) router->m_mouse_pressed[ 0 ].store( true );
			break;
		case WM_LBUTTONUP: router->m_mouse_buttons[ 0 ].store( false ); break;
		case WM_RBUTTONDOWN:
			if ( !router->m_mouse_buttons[ 1 ].exchange( true ) ) router->m_mouse_pressed[ 1 ].store( true );
			break;
		case WM_RBUTTONUP: router->m_mouse_buttons[ 1 ].store( false ); break;
		case WM_MBUTTONDOWN:
			if ( !router->m_mouse_buttons[ 2 ].exchange( true ) ) router->m_mouse_pressed[ 2 ].store( true );
			break;
		case WM_MBUTTONUP: router->m_mouse_buttons[ 2 ].store( false ); break;
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		{
			const int button = HIWORD( event.mouseData ) == XBUTTON1 ? 3 : 4;
			const bool button_down = message == WM_XBUTTONDOWN;
			const bool previous = router->m_mouse_buttons[ button ].exchange( button_down );
			if ( button_down && !previous ) router->m_mouse_pressed[ button ].store( true );
			break;
		}
		case WM_MOUSEWHEEL:
			router->m_wheel.fetch_add(
				static_cast<SHORT>( HIWORD( event.mouseData ) ), std::memory_order_relaxed );
			break;
		case WM_MOUSEHWHEEL:
			router->m_wheel_horizontal.fetch_add(
				static_cast<SHORT>( HIWORD( event.mouseData ) ), std::memory_order_relaxed );
			break;
		default: break;
		}
	}
	if ( !capture
		|| injected )
	{
		return ::CallNextHookEx( nullptr, code, message, parameter );
	}
	if ( message == WM_MOUSEMOVE )
		return ::CallNextHookEx( nullptr, code, message, parameter );
	return 1;
}

void input_router::thread_main( const std::stop_token stop )
{
	m_thread_id.store( ::GetCurrentThreadId( ), std::memory_order_release );
	::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_NORMAL );
	MSG message{};
	::PeekMessageW( &message, nullptr, WM_USER, WM_USER, PM_NOREMOVE );
	const auto synchronize_hooks = [ this ]( )
	{
		if ( m_capture.load( std::memory_order_acquire ) )
		{
			if ( !m_keyboard_hook )
				m_keyboard_hook = ::SetWindowsHookExW(
					WH_KEYBOARD_LL, keyboard_proc, ::GetModuleHandleW( nullptr ), 0 );
			if ( !m_mouse_hook )
				m_mouse_hook = ::SetWindowsHookExW(
					WH_MOUSE_LL, mouse_proc, ::GetModuleHandleW( nullptr ), 0 );
			const auto ready = m_keyboard_hook && m_mouse_hook;
			if ( !ready )
			{

				if ( m_mouse_hook ) ::UnhookWindowsHookEx( m_mouse_hook );
				if ( m_keyboard_hook ) ::UnhookWindowsHookEx( m_keyboard_hook );
				m_mouse_hook = nullptr;
				m_keyboard_hook = nullptr;
			}
			m_hooks_ready.store( ready, std::memory_order_release );
			return;
		}

		if ( m_mouse_hook ) ::UnhookWindowsHookEx( m_mouse_hook );
		if ( m_keyboard_hook ) ::UnhookWindowsHookEx( m_keyboard_hook );
		m_mouse_hook = nullptr;
		m_keyboard_hook = nullptr;
		m_hooks_ready.store( false, std::memory_order_release );
	};
	m_ready.store( true, std::memory_order_release );
	while ( !stop.stop_requested( ) && ::GetMessageW( &message, nullptr, 0, 0 ) > 0 )
	{
		if ( message.message == k_capture_changed_message )
		{
			synchronize_hooks( );
			continue;
		}
		::TranslateMessage( &message );
		::DispatchMessageW( &message );
	}
	if ( m_mouse_hook ) ::UnhookWindowsHookEx( m_mouse_hook );
	if ( m_keyboard_hook ) ::UnhookWindowsHookEx( m_keyboard_hook );
	m_mouse_hook = nullptr;
	m_keyboard_hook = nullptr;
	m_hooks_ready.store( false, std::memory_order_release );
}

bool input_router::passthrough_key( const std::uint32_t key )
{

	return key == VK_ESCAPE || key == VK_INSERT || key == VK_END || key == VK_TAB
		|| key == VK_MENU || key == VK_LMENU || key == VK_RMENU
		|| key == VK_LWIN || key == VK_RWIN;
}
