#include <stdafx.hpp>
#include <core/input/hotkeys.hpp>

namespace platform::windows {

	namespace {
		constexpr UINT k_gate_changed_message = WM_APP + 0x321;

		[[nodiscard]] constexpr bool contains( pointer_action value,
			pointer_action flag ) noexcept
		{
			return ( static_cast<std::uint32_t>( value ) &
				static_cast<std::uint32_t>( flag ) ) != 0;
		}
	}

	input_gateway::~input_gateway( )
	{
		if ( m_injected_primary_down.load( std::memory_order_acquire ) )
			pointer( 0, 0, pointer_action::primary_up );
		std::array<key_transition, 8> releases{};
		std::size_t release_count{};
		for ( std::uint16_t key = 1; key < 256; ++key )
		{
			if ( !m_injected_down[ key ].load( std::memory_order_acquire ) ) continue;
			releases[ release_count++ ] = { key, false };
			if ( release_count == releases.size( ) )
			{
				keys( std::span{ releases }.first( release_count ) );
				release_count = 0;
			}
		}
		if ( release_count ) keys( std::span{ releases }.first( release_count ) );
		stop_key_gate( );
	}

	bool input_gateway::connect( ) noexcept
	{
		const auto library = ::GetModuleHandleW( L"win32u.dll" );
		if ( !library )
			return false;

		m_pointer_injector = reinterpret_cast<inject_pointer_fn>(
			::GetProcAddress( library, "NtUserInjectMouseInput" ) );
		m_key_injector = reinterpret_cast<inject_key_fn>(
			::GetProcAddress( library, "NtUserInjectKeyboardInput" ) );
		if ( !m_pointer_injector || !m_key_injector )
			return false;

		input_gateway* expected{};
		if ( !s_gate_owner.compare_exchange_strong(
			expected, this, std::memory_order_acq_rel ) )
		{
			return expected == this;
		}

		try
		{
			m_gate_thread = std::jthread(
				[this]( const std::stop_token stop ) { gate_thread_main( stop ); } );
		}
		catch ( ... )
		{
			input_gateway* owner = this;
			s_gate_owner.compare_exchange_strong(
				owner, nullptr, std::memory_order_acq_rel );
			return false;
		}
		for ( int attempt = 0;
			attempt < 500 && !m_gate_thread_ready.load( std::memory_order_acquire );
			++attempt )
		{
			::Sleep( 1 );
		}
		if ( !m_gate_thread_ready.load( std::memory_order_acquire ) )
		{
			stop_key_gate( );
			return false;
		}
		return true;
	}

	bool input_gateway::pointer( int dx, int dy,
		pointer_action actions ) const noexcept
	{
		if ( !m_pointer_injector )
			return false;

		pointer_packet packet{};
		packet.point = { dx, dy };
		if ( contains( actions, pointer_action::relative_move ) ) packet.flags |= MOUSEEVENTF_MOVE;
		if ( contains( actions, pointer_action::primary_down ) ) packet.flags |= MOUSEEVENTF_LEFTDOWN;
		if ( contains( actions, pointer_action::primary_up ) ) packet.flags |= MOUSEEVENTF_LEFTUP;
		if ( contains( actions, pointer_action::secondary_down ) ) packet.flags |= MOUSEEVENTF_RIGHTDOWN;
		if ( contains( actions, pointer_action::secondary_up ) ) packet.flags |= MOUSEEVENTF_RIGHTUP;
		if ( contains( actions, pointer_action::middle_down ) ) packet.flags |= MOUSEEVENTF_MIDDLEDOWN;
		if ( contains( actions, pointer_action::middle_up ) ) packet.flags |= MOUSEEVENTF_MIDDLEUP;
		if ( contains( actions, pointer_action::auxiliary1_down )
			|| contains( actions, pointer_action::auxiliary1_up ) )
		{
			packet.mouse_data = XBUTTON1;
			if ( contains( actions, pointer_action::auxiliary1_down ) ) packet.flags |= MOUSEEVENTF_XDOWN;
			if ( contains( actions, pointer_action::auxiliary1_up ) ) packet.flags |= MOUSEEVENTF_XUP;
		}
		if ( contains( actions, pointer_action::auxiliary2_down )
			|| contains( actions, pointer_action::auxiliary2_up ) )
		{
			packet.mouse_data = XBUTTON2;
			if ( contains( actions, pointer_action::auxiliary2_down ) ) packet.flags |= MOUSEEVENTF_XDOWN;
			if ( contains( actions, pointer_action::auxiliary2_up ) ) packet.flags |= MOUSEEVENTF_XUP;
		}
		const auto injected = m_pointer_injector( &packet, 1 ) != FALSE;
		if ( injected )
		{
			if ( contains( actions, pointer_action::primary_down ) )
				m_injected_primary_down.store( true, std::memory_order_release );
			if ( contains( actions, pointer_action::primary_up ) )
				m_injected_primary_down.store( false, std::memory_order_release );
		}
		return injected;
	}

	bool input_gateway::key( std::uint16_t virtual_key,
		bool pressed ) const noexcept
	{
		const std::array transitions{ key_transition{ virtual_key, pressed } };
		return keys( transitions );
	}

	bool input_gateway::keys(
		std::span<const key_transition> transitions ) const noexcept
	{
		if ( !m_key_injector || transitions.empty( ) || transitions.size( ) > 8 )
			return false;

		std::array<key_packet, 8> packets{};
		for ( auto index = std::size_t{}; index < transitions.size( ); ++index )
		{
			const auto transition = transitions[index];
			auto& packet = packets[index];
			packet.virtual_key = transition.virtual_key;
			packet.scan_code = static_cast<std::uint16_t>(
				::MapVirtualKeyW( transition.virtual_key, MAPVK_VK_TO_VSC ) );
			packet.flags = transition.pressed ? 0u : KEYEVENTF_KEYUP;
		}
		const auto injected = m_key_injector( packets.data( ),
			static_cast<int>( transitions.size( ) ) ) != FALSE;
		if ( injected )
		{
			for ( const auto& transition : transitions )
			{
				if ( transition.virtual_key < m_injected_down.size( ) )
					m_injected_down[ transition.virtual_key ].store(
						transition.pressed, std::memory_order_release );
			}
		}
		return injected;
	}

	void input_gateway::set_key_gate(
		const std::uint16_t virtual_key, const bool enabled ) noexcept
	{

		const auto protected_key = is_lifecycle_key( virtual_key );
		const auto next_key = enabled && !protected_key
			&& virtual_key > 0 && virtual_key < 256
			? virtual_key : std::uint16_t{};
		const auto next_enabled = next_key != 0;
		const auto previous_key = m_gate_key.exchange(
			next_key, std::memory_order_acq_rel );
		const auto previous_enabled = m_primary_gate_enabled.exchange(
			next_enabled, std::memory_order_acq_rel );
		m_gate_enabled.store( next_enabled
			|| m_movement_gate_count.load( std::memory_order_acquire ) > 0,
			std::memory_order_release );
		if ( previous_key == next_key && previous_enabled == next_enabled )
			return;

		m_gate_physical_down.store(
			next_enabled && ( ::GetAsyncKeyState( next_key ) & 0x8000 ) != 0,
			std::memory_order_release );
		m_gate_hook_ready.store( false, std::memory_order_release );
		if ( const auto thread_id = m_gate_thread_id.load( std::memory_order_acquire ) )
			::PostThreadMessageW( thread_id, k_gate_changed_message, 0, 0 );

		if ( next_enabled && m_gate_physical_down.load( std::memory_order_acquire ) )
			key( next_key, false );
	}

	void input_gateway::set_movement_gate(
		std::span<const std::uint16_t> virtual_keys, const bool enabled ) noexcept
	{
		std::array<bool, 256> desired{};
		if ( enabled )
		{
			for ( const auto key : virtual_keys )
			{
				if ( key > 0 && key < 256 && !is_lifecycle_key( key ) )
					desired[ key ] = true;
			}
		}

		std::uint16_t count{};
		bool changed{};
		for ( std::uint16_t key = 1; key < 256; ++key )
		{
			const auto was = m_movement_gated[ key ].exchange(
				desired[ key ], std::memory_order_acq_rel );
			changed = changed || was != desired[ key ];
			if ( desired[ key ] )
			{
				++count;
				if ( !was )
				{

					const auto physical =
						( ::GetAsyncKeyState( key ) & 0x8000 ) != 0;
					m_movement_physical_down[ key ].store(
						physical, std::memory_order_release );
					if ( physical ) this->key( key, false );
				}
			}
			else if ( was )
			{

				const auto physical = m_movement_physical_down[ key ].exchange(
					false, std::memory_order_acq_rel );
				if ( physical ) this->key( key, true );
			}
		}
		m_movement_gate_count.store( count, std::memory_order_release );
		m_gate_enabled.store(
			m_primary_gate_enabled.load( std::memory_order_acquire ) || count > 0,
			std::memory_order_release );
		if ( !changed ) return;
		m_gate_hook_ready.store( false, std::memory_order_release );
		if ( const auto thread_id = m_gate_thread_id.load( std::memory_order_acquire ) )
			::PostThreadMessageW( thread_id, k_gate_changed_message, 0, 0 );
	}

	bool input_gateway::physical_key_down(
		const std::uint16_t virtual_key ) const noexcept
	{
		if ( virtual_key == 0 || virtual_key >= m_movement_gated.size( ) )
			return false;
		if ( m_primary_gate_enabled.load( std::memory_order_acquire )
			&& m_gate_key.load( std::memory_order_acquire ) == virtual_key )
		{
			return m_gate_physical_down.load( std::memory_order_acquire );
		}
		if ( m_movement_gated[ virtual_key ].load( std::memory_order_acquire ) )
		{
			return m_movement_physical_down[ virtual_key ].load(
				std::memory_order_acquire );
		}
		return ( ::GetAsyncKeyState( virtual_key ) & 0x8000 ) != 0;
	}

	LRESULT CALLBACK input_gateway::gate_proc(
		const int code, const WPARAM message, const LPARAM parameter )
	{
		auto* gateway = s_gate_owner.load( std::memory_order_acquire );
		if ( code < HC_ACTION || !gateway )
			return ::CallNextHookEx( nullptr, code, message, parameter );

		const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>( parameter );
		const auto key = gateway->m_gate_key.load( std::memory_order_relaxed );
		const bool enabled = gateway->m_primary_gate_enabled.load( std::memory_order_relaxed );
		const bool injected = ( event.flags & LLKHF_INJECTED ) != 0;
		const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
		if ( enabled && !injected && event.vkCode == key && ( down || up ) )
		{
			gateway->m_gate_physical_down.store( down, std::memory_order_release );
			return 1;
		}
		if ( !injected && event.vkCode < 256 && ( down || up )
			&& gateway->m_movement_gated[ event.vkCode ].load(
				std::memory_order_relaxed ) )
		{
			gateway->m_movement_physical_down[ event.vkCode ].store(
				down, std::memory_order_release );
			return 1;
		}
		return ::CallNextHookEx( nullptr, code, message, parameter );
	}

	void input_gateway::gate_thread_main( const std::stop_token stop )
	{
		m_gate_thread_id.store( ::GetCurrentThreadId( ), std::memory_order_release );
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_ABOVE_NORMAL );
		MSG message{};
		::PeekMessageW( &message, nullptr, WM_USER, WM_USER, PM_NOREMOVE );

		const auto synchronize_hook = [this]
		{
			const bool requested = m_gate_enabled.load( std::memory_order_acquire );
			if ( requested && !m_gate_hook )
			{
				m_gate_hook = ::SetWindowsHookExW(
					WH_KEYBOARD_LL, gate_proc, ::GetModuleHandleW( nullptr ), 0 );
			}
			else if ( !requested && m_gate_hook )
			{
				::UnhookWindowsHookEx( m_gate_hook );
				m_gate_hook = nullptr;
			}
			m_gate_hook_ready.store(
				requested && m_gate_hook != nullptr, std::memory_order_release );
		};

		m_gate_thread_ready.store( true, std::memory_order_release );
		synchronize_hook( );
		while ( !stop.stop_requested( )
			&& ::GetMessageW( &message, nullptr, 0, 0 ) > 0 )
		{
			if ( message.message == k_gate_changed_message )
			{
				synchronize_hook( );
				continue;
			}
			::TranslateMessage( &message );
			::DispatchMessageW( &message );
		}

		if ( m_gate_hook )
		{
			::UnhookWindowsHookEx( m_gate_hook );
			m_gate_hook = nullptr;
		}
		m_gate_hook_ready.store( false, std::memory_order_release );
		m_gate_thread_ready.store( false, std::memory_order_release );
	}

	void input_gateway::stop_key_gate( ) noexcept
	{
		m_gate_enabled.store( false, std::memory_order_release );
		m_primary_gate_enabled.store( false, std::memory_order_release );
		m_gate_key.store( 0, std::memory_order_release );
		m_gate_physical_down.store( false, std::memory_order_release );
		m_movement_gate_count.store( 0, std::memory_order_release );
		for ( std::uint16_t key = 1; key < 256; ++key )
		{
			m_movement_gated[ key ].store( false, std::memory_order_release );
			m_movement_physical_down[ key ].store( false, std::memory_order_release );
		}
		if ( m_gate_thread.joinable( ) )
		{
			m_gate_thread.request_stop( );
			if ( const auto thread_id = m_gate_thread_id.load( std::memory_order_acquire ) )
				::PostThreadMessageW( thread_id, WM_QUIT, 0, 0 );
			m_gate_thread.join( );
		}
		input_gateway* expected = this;
		s_gate_owner.compare_exchange_strong(
			expected, nullptr, std::memory_order_acq_rel );
	}

}
