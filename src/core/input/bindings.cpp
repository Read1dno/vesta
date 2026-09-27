#include <stdafx.hpp>
#include <core/input/bindings.hpp>
#include <core/input/manual_bindings.hpp>
#include <core/input/hotkeys.hpp>
#include <config/settings.hpp>

namespace game {

	namespace {
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

	} // namespace

	input_binding configured_input_bindings::resolve(
		const input_action action)
	{
		const auto snapshot = config::get_runtime_snapshot();
		if (!snapshot) return {};
		auto binding = configured_binding(action, snapshot->general.m_game_controls);
		if (binding.device == input_device::keyboard
			&& platform::windows::is_lifecycle_key(binding.virtual_key))
			return {};
		return binding;
	}

	std::vector<input_binding> configured_input_bindings::candidates(
		const input_action action)
	{
		auto binding = resolve(action);
		return binding ? std::vector<input_binding>{std::move(binding)}
			: std::vector<input_binding>{};
	}

	bool configured_input_bindings::text_entry_active( )
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

} // namespace game
