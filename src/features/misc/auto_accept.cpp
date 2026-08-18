#include <stdafx.hpp>
#include <features/misc/misc.hpp>

namespace
{
	constexpr std::string_view k_accept_button_id{ "AcceptMatchBtn" };
	constexpr std::string_view k_accept_popup_id{ "id-accept-match" };
	constexpr std::uint32_t k_panorama_scan_chunk{ 384 };

	struct panel_header
	{
		std::uintptr_t vtable{};
		std::uintptr_t client{};
		std::uintptr_t id{};
		std::uintptr_t parent{};
	};

	struct registry_entry
	{
		std::array<std::byte, 0x10> metadata{};
		std::uintptr_t panel{};
		std::uintptr_t generation{};
	};

	static_assert( sizeof( registry_entry ) == 0x20 );

	struct panorama_runtime
	{
		std::uintptr_t engine{};
		std::uintptr_t ui_panel_vtable{};
		std::uintptr_t button_vtable{};
		std::uintptr_t class_symbol_table{};
		std::uint32_t registry_offset{};
		bool attempted{};
	};

	struct panel_geometry
	{
		float x{};
		float y{};
		float width{};
		float height{};
		float root_width{};
		float root_height{};
	};

	[[nodiscard]] bool readable_pointer( const std::uintptr_t value )
	{
		return value >= 0x10000 && value <= 0x00007fffffffffffULL;
	}

	[[nodiscard]] HWND active_root_window( )
	{
		GUITHREADINFO gui{};
		gui.cbSize = sizeof( gui );
		return ::GetGUIThreadInfo( 0, &gui )
			? ::GetAncestor( gui.hwndActive, GA_ROOT ) : nullptr;
	}

	template <typename... arguments_t>
	void trace_auto_accept( std::format_string<arguments_t...> format,
		arguments_t&&... arguments )
	{
		const auto path = platform::windows::runtime_storage::area(
			"auto_accept.log" );
		if ( path.empty( ) ) return;
		std::ofstream stream{ path, std::ios::app };
		if ( stream ) stream << std::format( format,
			std::forward<arguments_t>( arguments )... ) << '\n';
	}

	[[nodiscard]] std::uint32_t decode_registry_offset(
		const platform::windows::process_session& process,
		const std::uintptr_t method )
	{
		std::array<std::uint8_t, 48> code{};
		if ( !process.copy( method, code.data( ), code.size( ) ) ) return 0;
		for ( std::size_t index = 0; index + 7 <= code.size( ); ++index )
		{
			if ( code[index] == 0x48 && code[index + 1] == 0x81
				&& code[index + 2] == 0xc1 )
			{
				std::uint32_t offset{};
				std::memcpy( &offset, code.data( ) + index + 3, sizeof( offset ) );
				return offset;
			}
			if ( code[index] == 0x48 && code[index + 1] == 0x83
				&& code[index + 2] == 0xc1 )
				return code[index + 3];
		}
		return 0;
	}

	[[nodiscard]] std::uintptr_t decode_engine_member(
		const platform::windows::process_session& process,
		const std::uintptr_t object, const std::uintptr_t method )
	{
		std::array<std::uint8_t, 32> code{};
		if ( !process.copy( method, code.data( ), code.size( ) ) ) return 0;
		for ( std::size_t index = 0; index + 7 <= code.size( ); ++index )
		{
			std::int32_t offset{};
			if ( code[index] == 0x48 && code[index + 1] == 0x8b
				&& code[index + 2] == 0x81 )
			{
				std::memcpy( &offset, code.data( ) + index + 3, sizeof( offset ) );
				return process.load<std::uintptr_t>( object + offset );
			}
			if ( code[index] == 0x48 && code[index + 1] == 0x8b
				&& code[index + 2] == 0x41 )
				return process.load<std::uintptr_t>( object + code[index + 3] );
		}
		return 0;
	}

	[[nodiscard]] std::uintptr_t decode_class_symbol_table(
		const platform::windows::process_session& process,
		const std::uintptr_t method )
	{

		std::array<std::uint8_t, 0x1200> code{};
		if ( !process.copy( method, code.data( ), code.size( ) ) ) return 0;
		for ( std::size_t index = 0; index + 13 <= code.size( ); ++index )
		{
			if ( code[index] != 0x48 || code[index + 1] != 0x8d
				|| code[index + 2] != 0x0d || code[index + 7] != 0xff
				|| code[index + 8] != 0x15 )
				continue;
			std::int32_t displacement{};
			std::memcpy( &displacement, code.data( ) + index + 3,
				sizeof( displacement ) );
			const auto candidate = method + index + 7 + displacement;
			const auto indices = process.load<std::uintptr_t>( candidate + 0x28 );
			const auto shift = process.load<std::uint32_t>( candidate + 0x38 );
			const auto mask = process.load<std::uint32_t>( candidate + 0x3c );
			const auto pool_count = process.load<std::uint32_t>( candidate + 0x40 );
			const auto external_pools = process.load<std::uint32_t>( candidate + 0x44 );
			const auto pools = process.load<std::uintptr_t>( candidate + 0x48 );
			if ( readable_pointer( indices ) && shift > 0 && shift < 32
				&& mask && pool_count > 0 && pool_count < 4096
				&& ( external_pools & 0x7fffffffu ) != 0
				&& readable_pointer( pools ) )
				return candidate;
		}
		return 0;
	}

	[[nodiscard]] std::string panorama_symbol_text(
		const panorama_runtime& runtime, const std::uint16_t symbol )
	{
		if ( symbol == 0xffff || !runtime.class_symbol_table ) return {};
		const auto& process = app::context().process;
		const auto table = runtime.class_symbol_table;
		const auto indices = process.load<std::uintptr_t>( table + 0x28 );
		const auto shift = process.load<std::uint32_t>( table + 0x38 );
		const auto mask = process.load<std::uint32_t>( table + 0x3c );
		const auto pool_count = process.load<std::uint32_t>( table + 0x40 );
		const auto pools = process.load<std::uintptr_t>( table + 0x48 );
		if ( !readable_pointer( indices ) || shift == 0 || shift >= 32
			|| !mask || !readable_pointer( pools ) )
			return {};
		const auto packed = process.load<std::uint32_t>(
			indices + static_cast<std::uintptr_t>( symbol ) * sizeof( std::uint32_t ) );
		const auto pool = packed >> shift;
		if ( pool >= pool_count ) return {};
		const auto pool_base = process.load<std::uintptr_t>(
			pools + static_cast<std::uintptr_t>( pool ) * 0x10 + 0x08 );
		if ( !readable_pointer( pool_base ) ) return {};
		return process.load_text( pool_base + ( packed & mask ), 64 );
	}

	[[nodiscard]] bool panel_has_class( const std::uintptr_t panel,
		const panorama_runtime& runtime, const std::string_view expected )
	{
		const auto& process = app::context().process;
		const auto count = process.load<std::uint32_t>( panel + 0x148 );
		const auto data = process.load<std::uintptr_t>( panel + 0x150 );
		if ( count == 0 ) return false;

		if ( count > 64 || !readable_pointer( data )
			|| !runtime.class_symbol_table )
			return true;
		std::array<std::uint16_t, 64> symbols{};
		if ( !process.copy( data, symbols.data( ), count * sizeof( symbols[0] ) ) )
			return true;
		for ( std::uint32_t index = 0; index < count; ++index )
		{
			if ( panorama_symbol_text( runtime, symbols[index] ) == expected )
				return true;
		}
		return false;
	}

	[[nodiscard]] panorama_runtime& panorama( )
	{
		static panorama_runtime runtime{};
		if ( runtime.attempted ) return runtime;

		const auto& process = app::context().process;
		const auto panorama_module = app::context().modules.panorama
			? app::context().modules.panorama
			: process.module_base( "panorama.dll" );
		const auto client_module = app::context().modules.client;
		if ( !panorama_module || !client_module ) return runtime;
		runtime.attempted = true;

		runtime.ui_panel_vtable = process.locate_vtable(
			panorama_module, "CUIPanel@panorama" );
		runtime.button_vtable = process.locate_vtable(
			client_module, "CButton@panorama" );
		const auto panel_serializer = process.load<std::uintptr_t>(
			runtime.ui_panel_vtable + 315 * sizeof( std::uintptr_t ) );
		runtime.class_symbol_table = decode_class_symbol_table(
			process, panel_serializer );
		const auto interface_object = process.locate_vtable_object(
			panorama_module, "CPanoramaUIEngine" );
		const auto interface_table = process.load<std::uintptr_t>( interface_object );
		const auto engine_getter = process.load<std::uintptr_t>(
			interface_table + 13 * sizeof( std::uintptr_t ) );
		runtime.engine = decode_engine_member(
			process, interface_object, engine_getter );
		const auto engine_table = process.load<std::uintptr_t>( runtime.engine );
		const auto valid_panel_method = process.load<std::uintptr_t>(
			engine_table + 32 * sizeof( std::uintptr_t ) );
		runtime.registry_offset = decode_registry_offset(
			process, valid_panel_method );

		if ( !readable_pointer( runtime.engine ) || !runtime.ui_panel_vtable
			|| !runtime.button_vtable || !runtime.class_symbol_table
			|| runtime.registry_offset < 0x40
			|| runtime.registry_offset > 0x1000 )
		{
			runtime = { .attempted = true };
			app::context().diagnostics.warning(
				"[auto-accept] Panorama object resolver initialization failed" );
		}
		else
		{
			app::context().diagnostics.info(
				"[auto-accept] Panorama Button resolver ready (registry +{:#x})",
				runtime.registry_offset );
		}
		return runtime;
	}

	[[nodiscard]] bool is_accept_button( const std::uintptr_t panel )
	{
		const auto& process = app::context().process;
		const auto& runtime = panorama( );
		if ( !readable_pointer( panel ) || !runtime.engine ) return false;

		const auto header = process.load<panel_header>( panel );
		if ( header.vtable != runtime.ui_panel_vtable
			|| !readable_pointer( header.client )
			|| process.load<std::uintptr_t>( header.client ) != runtime.button_vtable
			|| !readable_pointer( header.id ) )
			return false;
		const auto button_id = process.load_text( header.id, 64 );
		if ( button_id != k_accept_button_id ) return false;

		auto current = panel;
		bool accept_layout{};
		for ( int depth = 0; depth < 40 && readable_pointer( current ); ++depth )
		{
			const auto ancestor = process.load<panel_header>( current );
			const auto id = readable_pointer( ancestor.id )
				? process.load_text( ancestor.id, 64 ) : std::string{};
			accept_layout |= id == k_accept_popup_id;
			if ( !readable_pointer( ancestor.parent ) || ancestor.parent == current )
				break;
			current = ancestor.parent;
		}

		return accept_layout && !panel_has_class( panel, runtime, "hidden" );
	}

	[[nodiscard]] panel_geometry inspect_geometry( const std::uintptr_t panel )
	{
		const auto& process = app::context().process;
		panel_geometry result{};
		std::uintptr_t current = panel;
		std::uintptr_t root = panel;
		std::unordered_set<std::uintptr_t> visited{};

		for ( int depth = 0; depth < 40 && readable_pointer( current ); ++depth )
		{
			if ( !visited.emplace( current ).second ) return {};
			const auto visibility = process.load<std::uint8_t>( current + 0x11d );
			if ( ( visibility & 0x08 ) == 0 ) return {};

			const auto x = process.load<float>( current + 0x1b0 );
			const auto y = process.load<float>( current + 0x1b4 );
			if ( !std::isfinite( x ) || !std::isfinite( y )
				|| std::abs( x ) > 30000.0f || std::abs( y ) > 30000.0f )
				return {};
			result.x += x;
			result.y += y;
			if ( current == panel )
			{
				result.width = process.load<float>( current + 0x1c0 );
				result.height = process.load<float>( current + 0x1c4 );
			}

			root = current;
			const auto parent = process.load<std::uintptr_t>( current + 0x18 );
			if ( !readable_pointer( parent ) || parent == current ) break;
			current = parent;
		}

		result.root_width = process.load<float>( root + 0x1c0 );
		result.root_height = process.load<float>( root + 0x1c4 );
		if ( !std::isfinite( result.width ) || !std::isfinite( result.height )
			|| !std::isfinite( result.root_width ) || !std::isfinite( result.root_height )
			|| result.width < 24.0f || result.height < 16.0f
			|| result.root_width < 320.0f || result.root_height < 240.0f )
			return {};
		return result;
	}

	[[nodiscard]] std::optional<POINT> button_center(
		const std::uintptr_t panel, const HWND hwnd )
	{
		if ( !is_accept_button( panel ) ) return std::nullopt;
		const auto geometry = inspect_geometry( panel );
		if ( geometry.width <= 0.0f || geometry.height <= 0.0f )
			return std::nullopt;

		RECT client{};
		if ( !::GetClientRect( hwnd, &client ) ) return std::nullopt;
		const auto client_width = client.right - client.left;
		const auto client_height = client.bottom - client.top;
		if ( client_width <= 0 || client_height <= 0 ) return std::nullopt;

		const auto scale_x = static_cast<float>( client_width ) / geometry.root_width;
		const auto scale_y = static_cast<float>( client_height ) / geometry.root_height;

		constexpr auto click_y_fraction = 0.60f;
		POINT point{
			static_cast<LONG>( std::lround(
				( geometry.x + geometry.width * 0.5f ) * scale_x ) ),
			static_cast<LONG>( std::lround(
				( geometry.y + geometry.height * click_y_fraction ) * scale_y ) )
		};
		if ( point.x < client.left || point.x >= client.right
			|| point.y < client.top || point.y >= client.bottom )
			return std::nullopt;
		return ::ClientToScreen( hwnd, &point )
			? std::optional<POINT>{ point } : std::nullopt;
	}

	[[nodiscard]] std::uintptr_t scan_accept_button( std::uint32_t& cursor,
		std::vector<std::uintptr_t>& known_slots,
		std::uintptr_t& best_panel, std::uint32_t& best_generation )
	{
		const auto& process = app::context().process;
		const auto& runtime = panorama( );
		if ( !runtime.engine ) return 0;

		const auto registry = runtime.engine + runtime.registry_offset;
		const auto capacity = process.load<std::uint32_t>( registry + 0x0c )
			& 0x7fffffffu;
		const auto data = process.load<std::uintptr_t>( registry + 0x10 );
		if ( capacity == 0 || capacity > 65536 || !readable_pointer( data ) )
		{
			cursor = 0;
			known_slots.clear( );
			best_panel = 0;
			best_generation = 0;
			return 0;
		}

		if ( known_slots.size( ) != capacity )
		{
			known_slots.assign( capacity, 0 );
			cursor = 0;
			best_panel = 0;
			best_generation = 0;
		}
		if ( cursor >= capacity ) cursor = 0;
		const auto begin = cursor;
		const auto count = std::min( k_panorama_scan_chunk, capacity - cursor );
		std::vector<registry_entry> entries( count );
		if ( !process.copy( data + static_cast<std::uintptr_t>( cursor )
			* sizeof( registry_entry ), entries.data( ),
			entries.size( ) * sizeof( registry_entry ) ) )
		{
			cursor = 0;
			return 0;
		}

		cursor += count;
		const auto completed_cycle = cursor >= capacity;
		if ( completed_cycle ) cursor = 0;
		for ( std::uint32_t index = 0; index < count; ++index )
		{
			const auto slot = begin + index;
			const auto panel = entries[index].panel;
			if ( known_slots[slot] == panel ) continue;
			known_slots[slot] = panel;
			if ( !is_accept_button( panel ) ) continue;
			const auto generation = static_cast<std::uint32_t>(
				entries[index].generation );
			if ( !best_panel || static_cast<std::int32_t>(
				generation - best_generation ) > 0 )
			{
				best_panel = panel;
				best_generation = generation;
			}
		}
		if ( !completed_cycle ) return 0;
		const auto result = best_panel;
		best_panel = 0;
		best_generation = 0;
		return result;
	}
}

namespace features::misc {

	void auto_accept_t::tick( )
	{
		const auto clear_click = [this]( const bool release_button )
		{
			if ( release_button && m_click_phase == click_phase::pressed )
			{
				app::context().input.pointer( 0, 0,
					platform::windows::pointer_action::primary_up );
			}
			if ( m_click_phase != click_phase::idle && m_saved_cursor_valid )
				::SetCursorPos( m_saved_cursor_x, m_saved_cursor_y );
			m_click_phase = click_phase::idle;
			m_click_window = 0;
			m_click_panel = 0;
			m_saved_cursor_valid = false;
		};

		if ( !config::general_settings.auto_accept )
		{
			clear_click( true );
			m_signal_identity = 0;
			m_candidate_panel = 0;
			m_clicked_panel = 0;
			m_scan_best_panel = 0;
			m_scan_best_generation = 0;
			m_panorama_cursor = 0;
			m_panorama_slots.clear( );
			m_clicked_panel_hidden = false;
			return;
		}

		const auto signal = app::context().addresses.auto_accept
			? app::context().process.load<std::uintptr_t>(
				app::context().addresses.auto_accept ) : 0;
		if ( signal != m_signal_identity )
		{
			clear_click( true );
			m_signal_identity = signal;
			m_candidate_panel = 0;
			m_clicked_panel = 0;
			m_scan_best_panel = 0;
			m_scan_best_generation = 0;
			m_panorama_cursor = 0;
			m_panorama_slots.clear( );
			m_clicked_panel_hidden = false;
		}

		if ( m_click_phase != click_phase::idle )
		{
			const auto cs2_hwnd = reinterpret_cast<HWND>( m_click_window );
			const auto foreground = cs2_hwnd
				&& ::IsWindow( cs2_hwnd )
				&& active_root_window( ) == cs2_hwnd;
			const auto live_point = foreground
				? button_center( m_click_panel, cs2_hwnd )
				: std::optional<POINT>{};
			const auto finish_visible_button = [this, &clear_click]( const bool hidden )
			{
				const auto completed_panel = m_click_panel;
				clear_click( false );
				m_candidate_panel = 0;
				m_clicked_panel = completed_panel;

				m_clicked_panel_hidden = hidden;
			};

			switch ( m_click_phase )
			{
			case click_phase::moved:
				if ( !foreground )
				{
					trace_auto_accept( "hardware click paused on focus loss panel={:#x}",
						m_click_panel );
					clear_click( false );
					return;
				}
				if ( !live_point )
				{
					trace_auto_accept( "accept button disappeared panel={:#x}",
						m_click_panel );
					finish_visible_button( true );
					return;
				}
				m_click_x = live_point->x;
				m_click_y = live_point->y;

				::SetCursorPos( m_click_x, m_click_y );
				if ( !app::context().input.pointer( 0, 0,
					platform::windows::pointer_action::primary_down ) )
				{
					trace_auto_accept( "hardware button-down failed panel={:#x}",
						m_click_panel );
					clear_click( false );
					return;
				}
				m_click_phase = click_phase::pressed;
				trace_auto_accept( "hardware button-down panel={:#x} point={},{}",
					m_click_panel, m_click_x, m_click_y );
				return;

			case click_phase::pressed:

				if ( !app::context().input.pointer( 0, 0,
					platform::windows::pointer_action::primary_up ) )
				{
					trace_auto_accept( "hardware button-up retry panel={:#x}",
						m_click_panel );
					return;
				}
				if ( live_point )
				{
					trace_auto_accept(
						"hardware click completed once; waiting for hide panel={:#x}",
						m_click_panel );
				}
				else
				{
					trace_auto_accept(
						"hardware click accepted; button disappeared panel={:#x}",
						m_click_panel );
				}
				finish_visible_button( !live_point );
				return;

			case click_phase::idle:
				break;
			}
		}

		if ( m_clicked_panel )
		{
			if ( !is_accept_button( m_clicked_panel ) )
			{
				m_clicked_panel = 0;
				m_clicked_panel_hidden = false;
			}
			else if ( const auto cs2_hwnd =
				::FindWindowW( nullptr, L"Counter-Strike 2" ) )
			{
				const auto visible_point = button_center(
					m_clicked_panel, cs2_hwnd );
				if ( !visible_point )
				{
					m_clicked_panel_hidden = true;
				}
				else if ( m_clicked_panel_hidden )
				{
					trace_auto_accept( "reused accept button appeared panel={:#x}",
						m_clicked_panel );
					m_candidate_panel = m_clicked_panel;
					m_clicked_panel = 0;
					m_clicked_panel_hidden = false;
				}
			}
		}

		if ( const auto discovered = scan_accept_button(
			m_panorama_cursor, m_panorama_slots,
			m_scan_best_panel, m_scan_best_generation ) )
		{
			if ( discovered != m_clicked_panel )
			{
				m_candidate_panel = discovered;
				trace_auto_accept( "candidate panel={:#x}", discovered );
			}
		}

		if ( m_candidate_panel && !is_accept_button( m_candidate_panel ) )
			m_candidate_panel = 0;
		if ( !m_candidate_panel ) return;

		const auto cs2_hwnd = ::FindWindowW( nullptr, L"Counter-Strike 2" );
		if ( !cs2_hwnd ) return;

		const auto point = button_center( m_candidate_panel, cs2_hwnd );
		if ( !point )
		{
			trace_auto_accept( "geometry rejected panel={:#x}", m_candidate_panel );
			m_candidate_panel = 0;
			return;
		}

		const auto foreground = active_root_window( ) == cs2_hwnd;
		if ( !foreground ) return;

		POINT saved{};
		if ( !::GetCursorPos( &saved )
			|| !::SetCursorPos( point->x, point->y ) )
		{
			trace_auto_accept(
				"hardware cursor move failed panel={:#x} point={},{}",
				m_candidate_panel, point->x, point->y );
			return;
		}

		m_click_window = reinterpret_cast<std::uintptr_t>( cs2_hwnd );
		m_click_panel = m_candidate_panel;
		m_saved_cursor_x = saved.x;
		m_saved_cursor_y = saved.y;
		m_click_x = point->x;
		m_click_y = point->y;
		m_saved_cursor_valid = true;
		m_click_phase = click_phase::moved;
		trace_auto_accept(
			"hardware cursor moved panel={:#x} point={},{} saved={},{}",
			m_click_panel, m_click_x, m_click_y,
			m_saved_cursor_x, m_saved_cursor_y );
	}

}
