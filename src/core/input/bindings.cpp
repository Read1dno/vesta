#include <stdafx.hpp>
#include <core/input/bindings.hpp>

namespace game {

	namespace {

		constexpr std::size_t k_input_code_count{ 0x204 };
		constexpr std::size_t k_binding_record_size{ 0x58 };

		constexpr std::size_t k_binding_slot_count{ 11 };

		constexpr std::ptrdiff_t k_min_name_bias{ -8 };
		constexpr std::ptrdiff_t k_max_name_bias{ 8 };

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

		constexpr std::array<std::pair<std::string_view, std::string_view>, 9>
			k_layout_anchors{ {
				{ "+forward", "W" },
				{ "+back", "S" },
				{ "+moveleft", "A" },
				{ "+moveright", "D" },
				{ "+speed", "SHIFT" },
				{ "+duck", "CTRL" },
				{ "+jump", "SPACE" },
				{ "+attack", "MOUSE1" },
				{ "+attack2", "MOUSE2" },
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

		[[nodiscard]] bool same_text( std::string_view left,
			std::string_view right )
		{
			if ( left.size( ) != right.size( ) )
				return false;
			return std::ranges::equal( left, right, [ ]( const char a, const char b )
			{
				return std::tolower( static_cast<unsigned char>( a ) )
					== std::tolower( static_cast<unsigned char>( b ) );
			} );
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
				return 0;

			for ( auto index = std::size_t{}; index + 7 <= code.size( ); ++index )
			{
				if ( ( code[index] & 0xf0u ) != 0x40u
					|| code[index + 1] != 0x8bu )
				{
					continue;
				}
				const auto modrm = code[index + 2];
				if ( ( modrm >> 6u ) != 2u )
					continue;
				auto displacement_index = index + 3;
				if ( ( modrm & 7u ) == 4u )
					++displacement_index;
				if ( displacement_index + sizeof( std::int32_t ) > code.size( ) )
					continue;
				std::int32_t displacement{};
				std::memcpy( &displacement, code.data( ) + displacement_index,
					sizeof( displacement ) );
				if ( displacement >= 0x10000 && displacement <= 0x100000 )
					return static_cast<std::uintptr_t>( displacement );
			}
			return 0;
		}

		[[nodiscard]] std::uintptr_t locate_key_name_table(
			const platform::windows::process_session& process,
			const std::uintptr_t input_module )
		{
			const auto validate = [ &process ]( const std::uintptr_t table )
			{
				if ( !table )
					return false;
				std::array<std::uintptr_t, k_input_code_count> names{};
				if ( !process.copy( table, names.data( ), sizeof( names ) ) )
					return false;
				constexpr std::array required{
					std::string_view{ "SPACE" }, std::string_view{ "MOUSE1" },
					std::string_view{ "MOUSE2" }, std::string_view{ "W" } };
				for ( const auto expected : required )
				{
					const auto found = std::ranges::any_of( names,
						[ &process, expected ]( const std::uintptr_t address )
						{
							return address && same_text(
								process.load_text( address, 32 ), expected );
						} );
					if ( !found )
						return false;
				}
				return true;
			};

			const auto vtable = process.locate_vtable( input_module, "CInputSystem" );
			for ( auto slot = std::size_t{}; vtable && slot < 96; ++slot )
			{
				const auto function = process.load<std::uintptr_t>(
					vtable + slot * sizeof( std::uintptr_t ) );
				std::array<std::uint8_t, 128> code{};
				if ( !function || !process.copy( function, code.data( ), code.size( ) ) )
					continue;
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

			return 0;
		}

		[[nodiscard]] std::optional<std::ptrdiff_t> resolve_record_to_name_bias(
			const platform::windows::process_session& process,
			const std::vector<std::byte>& records,
			const std::array<std::uintptr_t, k_input_code_count>& key_names )
		{
			std::array<int, static_cast<std::size_t>(
				k_max_name_bias - k_min_name_bias + 1 )> scores{};
			for ( auto code = std::size_t{}; code < k_input_code_count; ++code )
			{
				for ( auto slot = std::size_t{}; slot < k_binding_slot_count; ++slot )
				{
					std::uintptr_t command_address{};
					std::memcpy( &command_address,
						records.data( ) + code * k_binding_record_size
							+ slot * sizeof( std::uintptr_t ), sizeof( command_address ) );
					if ( command_address < 0x10000 )
						continue;

					const auto command = process.load_text( command_address, 192 );
					for ( const auto& [ token, expected_name ] : k_layout_anchors )
					{
						if ( !contains_command( command, token ) )
							continue;
						for ( auto bias = k_min_name_bias;
							bias <= k_max_name_bias; ++bias )
						{
							const auto name_index = static_cast<std::ptrdiff_t>( code ) + bias;
							if ( name_index < 0 || name_index >=
								static_cast<std::ptrdiff_t>( key_names.size( ) ) )
								continue;
							const auto address = key_names[static_cast<std::size_t>( name_index )];
							if ( address && same_text(
								process.load_text( address, 32 ), expected_name ) )
								++scores[static_cast<std::size_t>( bias - k_min_name_bias )];
						}
					}
				}
			}

			auto best_score = 0;
			auto runner_up_score = 0;
			auto best_bias = std::ptrdiff_t{};
			for ( auto bias = k_min_name_bias; bias <= k_max_name_bias; ++bias )
			{
				const auto score = scores[static_cast<std::size_t>( bias - k_min_name_bias )];
				if ( score > best_score )
				{
					runner_up_score = best_score;
					best_score = score;
					best_bias = bias;
				}
				else if ( score > runner_up_score )
				{
					runner_up_score = score;
				}
			}
			return best_score >= 4 && best_score > runner_up_score
				? std::optional{ best_bias } : std::nullopt;
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

		[[nodiscard]] bool readable_pointer( const std::uintptr_t value )
		{
			return value >= 0x10000 && value <= 0x00007fffffffffffULL;
		}

		[[nodiscard]] std::optional<std::ptrdiff_t> byte_getter_offset(
			const platform::windows::process_session& process,
			const std::uintptr_t function )
		{
			std::array<std::uint8_t, 32> code{};
			if ( !function || !process.copy( function, code.data( ), code.size( ) ) )
				return std::nullopt;

			for ( auto index = std::size_t{}; index + 4 <= code.size( ); ++index )
			{
				if ( code[index] == 0x0f && code[index + 1] == 0xb6
					&& code[index + 2] == 0x41 )
				{
					return static_cast<std::ptrdiff_t>( code[index + 3] );
				}
				if ( code[index] == 0x8a && code[index + 1] == 0x41 )
					return static_cast<std::ptrdiff_t>( code[index + 2] );
				if ( index + 7 <= code.size( )
					&& ( ( code[index] == 0x0f && code[index + 1] == 0xb6
						&& code[index + 2] == 0x81 )
						|| ( code[index] == 0x8a && code[index + 1] == 0x81 ) ) )
				{
					const auto displacement_index = code[index] == 0x0f
						? index + 3 : index + 2;
					std::int32_t displacement{};
					std::memcpy( &displacement, code.data( ) + displacement_index,
						sizeof( displacement ) );
					if ( displacement >= 0 && displacement <= 0x400 )
						return static_cast<std::ptrdiff_t>( displacement );
				}
			}
			return std::nullopt;
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
		}
		if ( this->m_input_service && !this->m_binding_table )
		{
			const auto vtable = process.load<std::uintptr_t>( this->m_input_service );
			const auto getter = vtable ? process.load<std::uintptr_t>(
				vtable + 32 * sizeof( std::uintptr_t ) ) : 0;
			const auto offset = binding_member_offset( process, getter );
			this->m_binding_table = offset ? this->m_input_service + offset : 0;
		}
		if ( !this->m_key_name_table )
		{
			this->m_key_name_table = locate_key_name_table(
				process, app::context().modules.input_system );
		}
		if ( !this->m_binding_table || !this->m_key_name_table )
		{
			this->m_bindings = {};
			this->m_binding_layout_valid = false;
			return;
		}

		std::vector<std::byte> records( k_input_code_count * k_binding_record_size );
		std::array<std::uintptr_t, k_input_code_count> key_names{};
		if ( !process.copy( this->m_binding_table, records.data( ), records.size( ) )
			|| !process.copy( this->m_key_name_table, key_names.data( ),
				sizeof( key_names ) ) )
		{
			this->m_bindings = {};
			this->m_binding_layout_valid = false;
			return;
		}
		if ( !this->m_binding_layout_valid )
		{
			const auto bias = resolve_record_to_name_bias( process, records, key_names );
			if ( !bias )
			{
				this->m_bindings = {};
				return;
			}
			this->m_record_to_name_bias = *bias;
			this->m_binding_layout_valid = true;
		}

		decltype( m_bindings ) refreshed{};
		for ( auto code = std::size_t{}; code < k_input_code_count; ++code )
		{
			const auto name_index = static_cast<std::ptrdiff_t>( code )
				+ this->m_record_to_name_bias;
			if ( name_index < 0 || name_index >=
				static_cast<std::ptrdiff_t>( key_names.size( ) ) )
			{
				continue;
			}
			const auto name_address = key_names[static_cast<std::size_t>( name_index )];
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

	bool live_input_bindings::text_entry_active( )
	{
		std::scoped_lock lock( this->m_mutex );
		const auto now = std::chrono::steady_clock::now( );
		if ( now < this->m_next_chat_sample )
			return this->m_chat_active;
		this->m_next_chat_sample = now + std::chrono::milliseconds( 4 );

		auto& process = app::context().process;
		const auto process_id = process.process_id( );
		if ( process_id != this->m_chat_process_id )
		{
			this->m_hud_global = 0;
			this->m_hud_chat = 0;
			this->m_hud_chat_vtable = 0;
			this->m_chat_active_offset = -1;
			this->m_chat_process_id = process_id;
			this->m_next_chat_lookup = {};
		}

		const auto valid_chat = [ this, &process ]
		{
			return readable_pointer( this->m_hud_chat )
				&& this->m_hud_chat_vtable
				&& process.load<std::uintptr_t>( this->m_hud_chat )
					== this->m_hud_chat_vtable
				&& this->m_chat_active_offset >= 0
				&& this->m_chat_active_offset <= 0x400;
		};

		auto chat_valid = valid_chat( );
		if ( !chat_valid && now >= this->m_next_chat_lookup )
		{
			this->m_next_chat_lookup = now + std::chrono::milliseconds( 250 );
			this->m_hud_chat = 0;
			this->m_hud_chat_vtable = 0;
			this->m_chat_active_offset = -1;

			if ( !this->m_hud_global )
			{
				const auto find_hud = process.scan_signature(
					app::context().modules.client,
					"40 53 48 83 EC 20 48 8B 05 ? ? ? ? 48 8B D9 48 85 C0 74 ? 48 89 5C 24 ? 48 8D 88 58 02 00 00" );
				this->m_hud_global = find_hud
					? process.decode_rip( find_hud + 6 ) : 0;
			}

			const auto root = this->m_hud_global
				? process.load<std::uintptr_t>( this->m_hud_global ) : 0;
			if ( !readable_pointer( root ) )
			{
				this->m_hud_global = 0;
			}
			else
			{
				const auto table = root + 0x258;
				const auto capacity = process.load<std::uint32_t>( table + 0x0c )
					& 0x7fffffffu;
				const auto data = capacity
					? process.load<std::uintptr_t>( table + 0x10 ) : 0;
				if ( readable_pointer( data ) && capacity <= 1024 )
				{
					for ( std::uint32_t index = 0; index < capacity; ++index )
					{
						const auto entry = data
							+ static_cast<std::uintptr_t>( index ) * 0x20;
						const auto name = process.load<std::uintptr_t>( entry + 0x10 );
						const auto value = process.load<std::uintptr_t>( entry + 0x18 );
						if ( !readable_pointer( name ) || !readable_pointer( value )
							|| process.load_text( name, 32 ) != "CCSGO_HudChat" )
							continue;

						const auto vtable = process.load<std::uintptr_t>( value );
						const auto getter = readable_pointer( vtable )
							? process.load<std::uintptr_t>( vtable
								+ 8 * sizeof( std::uintptr_t ) ) : 0;
						const auto offset = byte_getter_offset( process, getter );
						if ( offset )
						{
							this->m_hud_chat = value;
							this->m_hud_chat_vtable = vtable;
							this->m_chat_active_offset = *offset;
						}
						break;
					}
				}
			}
			chat_valid = valid_chat( );
		}

		this->m_chat_active = chat_valid
			&& process.load<std::uint8_t>( this->m_hud_chat
				+ static_cast<std::uintptr_t>( this->m_chat_active_offset ) ) != 0;
		return this->m_chat_active;
	}

	}
