#include <stdafx.hpp>
#include <core/input/bindings.hpp>

namespace game {

	namespace {

		constexpr std::size_t k_input_code_count{ 0x204 };
		constexpr std::size_t k_binding_record_size{ 0x58 };

		constexpr std::size_t k_binding_slot_count{ 11 };

		constexpr std::size_t k_input_name_bias{ 1 };
		constexpr std::uintptr_t k_current_binding_offset{ 0x33050 };
		constexpr std::uintptr_t k_current_key_name_table_rva{ 0x3a210 };

		constexpr std::array<std::array<std::string_view, 2>, 9> k_command_tokens{ {
			{ "+forward", {} },
			{ "+back", {} },
			{ "+moveleft", "+left" },
			{ "+moveright", "+right" },
			{ "+speed", {} },
			{ "+duck", {} },
			{ "+jump", {} },
			{ "+attack", {} },
			{ "+attack2", {} },
		} };

		[[nodiscard]] bool contains_command(
			std::string_view command, std::string_view token )
		{
			if ( token.empty( ) )
				return false;

			const auto delimiter = [ ]( const char value )
			{
				return std::isspace( static_cast<unsigned char>( value ) ) != 0
					|| value == ';' || value == '"';
			};
			for ( auto cursor = std::size_t{};
				cursor + token.size( ) <= command.size( ); ++cursor )
			{
				auto matches = true;
				for ( auto index = std::size_t{}; index < token.size( ); ++index )
				{
					const auto character = static_cast<char>( std::tolower(
						static_cast<unsigned char>( command[cursor + index] ) ) );
					if ( character != token[index] )
					{
						matches = false;
						break;
					}
				}
				if ( !matches )
					continue;

				const auto before = cursor == 0 || delimiter( command[cursor - 1] );
				const auto after = cursor + token.size( ) == command.size( )
					|| delimiter( command[cursor + token.size( )] );
				if ( before && after )
					return true;
			}
			return false;
		}

		[[nodiscard]] input_binding source_key_to_binding( std::string_view name )
		{
			std::string normalized{ name };
			std::ranges::transform( normalized, normalized.begin( ), [ ]( const char value )
			{
				return static_cast<char>( std::toupper(
					static_cast<unsigned char>( value ) ) );
			} );

			const auto mouse = [ &normalized ]( const input_device device )
			{
				return input_binding{ device, 0, normalized };
			};
			if ( normalized == "MOUSE1" ) return mouse( input_device::mouse_primary );
			if ( normalized == "MOUSE2" ) return mouse( input_device::mouse_secondary );
			if ( normalized == "MOUSE3" ) return mouse( input_device::mouse_middle );
			if ( normalized == "MOUSE4" ) return mouse( input_device::mouse_auxiliary1 );
			if ( normalized == "MOUSE5" ) return mouse( input_device::mouse_auxiliary2 );

			std::uint16_t virtual_key{};
			if ( normalized.size( ) == 1 )
			{
				const auto value = normalized.front( );
				if ( ( value >= 'A' && value <= 'Z' )
					|| ( value >= '0' && value <= '9' ) )
				{
					virtual_key = static_cast<std::uint16_t>( value );
				}
			}
			if ( !virtual_key && normalized.size( ) >= 2 && normalized.front( ) == 'F' )
			{
				auto number = 0;
				for ( auto index = std::size_t{ 1 }; index < normalized.size( ); ++index )
				{
					if ( normalized[index] < '0' || normalized[index] > '9' )
					{
						number = 0;
						break;
					}
					number = number * 10 + normalized[index] - '0';
				}
				if ( number >= 1 && number <= 24 )
					virtual_key = static_cast<std::uint16_t>( VK_F1 + number - 1 );
			}

			static constexpr std::array named_keys{
				std::pair{ std::string_view{ "SPACE" }, std::uint16_t{ VK_SPACE } },
				std::pair{ std::string_view{ "TAB" }, std::uint16_t{ VK_TAB } },
				std::pair{ std::string_view{ "ENTER" }, std::uint16_t{ VK_RETURN } },
				std::pair{ std::string_view{ "ESCAPE" }, std::uint16_t{ VK_ESCAPE } },
				std::pair{ std::string_view{ "BACKSPACE" }, std::uint16_t{ VK_BACK } },
				std::pair{ std::string_view{ "CAPSLOCK" }, std::uint16_t{ VK_CAPITAL } },
				std::pair{ std::string_view{ "NUMLOCK" }, std::uint16_t{ VK_NUMLOCK } },
				std::pair{ std::string_view{ "SCROLLLOCK" }, std::uint16_t{ VK_SCROLL } },
				std::pair{ std::string_view{ "INSERT" }, std::uint16_t{ VK_INSERT } },
				std::pair{ std::string_view{ "DELETE" }, std::uint16_t{ VK_DELETE } },
				std::pair{ std::string_view{ "HOME" }, std::uint16_t{ VK_HOME } },
				std::pair{ std::string_view{ "END" }, std::uint16_t{ VK_END } },
				std::pair{ std::string_view{ "PGUP" }, std::uint16_t{ VK_PRIOR } },
				std::pair{ std::string_view{ "PGDN" }, std::uint16_t{ VK_NEXT } },
				std::pair{ std::string_view{ "PAUSE" }, std::uint16_t{ VK_PAUSE } },
				std::pair{ std::string_view{ "SHIFT" }, std::uint16_t{ VK_LSHIFT } },
				std::pair{ std::string_view{ "RSHIFT" }, std::uint16_t{ VK_RSHIFT } },
				std::pair{ std::string_view{ "CTRL" }, std::uint16_t{ VK_LCONTROL } },
				std::pair{ std::string_view{ "RCTRL" }, std::uint16_t{ VK_RCONTROL } },
				std::pair{ std::string_view{ "ALT" }, std::uint16_t{ VK_LMENU } },
				std::pair{ std::string_view{ "RALT" }, std::uint16_t{ VK_RMENU } },
				std::pair{ std::string_view{ "UPARROW" }, std::uint16_t{ VK_UP } },
				std::pair{ std::string_view{ "DOWNARROW" }, std::uint16_t{ VK_DOWN } },
				std::pair{ std::string_view{ "LEFTARROW" }, std::uint16_t{ VK_LEFT } },
				std::pair{ std::string_view{ "RIGHTARROW" }, std::uint16_t{ VK_RIGHT } },
				std::pair{ std::string_view{ "SEMICOLON" }, std::uint16_t{ VK_OEM_1 } },
				std::pair{ std::string_view{ "SLASH" }, std::uint16_t{ VK_OEM_2 } },
				std::pair{ std::string_view{ "BACKQUOTE" }, std::uint16_t{ VK_OEM_3 } },
				std::pair{ std::string_view{ "LBRACKET" }, std::uint16_t{ VK_OEM_4 } },
				std::pair{ std::string_view{ "BACKSLASH" }, std::uint16_t{ VK_OEM_5 } },
				std::pair{ std::string_view{ "RBRACKET" }, std::uint16_t{ VK_OEM_6 } },
				std::pair{ std::string_view{ "APOSTROPHE" }, std::uint16_t{ VK_OEM_7 } },
				std::pair{ std::string_view{ "EQUAL" }, std::uint16_t{ VK_OEM_PLUS } },
				std::pair{ std::string_view{ "COMMA" }, std::uint16_t{ VK_OEM_COMMA } },
				std::pair{ std::string_view{ "MINUS" }, std::uint16_t{ VK_OEM_MINUS } },
				std::pair{ std::string_view{ "PERIOD" }, std::uint16_t{ VK_OEM_PERIOD } },
				std::pair{ std::string_view{ "KP_0" }, std::uint16_t{ VK_NUMPAD0 } },
				std::pair{ std::string_view{ "KP_1" }, std::uint16_t{ VK_NUMPAD1 } },
				std::pair{ std::string_view{ "KP_2" }, std::uint16_t{ VK_NUMPAD2 } },
				std::pair{ std::string_view{ "KP_3" }, std::uint16_t{ VK_NUMPAD3 } },
				std::pair{ std::string_view{ "KP_4" }, std::uint16_t{ VK_NUMPAD4 } },
				std::pair{ std::string_view{ "KP_5" }, std::uint16_t{ VK_NUMPAD5 } },
				std::pair{ std::string_view{ "KP_6" }, std::uint16_t{ VK_NUMPAD6 } },
				std::pair{ std::string_view{ "KP_7" }, std::uint16_t{ VK_NUMPAD7 } },
				std::pair{ std::string_view{ "KP_8" }, std::uint16_t{ VK_NUMPAD8 } },
				std::pair{ std::string_view{ "KP_9" }, std::uint16_t{ VK_NUMPAD9 } },
				std::pair{ std::string_view{ "KP_DEL" }, std::uint16_t{ VK_DECIMAL } },
				std::pair{ std::string_view{ "KP_DIVIDE" }, std::uint16_t{ VK_DIVIDE } },
				std::pair{ std::string_view{ "KP_MULTIPLY" }, std::uint16_t{ VK_MULTIPLY } },
				std::pair{ std::string_view{ "KP_MINUS" }, std::uint16_t{ VK_SUBTRACT } },
				std::pair{ std::string_view{ "KP_PLUS" }, std::uint16_t{ VK_ADD } },
			};
			if ( !virtual_key )
			{
				for ( const auto& [ source, key ] : named_keys )
				{
					if ( normalized == source )
					{
						virtual_key = key;
						break;
					}
				}
			}
			return virtual_key
				? input_binding{ input_device::keyboard, virtual_key, normalized }
				: input_binding{};
		}

		[[nodiscard]] std::uintptr_t binding_member_offset(
			const platform::windows::process_session& process, const std::uintptr_t getter )
		{
			std::array<std::uint8_t, 96> code{};
			if ( !getter || !process.copy( getter, code.data( ), code.size( ) ) )
				return k_current_binding_offset;

			for ( auto index = std::size_t{}; index + 8 <= code.size( ); ++index )
			{
				if ( ( code[index] & 0xf0u ) != 0x40u || code[index + 1] != 0x8bu
					|| ( code[index + 2] & 0xc7u ) != 0x84u )
				{
					continue;
				}
				std::int32_t displacement{};
				std::memcpy( &displacement, code.data( ) + index + 4,
					sizeof( displacement ) );
				if ( displacement >= 0x10000 && displacement <= 0x100000 )
					return static_cast<std::uintptr_t>( displacement );
			}
			return k_current_binding_offset;
		}

		[[nodiscard]] std::uintptr_t locate_key_name_table(
			const platform::windows::process_session& process,
			const std::uintptr_t input_module )
		{
			const auto validate = [ &process ]( const std::uintptr_t table )
			{
				const auto space = process.load<std::uintptr_t>(
					table + 66 * sizeof( std::uintptr_t ) );
				return space && process.load_text( space, 16 ) == "SPACE";
			};

			const auto vtable = process.locate_vtable( input_module, "CInputSystem" );
			const auto function = vtable ? process.load<std::uintptr_t>(
				vtable + 40 * sizeof( std::uintptr_t ) ) : 0;
			std::array<std::uint8_t, 64> code{};
			if ( function && process.copy( function, code.data( ), code.size( ) ) )
			{
				for ( auto index = std::size_t{}; index + 7 <= code.size( ); ++index )
				{
					if ( code[index] != 0x48u || code[index + 1] != 0x8du
						|| ( code[index + 2] & 0xc7u ) != 0x05u )
						continue;

					std::int32_t displacement{};
					std::memcpy( &displacement, code.data( ) + index + 3,
						sizeof( displacement ) );
					const auto candidate = function + index + 7 + displacement;
					if ( validate( candidate ) )
						return candidate;
				}
			}

			const auto fallback = input_module + k_current_key_name_table_rva;
			return validate( fallback ) ? fallback : 0;
		}

		[[nodiscard]] bool same_binding(
			const input_binding& left, const input_binding& right )
		{
			return left.device == right.device
				&& left.virtual_key == right.virtual_key;
		}

		[[nodiscard]] input_binding default_binding( const input_action action )
		{
			switch ( action )
			{
			case input_action::forward:
				return { input_device::keyboard, 'W', "W" };
			case input_action::back:
				return { input_device::keyboard, 'S', "S" };
			case input_action::left:
				return { input_device::keyboard, 'A', "A" };
			case input_action::right:
				return { input_device::keyboard, 'D', "D" };
			case input_action::walk:
				return { input_device::keyboard, VK_LSHIFT, "SHIFT" };
			case input_action::duck:
				return { input_device::keyboard, VK_LCONTROL, "CTRL" };
			case input_action::jump:
				return { input_device::keyboard, VK_SPACE, "SPACE" };
			case input_action::attack:
				return { input_device::mouse_primary, 0, "MOUSE1" };
			case input_action::attack2:
				return { input_device::mouse_secondary, 0, "MOUSE2" };
			default:
				return {};
			}
		}

	}

	void live_input_bindings::refresh_locked(
		const std::chrono::steady_clock::time_point now )
	{
		if ( now < this->m_next_refresh )
			return;
		this->m_next_refresh = now + std::chrono::seconds( 1 );

		auto& process = app::context().process;
		if ( !this->m_input_service )
		{
			this->m_input_service = process.locate_vtable_object(
				app::context().modules.engine, "CInputService" );
			if ( this->m_input_service )
			{
				const auto vtable = process.load<std::uintptr_t>( this->m_input_service );
				const auto getter = vtable ? process.load<std::uintptr_t>(
					vtable + 32 * sizeof( std::uintptr_t ) ) : 0;
				this->m_binding_table = this->m_input_service
					+ binding_member_offset( process, getter );
			}
		}
		if ( !this->m_key_name_table )
		{
			this->m_key_name_table = locate_key_name_table(
				process, app::context().modules.input_system );
		}
		if ( !this->m_binding_table || !this->m_key_name_table )
			return;

		std::vector<std::byte> records( k_input_code_count * k_binding_record_size );
		std::array<std::uintptr_t, k_input_code_count> key_names{};
		if ( !process.copy( this->m_binding_table, records.data( ), records.size( ) )
			|| !process.copy( this->m_key_name_table, key_names.data( ),
				sizeof( key_names ) ) )
		{
			return;
		}
		const auto key_name_is = [ &process, &key_names ](
			const std::size_t code, const std::string_view expected )
		{
			return code < key_names.size( ) && key_names[ code ]
				&& process.load_text( key_names[ code ], 32 ) == expected;
		};

		if ( !key_name_is( 65, "ENTER" ) || !key_name_is( 66, "SPACE" )
			|| !key_name_is( 317, "MOUSE1" ) || !key_name_is( 318, "MOUSE2" ) )
		{
			this->m_bindings = {};
			return;
		}

		decltype( m_bindings ) refreshed{};
		for ( auto code = std::size_t{};
			code + k_input_name_bias < k_input_code_count; ++code )
		{
			const auto name_address = key_names[ code + k_input_name_bias ];
			for ( auto slot = std::size_t{}; slot < k_binding_slot_count; ++slot )
			{
				std::uintptr_t command_address{};
				std::memcpy( &command_address,
					records.data( ) + code * k_binding_record_size
						+ slot * sizeof( std::uintptr_t ), sizeof( command_address ) );
				if ( command_address < 0x10000 || !name_address )
					continue;

				const auto command = process.load_text( command_address, 192 );
				const auto name = process.load_text( name_address, 32 );
				auto binding = source_key_to_binding( name );
				if ( !binding )
					continue;

				for ( auto action = std::size_t{}; action < k_command_tokens.size( ); ++action )
				{
					const auto& tokens = k_command_tokens[action];
					if ( !contains_command( command, tokens[0] )
						&& !contains_command( command, tokens[1] ) )
					{
						continue;
					}

					auto& candidates = refreshed[action];
					if ( !std::ranges::any_of( candidates,
						[ &binding ]( const input_binding& candidate )
						{
							return same_binding( binding, candidate );
						} ) )
					{
						candidates.push_back( binding );
					}
				}
			}
		}
		this->m_bindings = std::move( refreshed );
	}

	input_binding live_input_bindings::resolve(
		const input_action action, const std::uint16_t preferred_virtual_key )
	{
		std::scoped_lock lock( this->m_mutex );
		this->refresh_locked( std::chrono::steady_clock::now( ) );

		const auto index = static_cast<std::size_t>( action );
		if ( index >= this->m_bindings.size( ) )
			return {};
		const auto& candidates = this->m_bindings[index];
		if ( preferred_virtual_key )
		{
			const auto preferred = std::ranges::find_if( candidates,
				[ preferred_virtual_key ]( const input_binding& candidate )
				{
					return candidate.device == input_device::keyboard
						&& candidate.virtual_key == preferred_virtual_key;
				} );
			if ( preferred != candidates.end( ) )
				return *preferred;
		}

		const auto expected_mouse = action == input_action::attack
			? input_device::mouse_primary
			: action == input_action::attack2
				? input_device::mouse_secondary : input_device::none;
		if ( expected_mouse != input_device::none )
		{
			const auto expected = std::ranges::find_if( candidates,
				[ expected_mouse ]( const input_binding& candidate )
				{
					return candidate.device == expected_mouse;
				} );
			if ( expected != candidates.end( ) )
				return *expected;
		}

		const auto ordinary = std::ranges::find_if( candidates,
			[ ]( const input_binding& candidate )
			{
				return candidate.device != input_device::keyboard
					|| ( candidate.virtual_key != VK_F23
						&& candidate.virtual_key != VK_F24 );
			} );
		return ordinary != candidates.end( ) ? *ordinary
			: candidates.empty( ) ? input_binding{} : candidates.front( );
	}

	std::vector<input_binding> live_input_bindings::candidates(
		const input_action action )
	{
		std::scoped_lock lock( this->m_mutex );
		this->refresh_locked( std::chrono::steady_clock::now( ) );
		const auto index = static_cast<std::size_t>( action );
		return index < this->m_bindings.size( )
			? this->m_bindings[index] : std::vector<input_binding>{};
	}

}
