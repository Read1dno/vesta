#include <stdafx.hpp>

#include <scripting/runtime.hpp>
#include <scripting/radar_asset.hpp>

#include <app/workers.hpp>
#include <core/input/bindings.hpp>
#include <features/visuals/visuals.hpp>
#include <simulation/grenade.hpp>
#include <nlohmann/json.hpp>
#include <resources/fonts/weapons.hpp>

#include <charconv>
#include <cwctype>
#include <wincodec.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace scripting {

	using json = nlohmann::json;
	using clock = std::chrono::steady_clock;

	namespace {

		constexpr std::size_t k_memory_limit = 64u * 1024u * 1024u;
		constexpr std::size_t k_draw_limit = 8192;
		constexpr std::size_t k_ui_limit = 512;
		constexpr auto k_tick_period = std::chrono::milliseconds( 16 );
		constexpr auto k_reload_probe_period = std::chrono::milliseconds( 250 );

		std::string path_utf8( const std::filesystem::path& path )
		{
			const auto value = path.u8string( );
			return { reinterpret_cast<const char*>( value.data( ) ), value.size( ) };
		}

		std::wstring wide_utf8( const std::string_view value )
		{
			if ( value.empty( ) ) return {};
			const auto size = ::MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS,
				value.data( ), static_cast<int>( value.size( ) ), nullptr, 0 );
			if ( size <= 0 ) return {};
			std::wstring result( static_cast<std::size_t>( size ), L'\0' );
			if ( ::MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, value.data( ),
				static_cast<int>( value.size( ) ), result.data( ), size ) != size ) return {};
			return result;
		}

		std::wstring quote_windows_argument( const std::wstring_view value )
		{
			std::wstring result{ L"\"" };
			std::size_t slashes{};
			for ( const auto character : value )
			{
				if ( character == L'\\' ) { ++slashes; continue; }
				if ( character == L'\"' ) result.append( slashes * 2 + 1, L'\\' );
				else result.append( slashes, L'\\' );
				slashes = 0; result.push_back( character );
			}
			result.append( slashes * 2, L'\\' );
			result.push_back( L'\"' );
			return result;
		}

		std::string sanitize_id( std::string value )
		{
			for ( auto& c : value )
			{
				const auto valid = ( c >= 'a' && c <= 'z' )
					|| ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' )
					|| c == '_' || c == '-';
				if ( !valid ) c = '_';
			}
			if ( value.empty( ) ) value = "script";
			return value;
		}

		void push_vec3( lua_State* state, const foundation::vec3& value )
		{
			lua_createtable( state, 0, 3 );
			lua_pushnumber( state, value.x ); lua_setfield( state, -2, "x" );
			lua_pushnumber( state, value.y ); lua_setfield( state, -2, "y" );
			lua_pushnumber( state, value.z ); lua_setfield( state, -2, "z" );
		}

		void push_vec2( lua_State* state, const foundation::vec2& value )
		{
			lua_createtable( state, 0, 2 );
			lua_pushnumber( state, value.x ); lua_setfield( state, -2, "x" );
			lua_pushnumber( state, value.y ); lua_setfield( state, -2, "y" );
		}

		foundation::vec3 read_vec3( lua_State* state, const int index )
		{
			foundation::vec3 result{};
			const auto absolute = lua_absindex( state, index );
			if ( !lua_istable( state, absolute ) ) return result;
			lua_getfield( state, absolute, "x" ); result.x = static_cast<float>( luaL_optnumber( state, -1, 0.0 ) ); lua_pop( state, 1 );
			lua_getfield( state, absolute, "y" ); result.y = static_cast<float>( luaL_optnumber( state, -1, 0.0 ) ); lua_pop( state, 1 );
			lua_getfield( state, absolute, "z" ); result.z = static_cast<float>( luaL_optnumber( state, -1, 0.0 ) ); lua_pop( state, 1 );
			return result;
		}

		void push_json( lua_State* state, const json& value, int depth = 0 )
		{
			if ( depth > 32 ) { lua_pushnil( state ); return; }
			if ( value.is_null( ) ) lua_pushnil( state );
			else if ( value.is_boolean( ) ) lua_pushboolean( state, value.get<bool>( ) );
			else if ( value.is_number_integer( ) ) lua_pushinteger( state, value.get<lua_Integer>( ) );
			else if ( value.is_number_unsigned( ) ) lua_pushinteger( state, static_cast<lua_Integer>( value.get<std::uint64_t>( ) ) );
			else if ( value.is_number_float( ) ) lua_pushnumber( state, value.get<lua_Number>( ) );
			else if ( value.is_string( ) )
			{
				const auto& text = value.get_ref<const std::string&>( );
				lua_pushlstring( state, text.data( ), text.size( ) );
			}
			else if ( value.is_array( ) )
			{
				lua_createtable( state, static_cast<int>( value.size( ) ), 0 );
				for ( std::size_t i = 0; i < value.size( ); ++i )
				{
					push_json( state, value[ i ], depth + 1 );
					lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
				}
			}
			else
			{
				lua_createtable( state, 0, static_cast<int>( value.size( ) ) );
				for ( const auto& [ key, child ] : value.items( ) )
				{
					push_json( state, child, depth + 1 );
					lua_setfield( state, -2, key.c_str( ) );
				}
			}
		}

		bool lua_to_json( lua_State* state, const int index, json& out,
			const int depth = 0 )
		{
			if ( depth > 32 ) return false;
			const auto absolute = lua_absindex( state, index );
			switch ( lua_type( state, absolute ) )
			{
			case LUA_TNIL: out = nullptr; return true;
			case LUA_TBOOLEAN: out = lua_toboolean( state, absolute ) != 0; return true;
			case LUA_TNUMBER:
				if ( lua_isinteger( state, absolute ) ) out = lua_tointeger( state, absolute );
				else out = lua_tonumber( state, absolute );
				return true;
			case LUA_TSTRING:
			{
				size_t length{};
				const auto* text = lua_tolstring( state, absolute, &length );
				out = std::string( text, length );
				return true;
			}
			case LUA_TTABLE:
			{
				const auto length = lua_rawlen( state, absolute );
				if ( length > 0 )
				{
					out = json::array( );
					for ( std::size_t i = 1; i <= length; ++i )
					{
						lua_rawgeti( state, absolute, static_cast<lua_Integer>( i ) );
						json child{};
						const auto ok = lua_to_json( state, -1, child, depth + 1 );
						lua_pop( state, 1 );
						if ( !ok ) return false;
						out.push_back( std::move( child ) );
					}
					return true;
				}
				out = json::object( );
				lua_pushnil( state );
				while ( lua_next( state, absolute ) != 0 )
				{
					if ( !lua_isstring( state, -2 ) ) { lua_pop( state, 2 ); return false; }
					size_t key_length{};
					const auto* key = lua_tolstring( state, -2, &key_length );
					json child{};
					if ( !lua_to_json( state, -1, child, depth + 1 ) ) { lua_pop( state, 2 ); return false; }
					out[ std::string( key, key_length ) ] = std::move( child );
					lua_pop( state, 1 );
				}
				return true;
			}
			default: return false;
			}
		}

		bool set_json_path( json& root, std::string_view path, const json& value )
		{
			json* current = &root;
			std::size_t begin{};
			while ( begin < path.size( ) )
			{
				const auto end = path.find( '.', begin );
				const auto token = path.substr( begin,
					end == std::string_view::npos ? path.size( ) - begin : end - begin );
				if ( token.empty( ) ) return false;
				const auto final = end == std::string_view::npos;
				if ( current->is_object( ) )
				{
					const auto it = current->find( std::string( token ) );
					if ( it == current->end( ) ) return false;
					if ( final )
					{
						if ( it->type( ) != value.type( )
							&& !( it->is_number( ) && value.is_number( ) ) ) return false;
						*it = value;
						return true;
					}
					current = &( *it );
				}
				else if ( current->is_array( ) )
				{
					std::size_t index{};
					const auto [ pointer, error ] = std::from_chars(
						token.data( ), token.data( ) + token.size( ), index );
					if ( error != std::errc{} || pointer != token.data( ) + token.size( )
						|| index >= current->size( ) ) return false;
					if ( final )
					{
						if ( ( *current )[ index ].type( ) != value.type( )
							&& !( ( *current )[ index ].is_number( ) && value.is_number( ) ) ) return false;
						( *current )[ index ] = value;
						return true;
					}
					current = &( *current )[ index ];
				}
				else return false;
				begin = end + 1;
			}
			return false;
		}

		json describe_json( const json& value, const int depth = 0 )
		{
			if ( depth > 32 ) return { { "type", "depth-limit" } };
			if ( value.is_object( ) )
			{
				json properties = json::object( );
				for ( const auto& [ key, child ] : value.items( ) )
					properties[ key ] = describe_json( child, depth + 1 );
				return { { "type", "object" }, { "properties", std::move( properties ) } };
			}
			if ( value.is_array( ) )
				return { { "type", "array" }, { "size", value.size( ) },
					{ "items", value.empty( ) ? json{ { "type", "unknown" } }
						: describe_json( value.front( ), depth + 1 ) } };
			if ( value.is_boolean( ) ) return { { "type", "boolean" } };
			if ( value.is_number_integer( ) || value.is_number_unsigned( ) )
				return { { "type", "integer" } };
			if ( value.is_number_float( ) ) return { { "type", "number" } };
			if ( value.is_string( ) ) return { { "type", "string" } };
			return { { "type", "null" } };
		}

		std::uint64_t projectile_id( const game::projectile_snapshot& projectile )
		{

			const auto spawn = static_cast<std::uint64_t>( std::llround(
				static_cast<double>( projectile.spawn_time ) * 1000.0 ) );
			auto value = spawn ^ ( static_cast<std::uint64_t>( projectile.thrower_handle ) << 24 )
				^ ( static_cast<std::uint64_t>( projectile.subtype ) << 56 );
			value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ULL;
			value ^= value >> 27; value *= 0x94d049bb133111ebULL;
			return value ^ ( value >> 31 );
		}

		const json* get_json_path( const json& root, std::string_view path )
		{
			const json* current = &root;
			std::size_t begin{};
			while ( begin < path.size( ) )
			{
				const auto end = path.find( '.', begin );
				const auto token = path.substr( begin,
					end == std::string_view::npos ? path.size( ) - begin : end - begin );
				if ( current->is_object( ) )
				{
					const auto it = current->find( std::string( token ) );
					if ( it == current->end( ) ) return nullptr;
					current = &( *it );
				}
				else if ( current->is_array( ) )
				{
					std::size_t index{};
					const auto [ pointer, error ] = std::from_chars(
						token.data( ), token.data( ) + token.size( ), index );
					if ( error != std::errc{} || pointer != token.data( ) + token.size( )
						|| index >= current->size( ) ) return nullptr;
					current = &( *current )[ index ];
				}
				else return nullptr;
				if ( end == std::string_view::npos ) break;
				begin = end + 1;
			}
			return current;
		}

		std::string local_player_name( )
		{
			static std::uintptr_t cached_controller{};
			static std::string cached_name{};
			static clock::time_point next_refresh{};
			const auto controller = game::local_player( ).controller( );
			if ( !controller )
			{
				cached_controller = 0;
				cached_name.clear( );
				next_refresh = {};
				return {};
			}
			const auto now = clock::now( );
			if ( controller == cached_controller && now < next_refresh ) return cached_name;
			cached_controller = controller;
			next_refresh = now + std::chrono::seconds( 1 );
			const auto name_offset = SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_id );
			if ( !name_offset ) return cached_name;
			const auto name_pointer = app::context( ).process.load<std::uintptr_t>(
				controller + name_offset );
			if ( const auto name = name_pointer
				? app::context( ).process.load_text( name_pointer, 128 ) : std::string{};
				!name.empty( ) ) cached_name = name;
			return cached_name;
		}

	}

	struct runtime_t::implementation
	{
		struct frame
		{
			std::uint64_t sequence{};
			clock::time_point timestamp{};
			std::uint32_t width{};
			std::uint32_t height{};
			std::string map{};
			bool game_valid{};
			bool menu_open{};
			bool input_ready{};
			bool local_alive{};
			std::string local_name{};
			std::int32_t local_team{};
			std::int32_t local_health{};
			std::uint32_t local_tick_base{};
			std::uint32_t local_weapon_type{};
			float local_game_time{};
			float local_flash_alpha{};
			game::presentation_camera_sample camera{};
			std::shared_ptr<const std::vector<game::player_snapshot>> players{};
			std::shared_ptr<const std::vector<game::world_item_snapshot>> items{};
			std::shared_ptr<const std::vector<game::projectile_snapshot>> projectiles{};
			std::shared_ptr<const std::vector<game::spectator_snapshot>> spectators{};
			features::visuals::bomb_t::hud_snapshot bomb{};
			features::visuals::bomb_t::damage_snapshot bomb_damage{};
			features::visuals::bullet_impacts_t::confirmed_hit confirmed_hit{};
		};

		enum class draw_kind : std::uint8_t
		{
			line,
			rect,
			filled_rect,
			circle,
			filled_circle,
			text,
			world_line,
			world_text,
			image,
			panel
		};

		struct draw_command
		{
			draw_kind kind{};
			foundation::vec3 a{};
			foundation::vec3 b{};
			float width{};
			float height{};
			float radius{};
			float thickness{ 1.0f };
			zdraw::rgba color{ 255, 255, 255, 255 };
			std::string text{};
			std::string texture{};
		};

		struct config_patch
		{
			std::string path{};
			json value{};
		};

		struct script_instance;

		std::filesystem::path root{};
		std::filesystem::path scripts_root{};
		std::filesystem::path modules_root{};
		std::filesystem::path data_root{};
		std::filesystem::path logs_root{};
		mutable std::mutex scripts_mutex{};
		std::vector<std::shared_ptr<script_instance>> instances{};
		std::atomic<std::shared_ptr<const frame>> current_frame{};
		std::atomic<std::shared_ptr<const json>> config_snapshot{};
		std::atomic<std::uint64_t> frame_sequence{};
		std::atomic<std::uint64_t> config_revision{};
		clock::time_point last_frame_publish{};
		clock::time_point last_config_publish{};
		std::mutex config_mutex{};
		std::vector<config_patch> config_patches{};
		std::atomic_bool initialized{};
		struct texture_entry
		{
			std::filesystem::path path{};
			ID3D11ShaderResourceView* view{};
			std::uint32_t width{};
			std::uint32_t height{};
			bool attempted{};
		};
		std::mutex textures_mutex{};
		std::unordered_map<std::string, texture_entry> textures{};

		[[nodiscard]] std::filesystem::path state_path( ) const
		{
			return root / L"runtime.json";
		}

		void load_runtime_state( );
		void save_runtime_state( ) const;
		void queue_config( std::string path, json value )
		{
			std::scoped_lock lock( config_mutex );
			config_patches.push_back( { std::move( path ), std::move( value ) } );
		}

		struct script_instance : std::enable_shared_from_this<script_instance>
		{
			implementation& owner;
			std::string id{};
			std::string name{};
			std::string version{ "1.0" };
			std::string author{};
			std::string description{};
			int api_major{ 1 };
			std::filesystem::path path{};
			std::filesystem::file_time_type write_time{};
			std::atomic<script_state> state{ script_state::stopped };
			std::atomic_bool enabled{};
			std::atomic_bool autoload{};
			std::atomic_bool hot_reload{ true };
			std::atomic_bool reload_requested{};
			std::atomic_bool release_requested{};
			std::atomic_size_t memory_bytes{};
			std::atomic<double> last_callback_ms{};
			std::atomic<std::uint32_t> data_demand{};
			std::atomic_bool config_demand{};
			bool frame_events_requested{};
			mutable std::mutex error_mutex{};
			std::string error{};
			std::jthread worker{};

			lua_State* lua{};
			clock::time_point callback_deadline{};
			std::size_t allocation_bytes{};
			struct callback_entry { std::uint64_t token{}; int reference{ LUA_NOREF }; };
			std::unordered_map<std::string, std::vector<callback_entry>> callbacks{};
			std::uint64_t next_callback_token{ 1 };
			mutable std::mutex controls_mutex{};
			std::vector<ui_control> controls{};
			std::unordered_map<std::string, std::uint64_t> button_presses{};
			std::atomic<std::shared_ptr<const std::vector<draw_command>>> published_draw{
				std::make_shared<const std::vector<draw_command>>( ) };
			std::vector<draw_command> building_draw{};
			std::unordered_set<std::uint16_t> held_keys{};
			bool primary_down{};
			bool secondary_down{};
			json storage{ json::object( ) };
			std::uint64_t observed_frame{};
			std::uint64_t observed_config{};
			std::string observed_map{};
			std::unordered_map<std::uint32_t, int> observed_health{};
			std::uint64_t observed_hit{};
			bool observed_connected{};
			bool observed_bomb_planted{};
			bool observed_bomb_defusing{};
			std::unordered_set<std::uint32_t> observed_players{};
			std::unordered_map<std::uint32_t, std::uint32_t> observed_weapons{};
			std::unordered_map<std::uint64_t, bool> observed_projectiles{};
			bool provisional{};

			explicit script_instance( implementation& runtime ) : owner( runtime ) {}
			~script_instance( ) { stop( ); }

			void start( );
			void request_stop( ) noexcept;
			void stop( ) noexcept;
			void run( std::stop_token stop );
			bool create_state( );
			bool replace_state( );
			void destroy_state( ) noexcept;
			void release_input( ) noexcept;
			void set_error( std::string value, script_state next = script_state::error );
			void emit( std::string_view event_name,
				const std::function<void( lua_State* )>& push_argument = {} );
			bool invoke_ref( int reference, int arguments );
			void dispatch_frame( const std::shared_ptr<const frame>& value );
			void register_api( );
			void load_storage( );
			void save_storage( ) const;

			[[nodiscard]] script_info info( ) const;
			[[nodiscard]] std::vector<ui_control> controls_snapshot( ) const;

			static script_instance* self( lua_State* state )
			{
				return *static_cast<script_instance**>( lua_getextraspace( state ) );
			}

			static void* allocate( void* userdata, void* pointer,
				size_t old_size, size_t new_size );
			static void hook( lua_State* state, lua_Debug* debug );

			static int api_log( lua_State* state );
			static int api_events_on( lua_State* state );
			static int api_events_off( lua_State* state );
			static int api_game_snapshot( lua_State* state );
			static int api_game_radar_snapshot( lua_State* state );
			static int api_world_to_screen( lua_State* state );
			static int api_trace_ray( lua_State* state );
			static int api_bomb_damage( lua_State* state );
			static int api_predict_grenade( lua_State* state );
			static int api_radar_overview( lua_State* state );
			static int api_penetration_damage( lua_State* state );
			static int api_config_get( lua_State* state );
			static int api_config_set( lua_State* state );
			static int api_config_patch( lua_State* state );
			static int api_config_snapshot( lua_State* state );
			static int api_config_schema( lua_State* state );
		static int api_storage_get( lua_State* state );
		static int api_storage_set( lua_State* state );
		static int api_storage_remove( lua_State* state );
		static int api_helpers_start( lua_State* state );
		static int api_helpers_copy_text( lua_State* state );
		static int api_assets_weapon_font( lua_State* state );
			static int api_ui_text( lua_State* state );
			static int api_ui_button( lua_State* state );
			static int api_ui_toggle( lua_State* state );
			static int api_ui_slider( lua_State* state );
			static int api_ui_select( lua_State* state );
			static int api_ui_input( lua_State* state );
			static int api_ui_color( lua_State* state );
			static int api_ui_keybind( lua_State* state );
			static int api_ui_separator( lua_State* state );
			static int api_ui_get( lua_State* state );
			static int api_ui_consume( lua_State* state );
			static int api_draw_line( lua_State* state );
			static int api_draw_rect( lua_State* state );
			static int api_draw_filled_rect( lua_State* state );
			static int api_draw_circle( lua_State* state );
			static int api_draw_filled_circle( lua_State* state );
			static int api_draw_text( lua_State* state );
			static int api_draw_world_line( lua_State* state );
			static int api_draw_world_text( lua_State* state );
			static int api_draw_load_texture( lua_State* state );
			static int api_draw_image( lua_State* state );
			static int api_draw_panel( lua_State* state );
			static int api_input_is_down( lua_State* state );
			static int api_input_binding( lua_State* state );
			static int api_input_key( lua_State* state );
			static int api_input_tap( lua_State* state );
			static int api_input_mouse_move( lua_State* state );
			static int api_input_mouse_button( lua_State* state );
		};
	};

	void* runtime_t::implementation::script_instance::allocate(
		void* userdata, void* pointer, const size_t old_size, const size_t new_size )
	{
		auto* script = static_cast<script_instance*>( userdata );

		const auto previous_size = pointer ? old_size : 0u;
		if ( new_size == 0 )
		{
			std::free( pointer );
			script->allocation_bytes = previous_size > script->allocation_bytes
				? 0 : script->allocation_bytes - previous_size;
			script->memory_bytes.store( script->allocation_bytes,
				std::memory_order_relaxed );
			return nullptr;
		}
		const auto base = previous_size > script->allocation_bytes
			? 0 : script->allocation_bytes - previous_size;
		if ( base + new_size > k_memory_limit ) return nullptr;
		const auto result = std::realloc( pointer, new_size );
		if ( result )
		{
			script->allocation_bytes = base + new_size;
			script->memory_bytes.store( script->allocation_bytes,
				std::memory_order_relaxed );
		}
		return result;
	}

	void runtime_t::implementation::script_instance::hook(
		lua_State* state, lua_Debug* )
	{
		auto* script = self( state );
		if ( script && clock::now( ) > script->callback_deadline )
			luaL_error( state, "Vesta Lua callback exceeded its execution budget" );
	}

	void runtime_t::implementation::script_instance::set_error(
		std::string value, const script_state next )
	{
		const auto persistent_message = value;
		{
			std::scoped_lock lock( error_mutex );
			error = std::move( value );
		}
		state.store( next, std::memory_order_release );
		try
		{
			std::error_code error_code{};
			std::filesystem::create_directories( owner.logs_root, error_code );
			std::ofstream stream( owner.logs_root / std::filesystem::u8path( id + ".log" ),
				std::ios::app );
			if ( stream ) stream << persistent_message << '\n';
		}
		catch ( ... ) {}
	}

	script_info runtime_t::implementation::script_instance::info( ) const
	{
		script_info result{};
		result.id = id;
		result.name = name;
		result.version = version;
		result.author = author;
		result.description = description;
		result.state = state.load( std::memory_order_acquire );
		result.enabled = enabled.load( std::memory_order_acquire );
		result.autoload = autoload.load( std::memory_order_acquire );
		result.hot_reload = hot_reload.load( std::memory_order_acquire );
		result.memory_bytes = memory_bytes.load( std::memory_order_relaxed );
		result.last_callback_ms = last_callback_ms.load( std::memory_order_relaxed );
		{
			std::scoped_lock lock( error_mutex );
			result.error = error;
		}
		return result;
	}

	std::vector<ui_control>
	runtime_t::implementation::script_instance::controls_snapshot( ) const
	{
		std::scoped_lock lock( controls_mutex );
		return controls;
	}

	void runtime_t::implementation::script_instance::start( )
	{
		if ( api_major != 1 )
		{
			set_error( std::format( "unsupported Lua API major version {}", api_major ) );
			return;
		}
		if ( worker.joinable( ) )
		{
			if ( state.load( std::memory_order_acquire ) != script_state::stopped ) return;
			worker.join( );
		}
		if ( enabled.exchange( true ) ) return;
		state.store( script_state::starting, std::memory_order_release );
		worker = std::jthread( [ this ]( const std::stop_token stop ) { run( stop ); } );
	}

	void runtime_t::implementation::script_instance::request_stop( ) noexcept
	{
		enabled.store( false, std::memory_order_release );
		if ( worker.joinable( ) ) worker.request_stop( );
	}

	void runtime_t::implementation::script_instance::stop( ) noexcept
	{
		request_stop( );
		if ( worker.joinable( ) )
		{
			worker.request_stop( );
			if ( worker.get_id( ) != std::this_thread::get_id( ) ) worker.join( );
		}
		release_input( );
		state.store( script_state::stopped, std::memory_order_release );
	}

	void runtime_t::implementation::script_instance::release_input( ) noexcept
	{
		for ( const auto key : held_keys ) app::context().input.key( key, false );
		held_keys.clear( );
		auto actions = platform::windows::pointer_action::none;
		if ( primary_down ) actions = actions | platform::windows::pointer_action::primary_up;
		if ( secondary_down ) actions = actions | platform::windows::pointer_action::secondary_up;
		if ( actions != platform::windows::pointer_action::none )
			app::context().input.pointer( 0, 0, actions );
		primary_down = false;
		secondary_down = false;
	}

	void runtime_t::implementation::script_instance::load_storage( )
	{
		storage = json::object( );
		try
		{
			const auto file = owner.data_root / std::filesystem::u8path( id ) / L"store.json";
			std::ifstream stream( file );
			if ( stream ) stream >> storage;
			if ( !storage.is_object( ) ) storage = json::object( );
		}
		catch ( ... ) { storage = json::object( ); }
	}

	void runtime_t::implementation::script_instance::save_storage( ) const
	{
		if ( provisional ) return;
		try
		{
			const auto directory = owner.data_root / std::filesystem::u8path( id );
			std::error_code error_code{};
			std::filesystem::create_directories( directory, error_code );
			if ( error_code ) return;
			const auto target = directory / L"store.json";
			auto pending = target;
			pending += L".tmp";
			{
				std::ofstream stream( pending, std::ios::trunc );
				if ( !stream ) return;
				stream << storage.dump( 2 );
			}
			::MoveFileExW( pending.c_str( ), target.c_str( ),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH );
		}
		catch ( ... ) {}
	}

	bool runtime_t::implementation::script_instance::invoke_ref(
		const int reference, const int arguments )
	{
		if ( !lua ) return false;
		const auto begin = clock::now( );
		callback_deadline = begin + std::chrono::milliseconds( 8 );
		lua_sethook( lua, hook, LUA_MASKCOUNT, 10000 );
		const auto result = lua_pcall( lua, arguments, 0, 0 );
		lua_sethook( lua, nullptr, 0, 0 );
		last_callback_ms.store(
			std::chrono::duration<double, std::milli>( clock::now( ) - begin ).count( ),
			std::memory_order_relaxed );
		if ( result == LUA_OK ) return true;
		const auto* message = lua_tostring( lua, -1 );
		set_error( message ? message : "unknown Lua callback error",
			result == LUA_ERRMEM ? script_state::over_budget : script_state::error );
		lua_pop( lua, 1 );
		return false;
	}

	void runtime_t::implementation::script_instance::emit(
		const std::string_view event_name,
		const std::function<void( lua_State* )>& push_argument )
	{
		if ( !lua ) return;
		const auto found = callbacks.find( std::string( event_name ) );
		if ( found == callbacks.end( ) ) return;
		std::vector<std::uint64_t> tokens{};
		tokens.reserve( found->second.size( ) );
		for ( const auto& callback : found->second ) tokens.push_back( callback.token );
		for ( const auto token : tokens )
		{
			const auto current = callbacks.find( std::string( event_name ) );
			if ( current == callbacks.end( ) ) break;
			const auto callback = std::ranges::find( current->second, token,
				&callback_entry::token );
			if ( callback == current->second.end( ) ) continue;
			const auto reference = callback->reference;
			lua_rawgeti( lua, LUA_REGISTRYINDEX, reference );
			int arguments{};
			if ( push_argument ) { push_argument( lua ); arguments = 1; }
			if ( !invoke_ref( reference, arguments ) ) break;
		}
	}

	bool runtime_t::implementation::script_instance::create_state( )
	{
		allocation_bytes = 0;
		lua = lua_newstate( allocate, this );
		if ( !lua ) { set_error( "unable to create Lua state", script_state::over_budget ); return false; }
		*static_cast<script_instance**>( lua_getextraspace( lua ) ) = this;

		const luaL_Reg libraries[]{
			{ LUA_GNAME, luaopen_base }, { LUA_LOADLIBNAME, luaopen_package },
			{ LUA_COLIBNAME, luaopen_coroutine }, { LUA_TABLIBNAME, luaopen_table },
			{ LUA_IOLIBNAME, luaopen_io }, { LUA_OSLIBNAME, luaopen_os },
			{ LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
			{ LUA_UTF8LIBNAME, luaopen_utf8 }, { nullptr, nullptr }
		};
		for ( const auto* library = libraries; library->name; ++library )
		{
			luaL_requiref( lua, library->name, library->func, 1 );
			lua_pop( lua, 1 );
		}

		lua_getglobal( lua, "package" );
		const auto script_directory = path.parent_path( );
		const auto lua_path = path_utf8( script_directory / L"?.lua" ) + ";"
			+ path_utf8( script_directory / L"?" / L"init.lua" ) + ";"
			+ path_utf8( owner.modules_root / L"?.lua" ) + ";"
			+ path_utf8( owner.modules_root / L"?" / L"init.lua" );
		lua_pushlstring( lua, lua_path.data( ), lua_path.size( ) );
		lua_setfield( lua, -2, "path" );
		lua_pushliteral( lua, "" ); lua_setfield( lua, -2, "cpath" );
		lua_pushnil( lua ); lua_setfield( lua, -2, "loadlib" );
		lua_getfield( lua, -1, "searchers" );
		if ( lua_istable( lua, -1 ) )
		{
			lua_pushnil( lua ); lua_rawseti( lua, -2, 3 );
			lua_pushnil( lua ); lua_rawseti( lua, -2, 4 );
		}
		lua_pop( lua, 2 );

		register_api( );
		load_storage( );
		{
			std::scoped_lock lock( controls_mutex );
			controls.clear( );
			button_presses.clear( );
		}
		callbacks.clear( );
		building_draw.clear( );

		const auto file = path_utf8( path );
		if ( luaL_loadfilex( lua, file.c_str( ), "t" ) != LUA_OK )
		{
			const auto* message = lua_tostring( lua, -1 );
			set_error( message ? message : "unable to load Lua source" );
			lua_pop( lua, 1 );
			return false;
		}
		if ( !invoke_ref( LUA_NOREF, 0 ) ) return false;
		state.store( script_state::running, std::memory_order_release );
		{
			std::scoped_lock lock( error_mutex );
			error.clear( );
		}
		if ( !provisional ) emit( "load" );
		return true;
	}

	bool runtime_t::implementation::script_instance::replace_state( )
	{

		save_storage( );
		script_instance candidate{ owner };
		candidate.provisional = true;
		candidate.id = id; candidate.name = name; candidate.version = version;
		candidate.author = author; candidate.description = description;
		candidate.api_major = api_major; candidate.path = path;
		if ( !candidate.create_state( ) )
		{
			const auto details = candidate.info().error;
			candidate.destroy_state( );
			std::scoped_lock lock( error_mutex );
			error = std::format( "Reload failed: {}", details );
			return false;
		}

		destroy_state( );
		lua = candidate.lua;
		candidate.lua = nullptr;
		allocation_bytes = candidate.allocation_bytes;
		candidate.allocation_bytes = 0;
		memory_bytes.store( allocation_bytes, std::memory_order_relaxed );
		data_demand.store( candidate.data_demand.load( std::memory_order_acquire ),
			std::memory_order_release );
		config_demand.store( candidate.config_demand.load( std::memory_order_acquire ),
			std::memory_order_release );
		frame_events_requested = candidate.frame_events_requested;
		lua_setallocf( lua, allocate, this );
		*static_cast<script_instance**>( lua_getextraspace( lua ) ) = this;
		callbacks = std::move( candidate.callbacks );
		storage = std::move( candidate.storage );
		building_draw = std::move( candidate.building_draw );
		published_draw.store( candidate.published_draw.load( std::memory_order_acquire ),
			std::memory_order_release );
		{
			std::scoped_lock lock( controls_mutex, candidate.controls_mutex );
			controls = std::move( candidate.controls );
			button_presses = std::move( candidate.button_presses );
		}
		provisional = false;
		state.store( script_state::running, std::memory_order_release );
		{
			std::scoped_lock lock( error_mutex ); error.clear( );
		}
		emit( "load" );
		return true;
	}

	void runtime_t::implementation::script_instance::destroy_state( ) noexcept
	{
		if ( !lua ) return;
		if ( !provisional ) emit( "unload" );
		release_input( );
		save_storage( );
		for ( auto& [ _, entries ] : callbacks )
			for ( const auto& callback : entries )
				luaL_unref( lua, LUA_REGISTRYINDEX, callback.reference );
		callbacks.clear( );
		lua_close( lua );
		lua = nullptr;
		published_draw.store( std::make_shared<const std::vector<draw_command>>( ),
			std::memory_order_release );
	}

	void runtime_t::implementation::script_instance::run( const std::stop_token stop )
	{
		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_BELOW_NORMAL );
		try { write_time = std::filesystem::last_write_time( path ); } catch ( ... ) {}
		if ( create_state( ) ) emit( "load" );
		auto next_tick = clock::now( );
		auto next_probe = next_tick + k_reload_probe_period;
		while ( !stop.stop_requested( ) && enabled.load( std::memory_order_acquire ) )
		{
			const auto now = clock::now( );
			if ( release_requested.exchange( false ) ) release_input( );
			if ( hot_reload.load( std::memory_order_relaxed ) && now >= next_probe )
			{
				next_probe = now + k_reload_probe_period;
				try
				{
					const auto current = std::filesystem::last_write_time( path );
					if ( current != write_time ) { write_time = current; reload_requested.store( true ); }
				}
				catch ( ... ) {}
			}
			if ( reload_requested.exchange( false ) )
			{
				replace_state( );
			}
			if ( lua && state.load( std::memory_order_acquire ) == script_state::running )
			{
				emit( "tick", []( lua_State* target ) { lua_pushnumber( target, 0.016 ); } );
				const auto current = owner.current_frame.load( std::memory_order_acquire );
				if ( current && current->sequence != observed_frame ) dispatch_frame( current );
				const auto config = owner.config_revision.load( std::memory_order_acquire );
				if ( config != observed_config )
				{
					observed_config = config;
					emit( "config_changed" );
				}
			}
			next_tick += k_tick_period;
			if ( next_tick <= clock::now( ) ) next_tick = clock::now( ) + k_tick_period;
			std::this_thread::sleep_until( next_tick );
		}
		destroy_state( );
		state.store( script_state::stopped, std::memory_order_release );
	}

	namespace {

		void push_weapon( lua_State* state, const game::equipped_weapon_snapshot& weapon )
		{
			lua_createtable( state, 0, 6 );
			lua_pushinteger( state, weapon.handle ); lua_setfield( state, -2, "handle" );
			lua_pushinteger( state, weapon.item_definition ); lua_setfield( state, -2, "item_definition" );
			lua_pushlstring( state, weapon.name.data( ), weapon.name.size( ) ); lua_setfield( state, -2, "name" );
			lua_pushinteger( state, weapon.ammo ); lua_setfield( state, -2, "ammo" );
			lua_pushinteger( state, weapon.max_ammo ); lua_setfield( state, -2, "max_ammo" );
			lua_pushboolean( state, weapon.pin_pulled ); lua_setfield( state, -2, "pin_pulled" );
		}

		void push_player( lua_State* state, const game::player_snapshot& player )
		{
			lua_createtable( state, 0, 44 );
			lua_pushinteger( state, player.pawn_handle ); lua_setfield( state, -2, "handle" );
			lua_pushinteger( state, player.controller_index ); lua_setfield( state, -2, "controller_index" );
			lua_pushinteger( state, static_cast<lua_Integer>( player.steamid ) ); lua_setfield( state, -2, "steam_id64" );
			lua_pushlstring( state, player.display_name.data( ), player.display_name.size( ) ); lua_setfield( state, -2, "name" );
			lua_pushinteger( state, player.health ); lua_setfield( state, -2, "health" );
			lua_pushinteger( state, player.armor ); lua_setfield( state, -2, "armor" );
			lua_pushinteger( state, player.team ); lua_setfield( state, -2, "team" );
			lua_pushinteger( state, player.money ); lua_setfield( state, -2, "money" );
			lua_pushinteger( state, player.ping ); lua_setfield( state, -2, "ping" );
			lua_pushinteger( state, player.comp_rank ); lua_setfield( state, -2, "rank" );
			lua_pushinteger( state, player.comp_wins ); lua_setfield( state, -2, "wins" );
			lua_pushinteger( state, player.comp_rank_type ); lua_setfield( state, -2, "rank_type" );
			lua_pushinteger( state, player.simulation_tick ); lua_setfield( state, -2, "simulation_tick" );
			lua_pushnumber( state, player.simulation_time ); lua_setfield( state, -2, "simulation_time" );
			lua_pushnumber( state, player.last_fired_time ); lua_setfield( state, -2, "last_fired_time" );
			lua_pushnumber( state, player.emit_sound_time ); lua_setfield( state, -2, "emit_sound_time" );
			push_vec3( state, player.origin ); lua_setfield( state, -2, "origin" );
			push_vec3( state, player.velocity ); lua_setfield( state, -2, "velocity" );
			push_vec3( state, player.collision_center ); lua_setfield( state, -2, "collision_center" );
			push_vec3( state, player.eye_angles ); lua_setfield( state, -2, "eye_angles" );
			lua_pushlstring( state, player.model_path.data( ), player.model_path.size( ) ); lua_setfield( state, -2, "model_path" );
			push_weapon( state, player.weapon ); lua_setfield( state, -2, "weapon" );
			lua_createtable( state, static_cast<int>( player.loadout.size( ) ), 0 );
			for ( std::size_t index = 0; index < player.loadout.size( ); ++index )
			{
				lua_pushlstring( state, player.loadout[ index ].data( ), player.loadout[ index ].size( ) );
				lua_rawseti( state, -2, static_cast<lua_Integer>( index + 1 ) );
			}
			lua_setfield( state, -2, "loadout" );
			const std::pair<const char*, bool> flags[]{
				{ "invulnerable", player.invulnerable }, { "helmet", player.has_helmet },
				{ "defuser", player.has_defuser }, { "scoped", player.is_scoped },
				{ "defusing", player.is_defusing }, { "ducked", player.is_ducked },
				{ "flashed", player.is_flashed }, { "visible", player.is_visible },
				{ "spotted", player.is_spotted }, { "legit_visible", player.legit_visible }
			};
			for ( const auto& [ name, value ] : flags )
			{
				lua_pushboolean( state, value ); lua_setfield( state, -2, name );
			}
			lua_pushinteger( state, static_cast<lua_Integer>( player.spotted_by_mask ) );
			lua_setfield( state, -2, "spotted_by_mask" );

			lua_createtable( state, 128, 0 );
			for ( std::size_t i = 0; i < player.bones.bones.size( ); ++i )
			{
				push_vec3( state, player.bones.bones[ i ].position );
				lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
			}
			lua_setfield( state, -2, "bones" );

			lua_createtable( state, player.hitboxes.count, 0 );
			int output_index{ 1 };
			for ( const auto& hitbox : player.hitboxes )
			{
				lua_createtable( state, 0, 7 );
				lua_pushinteger( state, hitbox.index ); lua_setfield( state, -2, "index" );
				lua_pushinteger( state, hitbox.bone ); lua_setfield( state, -2, "bone" );
				lua_pushstring( state, hitbox.name.data( ) ); lua_setfield( state, -2, "name" );
				push_vec3( state, hitbox.mins ); lua_setfield( state, -2, "mins" );
				push_vec3( state, hitbox.maxs ); lua_setfield( state, -2, "maxs" );
				lua_pushnumber( state, hitbox.radius ); lua_setfield( state, -2, "radius" );
				lua_rawseti( state, -2, output_index++ );
			}
			lua_setfield( state, -2, "hitboxes" );
		}

		void push_frame( lua_State* state,
			const runtime_t::implementation::frame& value )
		{
			lua_createtable( state, 0, 14 );
			lua_pushinteger( state, static_cast<lua_Integer>( value.sequence ) ); lua_setfield( state, -2, "sequence" );
			lua_pushinteger( state, static_cast<lua_Integer>( std::chrono::duration_cast<std::chrono::microseconds>( value.timestamp.time_since_epoch( ) ).count( ) ) ); lua_setfield( state, -2, "timestamp_us" );
			lua_pushboolean( state, value.game_valid ); lua_setfield( state, -2, "connected" );
			lua_pushboolean( state, value.menu_open ); lua_setfield( state, -2, "menu_open" );
			lua_pushboolean( state, value.input_ready ); lua_setfield( state, -2, "input_ready" );
			lua_pushlstring( state, value.map.data( ), value.map.size( ) ); lua_setfield( state, -2, "map" );
			lua_createtable( state, 0, 2 );
			lua_pushinteger( state, value.width ); lua_setfield( state, -2, "width" );
			lua_pushinteger( state, value.height ); lua_setfield( state, -2, "height" );
			lua_setfield( state, -2, "screen" );

			lua_createtable( state, 0, 5 );
			push_vec3( state, value.camera.origin ); lua_setfield( state, -2, "origin" );
			push_vec3( state, value.camera.angles ); lua_setfield( state, -2, "angles" );
			lua_pushnumber( state, value.camera.fov ); lua_setfield( state, -2, "fov" );
			lua_createtable( state, 16, 0 );
			for ( int row = 0; row < 4; ++row ) for ( int column = 0; column < 4; ++column )
			{
				lua_pushnumber( state, value.camera.matrix[ row ][ column ] );
				lua_rawseti( state, -2, row * 4 + column + 1 );
			}
			lua_setfield( state, -2, "matrix" );
			lua_setfield( state, -2, "camera" );

			lua_createtable( state, 0, 9 );
			lua_pushboolean( state, value.local_alive ); lua_setfield( state, -2, "alive" );
			lua_pushlstring( state, value.local_name.data( ), value.local_name.size( ) ); lua_setfield( state, -2, "name" );
			lua_pushinteger( state, value.local_team ); lua_setfield( state, -2, "team" );
			lua_pushinteger( state, value.local_health ); lua_setfield( state, -2, "health" );
			lua_pushinteger( state, value.local_tick_base ); lua_setfield( state, -2, "tick_base" );
			lua_pushnumber( state, value.local_game_time ); lua_setfield( state, -2, "game_time" );
			lua_pushnumber( state, value.local_flash_alpha ); lua_setfield( state, -2, "flash_alpha" );
			lua_pushinteger( state, value.local_weapon_type ); lua_setfield( state, -2, "weapon_type" );
			lua_setfield( state, -2, "local_player" );

			lua_createtable( state, value.players ? static_cast<int>( value.players->size( ) ) : 0, 0 );
			if ( value.players ) for ( std::size_t i = 0; i < value.players->size( ); ++i )
			{
				push_player( state, ( *value.players )[ i ] );
				lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
			}
			lua_setfield( state, -2, "players" );

			lua_createtable( state, value.items ? static_cast<int>( value.items->size( ) ) : 0, 0 );
			if ( value.items ) for ( std::size_t i = 0; i < value.items->size( ); ++i )
			{
				const auto& item = ( *value.items )[ i ];
				lua_createtable( state, 0, 4 );
				lua_pushinteger( state, static_cast<int>( item.subtype ) ); lua_setfield( state, -2, "kind" );
				push_vec3( state, item.origin ); lua_setfield( state, -2, "origin" );
				lua_pushinteger( state, item.ammo ); lua_setfield( state, -2, "ammo" );
				lua_pushinteger( state, item.max_ammo ); lua_setfield( state, -2, "max_ammo" );
				lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
			}
			lua_setfield( state, -2, "items" );

			lua_createtable( state, value.projectiles ? static_cast<int>( value.projectiles->size( ) ) : 0, 0 );
			if ( value.projectiles ) for ( std::size_t i = 0; i < value.projectiles->size( ); ++i )
			{
				const auto& projectile = ( *value.projectiles )[ i ];
				lua_createtable( state, 0, 20 );
				lua_pushinteger( state, static_cast<lua_Integer>( projectile_id( projectile ) ) ); lua_setfield( state, -2, "id" );
				lua_pushinteger( state, static_cast<int>( projectile.subtype ) ); lua_setfield( state, -2, "kind" );
				push_vec3( state, projectile.origin ); lua_setfield( state, -2, "origin" );
				push_vec3( state, projectile.velocity ); lua_setfield( state, -2, "velocity" );
				push_vec3( state, projectile.initial_position ); lua_setfield( state, -2, "initial_position" );
				push_vec3( state, projectile.initial_velocity ); lua_setfield( state, -2, "initial_velocity" );
				lua_pushinteger( state, projectile.thrower_handle ); lua_setfield( state, -2, "thrower_handle" );
				lua_pushinteger( state, projectile.bounces ); lua_setfield( state, -2, "bounces" );
				lua_pushinteger( state, projectile.effect_tick_begin ); lua_setfield( state, -2, "effect_tick_begin" );
				lua_pushnumber( state, projectile.spawn_time ); lua_setfield( state, -2, "spawn_time" );
				lua_pushnumber( state, projectile.detonate_time ); lua_setfield( state, -2, "detonate_time" );
				lua_pushnumber( state, projectile.remaining_lifetime ); lua_setfield( state, -2, "remaining_lifetime" );
				lua_pushboolean( state, projectile.detonated ); lua_setfield( state, -2, "detonated" );
				lua_pushboolean( state, projectile.smoke_active ); lua_setfield( state, -2, "smoke_active" );
				push_vec3( state, projectile.smoke_detonation_pos ); lua_setfield( state, -2, "smoke_position" );
				lua_pushinteger( state, projectile.smoke_voxel_size ); lua_setfield( state, -2, "smoke_voxel_size" );
				lua_pushboolean( state, projectile.smoke_volume_received ); lua_setfield( state, -2, "smoke_volume_received" );
				lua_pushnumber( state, projectile.expire_time ); lua_setfield( state, -2, "expire_time" );
				lua_createtable( state, static_cast<int>( projectile.fire_points.size( ) ), 0 );
				for ( std::size_t point = 0; point < projectile.fire_points.size( ); ++point )
				{
					push_vec3( state, projectile.fire_points[ point ] );
					lua_rawseti( state, -2, static_cast<lua_Integer>( point + 1 ) );
				}
				lua_setfield( state, -2, "fire_points" );
				lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
			}
			lua_setfield( state, -2, "projectiles" );

			lua_createtable( state, value.spectators ? static_cast<int>( value.spectators->size( ) ) : 0, 0 );
			if ( value.spectators ) for ( std::size_t i = 0; i < value.spectators->size( ); ++i )
			{
				const auto& spectator = ( *value.spectators )[ i ];
				lua_createtable( state, 0, 3 );
				lua_pushlstring( state, spectator.name.data( ), spectator.name.size( ) ); lua_setfield( state, -2, "name" );
				lua_pushinteger( state, static_cast<lua_Integer>( spectator.steamid ) ); lua_setfield( state, -2, "steam_id64" );
				lua_pushinteger( state, spectator.mode ); lua_setfield( state, -2, "mode" );
				lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
			}
			lua_setfield( state, -2, "spectators" );

			lua_createtable( state, 0, 13 );
			lua_pushboolean( state, value.bomb.active ); lua_setfield( state, -2, "active" );
			push_vec3( state, value.bomb.active_position ); lua_setfield( state, -2, "active_position" );
			lua_pushboolean( state, value.bomb.planted ); lua_setfield( state, -2, "planted" );
			lua_pushnumber( state, value.bomb.time_remaining ); lua_setfield( state, -2, "time_remaining" );
			lua_pushnumber( state, value.bomb.timer_length ); lua_setfield( state, -2, "timer_length" );
			lua_pushboolean( state, value.bomb.being_defused ); lua_setfield( state, -2, "being_defused" );
			lua_pushnumber( state, value.bomb.defuse_remaining ); lua_setfield( state, -2, "defuse_remaining" );
			lua_pushnumber( state, value.bomb.defuse_length ); lua_setfield( state, -2, "defuse_length" );
			lua_pushboolean( state, value.bomb.defuse_success ); lua_setfield( state, -2, "defuse_success" );
			lua_pushinteger( state, value.bomb.bomb_site ); lua_setfield( state, -2, "site" );
			lua_pushinteger( state, value.bomb.predicted_damage ); lua_setfield( state, -2, "predicted_damage" );
			push_vec3( state, value.bomb.position ); lua_setfield( state, -2, "position" );
			lua_setfield( state, -2, "bomb" );
		}

		void push_radar_player( lua_State* state, const game::player_snapshot& player )
		{
			lua_createtable( state, 0, 20 );
			lua_pushinteger( state, player.pawn_handle ); lua_setfield( state, -2, "handle" );
			lua_pushlstring( state, player.display_name.data( ), player.display_name.size( ) ); lua_setfield( state, -2, "name" );
			lua_pushinteger( state, player.health ); lua_setfield( state, -2, "health" );
			lua_pushinteger( state, player.armor ); lua_setfield( state, -2, "armor" );
			lua_pushinteger( state, player.team ); lua_setfield( state, -2, "team" );
			lua_pushinteger( state, player.money ); lua_setfield( state, -2, "money" );
			push_vec3( state, player.origin ); lua_setfield( state, -2, "origin" );
			push_vec3( state, player.velocity ); lua_setfield( state, -2, "velocity" );
			push_vec3( state, player.eye_angles ); lua_setfield( state, -2, "eye_angles" );
			push_weapon( state, player.weapon ); lua_setfield( state, -2, "weapon" );
			lua_createtable( state, static_cast<int>( player.loadout.size( ) ), 0 );
			for ( std::size_t index = 0; index < player.loadout.size( ); ++index )
			{
				lua_pushlstring( state, player.loadout[ index ].data( ), player.loadout[ index ].size( ) );
				lua_rawseti( state, -2, static_cast<lua_Integer>( index + 1 ) );
			}
			lua_setfield( state, -2, "loadout" );
			const std::pair<const char*, bool> flags[]{
				{ "invulnerable", player.invulnerable }, { "helmet", player.has_helmet },
				{ "defuser", player.has_defuser }, { "scoped", player.is_scoped },
				{ "visible", player.is_visible }, { "spotted", player.is_spotted }
			};
			for ( const auto& [ name, enabled ] : flags )
			{
				lua_pushboolean( state, enabled ); lua_setfield( state, -2, name );
			}
		}

		void push_radar_frame( lua_State* state,
			const runtime_t::implementation::frame& value )
		{
			lua_createtable( state, 0, 10 );
			lua_pushinteger( state, static_cast<lua_Integer>( value.sequence ) ); lua_setfield( state, -2, "sequence" );
			lua_pushinteger( state, static_cast<lua_Integer>( std::chrono::duration_cast<std::chrono::microseconds>( value.timestamp.time_since_epoch( ) ).count( ) ) ); lua_setfield( state, -2, "timestamp_us" );
			lua_pushboolean( state, value.game_valid ); lua_setfield( state, -2, "connected" );
			lua_pushlstring( state, value.map.data( ), value.map.size( ) ); lua_setfield( state, -2, "map" );

			lua_createtable( state, 0, 3 );
			push_vec3( state, value.camera.origin ); lua_setfield( state, -2, "origin" );
			push_vec3( state, value.camera.angles ); lua_setfield( state, -2, "angles" );
			lua_pushnumber( state, value.camera.fov ); lua_setfield( state, -2, "fov" );
			lua_setfield( state, -2, "camera" );

			lua_createtable( state, 0, 6 );
			lua_pushboolean( state, value.local_alive ); lua_setfield( state, -2, "alive" );
			lua_pushlstring( state, value.local_name.data( ), value.local_name.size( ) ); lua_setfield( state, -2, "name" );
			lua_pushinteger( state, value.local_team ); lua_setfield( state, -2, "team" );
			lua_pushinteger( state, value.local_health ); lua_setfield( state, -2, "health" );
			lua_pushnumber( state, value.local_game_time ); lua_setfield( state, -2, "game_time" );
			lua_setfield( state, -2, "local_player" );

			lua_createtable( state, value.players ? static_cast<int>( value.players->size( ) ) : 0, 0 );
			if ( value.players ) for ( std::size_t index = 0; index < value.players->size( ); ++index )
			{
				push_radar_player( state, ( *value.players )[ index ] );
				lua_rawseti( state, -2, static_cast<lua_Integer>( index + 1 ) );
			}
			lua_setfield( state, -2, "players" );

			lua_createtable( state, value.items ? static_cast<int>( value.items->size( ) ) : 0, 0 );
			if ( value.items ) for ( std::size_t index = 0; index < value.items->size( ); ++index )
			{
				const auto& item = ( *value.items )[ index ];
				lua_createtable( state, 0, 2 );
				lua_pushinteger( state, static_cast<int>( item.subtype ) ); lua_setfield( state, -2, "kind" );
				push_vec3( state, item.origin ); lua_setfield( state, -2, "origin" );
				lua_rawseti( state, -2, static_cast<lua_Integer>( index + 1 ) );
			}
			lua_setfield( state, -2, "items" );

			lua_createtable( state, value.projectiles ? static_cast<int>( value.projectiles->size( ) ) : 0, 0 );
			if ( value.projectiles ) for ( std::size_t index = 0; index < value.projectiles->size( ); ++index )
			{
				const auto& projectile = ( *value.projectiles )[ index ];
				lua_createtable( state, 0, 18 );
				lua_pushinteger( state, static_cast<lua_Integer>( projectile_id( projectile ) ) ); lua_setfield( state, -2, "id" );
				lua_pushinteger( state, static_cast<int>( projectile.subtype ) ); lua_setfield( state, -2, "kind" );
				push_vec3( state, projectile.origin ); lua_setfield( state, -2, "origin" );
				push_vec3( state, projectile.velocity ); lua_setfield( state, -2, "velocity" );
				push_vec3( state, projectile.initial_position ); lua_setfield( state, -2, "initial_position" );
				push_vec3( state, projectile.initial_velocity ); lua_setfield( state, -2, "initial_velocity" );
				lua_pushinteger( state, projectile.bounces ); lua_setfield( state, -2, "bounces" );
				lua_pushinteger( state, projectile.effect_tick_begin ); lua_setfield( state, -2, "effect_tick_begin" );
				lua_pushnumber( state, projectile.spawn_time ); lua_setfield( state, -2, "spawn_time" );
				lua_pushnumber( state, projectile.detonate_time ); lua_setfield( state, -2, "detonate_time" );
				lua_pushnumber( state, projectile.remaining_lifetime ); lua_setfield( state, -2, "remaining_lifetime" );
				lua_pushnumber( state, projectile.expire_time ); lua_setfield( state, -2, "expire_time" );
				lua_pushboolean( state, projectile.detonated ); lua_setfield( state, -2, "detonated" );
				lua_pushboolean( state, projectile.smoke_active ); lua_setfield( state, -2, "smoke_active" );
				lua_createtable( state, static_cast<int>( projectile.fire_points.size( ) ), 0 );
				for ( std::size_t point = 0; point < projectile.fire_points.size( ); ++point )
				{
					push_vec3( state, projectile.fire_points[ point ] );
					lua_rawseti( state, -2, static_cast<lua_Integer>( point + 1 ) );
				}
				lua_setfield( state, -2, "fire_points" );
				lua_rawseti( state, -2, static_cast<lua_Integer>( index + 1 ) );
			}
			lua_setfield( state, -2, "projectiles" );

			lua_createtable( state, 0, 13 );
			lua_pushboolean( state, value.bomb.active ); lua_setfield( state, -2, "active" );
			push_vec3( state, value.bomb.active_position ); lua_setfield( state, -2, "active_position" );
			lua_pushboolean( state, value.bomb.planted ); lua_setfield( state, -2, "planted" );
			lua_pushnumber( state, value.bomb.time_remaining ); lua_setfield( state, -2, "time_remaining" );
			lua_pushboolean( state, value.bomb.being_defused ); lua_setfield( state, -2, "being_defused" );
			lua_pushnumber( state, value.bomb.defuse_remaining ); lua_setfield( state, -2, "defuse_remaining" );
			lua_pushboolean( state, value.bomb.defuse_success ); lua_setfield( state, -2, "defuse_success" );
			lua_pushinteger( state, value.bomb.bomb_site ); lua_setfield( state, -2, "site" );
			lua_pushinteger( state, value.bomb.predicted_damage ); lua_setfield( state, -2, "predicted_damage" );
			push_vec3( state, value.bomb.position ); lua_setfield( state, -2, "position" );
			lua_setfield( state, -2, "bomb" );
		}

	}

	void runtime_t::implementation::script_instance::dispatch_frame(
		const std::shared_ptr<const frame>& value )
	{
		observed_frame = value->sequence;
		building_draw.clear( );

		if ( !frame_events_requested ) return;
		if ( value->game_valid != observed_connected )
		{
			observed_connected = value->game_valid;
			emit( observed_connected ? "game_connected" : "game_disconnected" );
			if ( !observed_connected )
			{
				observed_health.clear( ); observed_players.clear( );
				observed_weapons.clear( ); observed_projectiles.clear( );
				observed_bomb_planted = false; observed_bomb_defusing = false;
			}
		}

		if ( value->map != observed_map )
		{
			observed_map = value->map;
			emit( "map_changed", [ & ]( lua_State* target )
				{ lua_pushlstring( target, observed_map.data( ), observed_map.size( ) ); } );
		}

		std::unordered_map<std::uint32_t, int> next_health{};
		std::unordered_set<std::uint32_t> next_players{};
		std::unordered_map<std::uint32_t, std::uint32_t> next_weapons{};
		if ( value->players )
		{
			next_health.reserve( value->players->size( ) );
			next_players.reserve( value->players->size( ) );
			next_weapons.reserve( value->players->size( ) );
			for ( const auto& player : *value->players )
			{
				next_health[ player.pawn_handle ] = player.health;
				next_players.insert( player.pawn_handle );
				next_weapons[ player.pawn_handle ] = player.weapon.handle;
				if ( !observed_players.empty( ) && !observed_players.contains( player.pawn_handle ) )
					emit( "player_joined", [ & ]( lua_State* target ) { push_player( target, player ); } );
				const auto old_weapon = observed_weapons.find( player.pawn_handle );
				if ( old_weapon != observed_weapons.end( ) && old_weapon->second != player.weapon.handle )
					emit( "weapon_changed", [ & ]( lua_State* target )
					{
						lua_createtable( target, 0, 2 );
						lua_pushinteger( target, player.pawn_handle ); lua_setfield( target, -2, "handle" );
						push_weapon( target, player.weapon ); lua_setfield( target, -2, "weapon" );
					} );
				const auto previous = observed_health.find( player.pawn_handle );
				if ( previous != observed_health.end( ) && player.health < previous->second )
				{
					const auto damage = previous->second - player.health;
					emit( player.health <= 0 ? "player_killed" : "player_hurt",
						[ & ]( lua_State* target )
						{
							lua_createtable( target, 0, 3 );
							lua_pushinteger( target, player.pawn_handle ); lua_setfield( target, -2, "handle" );
							lua_pushinteger( target, damage ); lua_setfield( target, -2, "damage" );
							lua_pushinteger( target, player.health ); lua_setfield( target, -2, "health" );
						} );
				}
			}
		}
		for ( const auto handle : observed_players ) if ( !next_players.contains( handle ) )
			emit( "player_left", [ & ]( lua_State* target ) { lua_pushinteger( target, handle ); } );
		observed_health = std::move( next_health );
		observed_players = std::move( next_players );
		observed_weapons = std::move( next_weapons );

		std::unordered_map<std::uint64_t, bool> next_projectiles{};
		if ( value->projectiles )
		{
			next_projectiles.reserve( value->projectiles->size( ) );
			for ( const auto& projectile : *value->projectiles )
			{
				const auto id = projectile_id( projectile );
				next_projectiles[ id ] = projectile.detonated || projectile.smoke_active;
				const auto old = observed_projectiles.find( id );
				if ( old == observed_projectiles.end( ) )
					emit( "projectile_created", [ & ]( lua_State* target )
					{
						lua_createtable( target, 0, 4 );
						lua_pushinteger( target, static_cast<lua_Integer>( id ) ); lua_setfield( target, -2, "id" );
						lua_pushinteger( target, static_cast<int>( projectile.subtype ) ); lua_setfield( target, -2, "kind" );
						push_vec3( target, projectile.origin ); lua_setfield( target, -2, "origin" );
						push_vec3( target, projectile.velocity ); lua_setfield( target, -2, "velocity" );
					} );
				else if ( !old->second && next_projectiles[ id ] )
					emit( "projectile_detonated", [ & ]( lua_State* target )
					{
						lua_createtable( target, 0, 3 );
						lua_pushinteger( target, static_cast<lua_Integer>( id ) ); lua_setfield( target, -2, "id" );
						lua_pushinteger( target, static_cast<int>( projectile.subtype ) ); lua_setfield( target, -2, "kind" );
						push_vec3( target, projectile.origin ); lua_setfield( target, -2, "origin" );
					} );
			}
		}
		for ( const auto& [ id, _ ] : observed_projectiles ) if ( !next_projectiles.contains( id ) )
			emit( "projectile_removed", [ & ]( lua_State* target )
				{ lua_pushinteger( target, static_cast<lua_Integer>( id ) ); } );
		observed_projectiles = std::move( next_projectiles );

		if ( value->bomb.planted && !observed_bomb_planted ) emit( "bomb_planted" );
		if ( value->bomb.being_defused && !observed_bomb_defusing ) emit( "bomb_defuse_started" );
		if ( !value->bomb.being_defused && observed_bomb_defusing && value->bomb.planted )
			emit( "bomb_defuse_stopped" );
		if ( !value->bomb.planted && observed_bomb_planted )
			emit( value->bomb.defuse_success ? "bomb_defused" : "bomb_resolved" );
		observed_bomb_planted = value->bomb.planted;
		observed_bomb_defusing = value->bomb.being_defused;
		if ( value->confirmed_hit.sequence && value->confirmed_hit.sequence != observed_hit )
		{
			observed_hit = value->confirmed_hit.sequence;
			emit( "confirmed_hit", [ & ]( lua_State* target )
			{
				lua_createtable( target, 0, 3 );
				lua_pushinteger( target, value->confirmed_hit.damage ); lua_setfield( target, -2, "damage" );
				lua_pushboolean( target, value->confirmed_hit.killed ); lua_setfield( target, -2, "killed" );
			} );
		}

		emit( "frame", [ & ]( lua_State* target ) { push_frame( target, *value ); } );
		published_draw.store(
			std::make_shared<const std::vector<draw_command>>( building_draw ),
			std::memory_order_release );
	}

	namespace {

		void set_function( lua_State* state, const char* name, lua_CFunction function )
		{
			lua_pushcfunction( state, function );
			lua_setfield( state, -2, name );
		}

		zdraw::rgba read_color( lua_State* state, const int first )
		{
			const auto channel = [ & ]( const int index, const int fallback )
			{
				return static_cast<std::uint8_t>( std::clamp<lua_Integer>(
					luaL_optinteger( state, index, fallback ), 0, 255 ) );
			};
			return { channel( first, 255 ), channel( first + 1, 255 ),
				channel( first + 2, 255 ), channel( first + 3, 255 ) };
		}

		void push_control_value( lua_State* state, const control_value& value )
		{
			std::visit( [ & ]( const auto& item )
			{
				using type = std::decay_t<decltype( item )>;
				if constexpr ( std::same_as<type, bool> ) lua_pushboolean( state, item );
				else if constexpr ( std::same_as<type, double> ) lua_pushnumber( state, item );
				else if constexpr ( std::same_as<type, int> ) lua_pushinteger( state, item );
				else if constexpr ( std::same_as<type, zdraw::rgba> )
				{
					lua_createtable( state, 0, 4 );
					lua_pushinteger( state, item.r ); lua_setfield( state, -2, "r" );
					lua_pushinteger( state, item.g ); lua_setfield( state, -2, "g" );
					lua_pushinteger( state, item.b ); lua_setfield( state, -2, "b" );
					lua_pushinteger( state, item.a ); lua_setfield( state, -2, "a" );
				}
				else lua_pushlstring( state, item.data( ), item.size( ) );
			}, value );
		}

		zdraw::rgba read_color_table( lua_State* state, const int index,
			const zdraw::rgba fallback = { 255, 255, 255, 255 } )
		{
			if ( !lua_istable( state, index ) ) return fallback;
			const auto absolute = lua_absindex( state, index );
			auto channel = [ & ]( const char* name, const std::uint8_t initial )
			{
				lua_getfield( state, absolute, name );
				const auto result = static_cast<std::uint8_t>( std::clamp<lua_Integer>(
					luaL_optinteger( state, -1, initial ), 0, 255 ) );
				lua_pop( state, 1 );
				return result;
			};
			return { channel( "r", fallback.r ), channel( "g", fallback.g ),
				channel( "b", fallback.b ), channel( "a", fallback.a ) };
		}

		game::input_action parse_action( const std::string_view value )
		{
			if ( value == "forward" ) return game::input_action::forward;
			if ( value == "back" ) return game::input_action::back;
			if ( value == "left" ) return game::input_action::left;
			if ( value == "right" ) return game::input_action::right;
			if ( value == "walk" ) return game::input_action::walk;
			if ( value == "duck" ) return game::input_action::duck;
			if ( value == "jump" ) return game::input_action::jump;
			if ( value == "attack" ) return game::input_action::attack;
			if ( value == "attack2" ) return game::input_action::attack2;
			return game::input_action::count;
		}

	}

	void runtime_t::implementation::script_instance::register_api( )
	{
		lua_createtable( lua, 0, 12 );
		lua_createtable( lua, 0, 4 );
		lua_pushinteger( lua, 1 ); lua_setfield( lua, -2, "major" );
		lua_pushinteger( lua, 0 ); lua_setfield( lua, -2, "minor" );
		const auto script_directory = path_utf8( path.parent_path( ) );
		const auto data_directory = path_utf8( owner.data_root / std::filesystem::u8path( id ) );
		lua_pushlstring( lua, script_directory.data( ), script_directory.size( ) );
		lua_setfield( lua, -2, "script_dir" );
		lua_pushlstring( lua, data_directory.data( ), data_directory.size( ) );
		lua_setfield( lua, -2, "data_dir" );
		lua_setfield( lua, -2, "api" );
		set_function( lua, "log", api_log );

		lua_createtable( lua, 0, 2 );
		set_function( lua, "on", api_events_on );
		set_function( lua, "off", api_events_off );
		lua_setfield( lua, -2, "events" );
		lua_createtable( lua, 0, 8 );
		set_function( lua, "snapshot", api_game_snapshot );
		set_function( lua, "radar_snapshot", api_game_radar_snapshot );
		set_function( lua, "world_to_screen", api_world_to_screen );
		set_function( lua, "trace_ray", api_trace_ray );
		set_function( lua, "bomb_damage", api_bomb_damage );
		set_function( lua, "predict_grenade", api_predict_grenade );
		set_function( lua, "radar_overview", api_radar_overview );
		set_function( lua, "penetration_damage", api_penetration_damage );
		lua_setfield( lua, -2, "game" );

		lua_createtable( lua, 0, 5 );
		set_function( lua, "get", api_config_get ); set_function( lua, "set", api_config_set );
		set_function( lua, "patch", api_config_patch ); set_function( lua, "snapshot", api_config_snapshot );
		set_function( lua, "schema", api_config_schema );
		lua_setfield( lua, -2, "config" );

		lua_createtable( lua, 0, 3 );
		set_function( lua, "get", api_storage_get ); set_function( lua, "set", api_storage_set );
		set_function( lua, "remove", api_storage_remove ); lua_setfield( lua, -2, "storage" );

		lua_createtable( lua, 0, 2 );
		set_function( lua, "start", api_helpers_start );
		set_function( lua, "copy_text", api_helpers_copy_text );
		lua_setfield( lua, -2, "helpers" );

		lua_createtable( lua, 0, 1 );
		set_function( lua, "weapon_font", api_assets_weapon_font );
		lua_setfield( lua, -2, "assets" );

		lua_createtable( lua, 0, 12 );
		set_function( lua, "text", api_ui_text ); set_function( lua, "button", api_ui_button );
		set_function( lua, "toggle", api_ui_toggle ); set_function( lua, "slider", api_ui_slider );
		set_function( lua, "select", api_ui_select ); set_function( lua, "get", api_ui_get );
		set_function( lua, "input", api_ui_input ); set_function( lua, "color", api_ui_color );
		set_function( lua, "keybind", api_ui_keybind ); set_function( lua, "separator", api_ui_separator );
		set_function( lua, "consume", api_ui_consume ); lua_setfield( lua, -2, "ui" );

		lua_createtable( lua, 0, 15 );
		set_function( lua, "line", api_draw_line ); set_function( lua, "rect", api_draw_rect );
		set_function( lua, "filled_rect", api_draw_filled_rect ); set_function( lua, "circle", api_draw_circle );
		set_function( lua, "filled_circle", api_draw_filled_circle ); set_function( lua, "text", api_draw_text );
		set_function( lua, "world_line", api_draw_world_line ); set_function( lua, "world_text", api_draw_world_text );
		set_function( lua, "load_texture", api_draw_load_texture ); set_function( lua, "image", api_draw_image );
		set_function( lua, "panel", api_draw_panel );
		lua_setfield( lua, -2, "draw" );

		lua_createtable( lua, 0, 8 );
		set_function( lua, "is_down", api_input_is_down ); set_function( lua, "binding", api_input_binding );
		set_function( lua, "key", api_input_key ); set_function( lua, "tap", api_input_tap );
		set_function( lua, "mouse_move", api_input_mouse_move );
		set_function( lua, "mouse_button", api_input_mouse_button );
		lua_setfield( lua, -2, "input" );

		lua_setglobal( lua, "vesta" );
	}

	int runtime_t::implementation::script_instance::api_log( lua_State* state )
	{
		auto* script = self( state );
		const auto text = luaL_checkstring( state, 1 );
		app::context().diagnostics.info( "[lua:{}] {}", script->id, text );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_events_on( lua_State* state )
	{
		auto* script = self( state );
		const auto event = luaL_checkstring( state, 1 );
		const std::string_view event_name{ event };
		game::script_data_demand demand = game::script_data_demand::none;
		if ( event_name == "frame" )
			demand = game::script_data_demand::all;
		else if ( event_name.starts_with( "player_" )
			|| event_name == "weapon_changed" || event_name == "confirmed_hit" )
			demand = game::script_data_demand::players;
		else if ( event_name.starts_with( "projectile_" ) )
			demand = game::script_data_demand::projectiles;
		else if ( event_name.starts_with( "bomb_" ) )
			demand = game::script_data_demand::bomb;
		if ( demand != game::script_data_demand::none )
			script->data_demand.fetch_or( static_cast<std::uint32_t>( demand ),
				std::memory_order_release );
		if ( event_name == "config_changed" )
			script->config_demand.store( true, std::memory_order_release );
		static constexpr std::array<std::string_view, 18> frame_events{
			"frame", "game_connected", "game_disconnected", "map_changed",
			"player_joined", "player_left", "player_hurt", "player_killed",
			"weapon_changed", "projectile_created", "projectile_detonated",
			"projectile_removed", "bomb_planted", "bomb_defuse_started",
			"bomb_defuse_stopped", "bomb_defused", "bomb_resolved",
			"confirmed_hit" };
		if ( std::ranges::contains( frame_events, event_name ) )
			script->frame_events_requested = true;
		luaL_checktype( state, 2, LUA_TFUNCTION );
		lua_pushvalue( state, 2 );
		const auto reference = luaL_ref( state, LUA_REGISTRYINDEX );
		const auto token = script->next_callback_token++;
		script->callbacks[ event ].push_back( { token, reference } );
		lua_pushinteger( state, static_cast<lua_Integer>( token ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_events_off( lua_State* state )
	{
		auto* script = self( state );
		const std::string event = luaL_checkstring( state, 1 );
		const auto found = script->callbacks.find( event );
		if ( found == script->callbacks.end( ) ) { lua_pushboolean( state, false ); return 1; }
		const auto has_token = !lua_isnoneornil( state, 2 );
		const auto token = has_token ? static_cast<std::uint64_t>( luaL_checkinteger( state, 2 ) ) : 0;
		bool removed{};
		std::erase_if( found->second, [ & ]( const callback_entry& callback )
		{
			if ( has_token && callback.token != token ) return false;
			luaL_unref( state, LUA_REGISTRYINDEX, callback.reference );
			removed = true;
			return true;
		} );
		if ( found->second.empty( ) ) script->callbacks.erase( found );
		lua_pushboolean( state, removed );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_game_snapshot( lua_State* state )
	{
		auto* script = self( state );
		script->data_demand.fetch_or( static_cast<std::uint32_t>(
			game::script_data_demand::all ), std::memory_order_release );
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( !frame ) { lua_pushnil( state ); return 1; }
		push_frame( state, *frame );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_game_radar_snapshot( lua_State* state )
	{
		auto* script = self( state );
		constexpr auto radar_demand = game::script_data_demand::players
			| game::script_data_demand::items
			| game::script_data_demand::projectiles
			| game::script_data_demand::bomb;
		script->data_demand.fetch_or( static_cast<std::uint32_t>( radar_demand ),
			std::memory_order_release );
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( !frame ) { lua_pushnil( state ); return 1; }
		push_radar_frame( state, *frame );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_world_to_screen( lua_State* state )
	{
		const auto point = read_vec3( state, 1 );
		const auto screen = game::camera().project( point );
		if ( !game::camera().projection_valid( screen ) ) { lua_pushnil( state ); return 1; }
		push_vec2( state, screen );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_trace_ray( lua_State* state )
	{
		if ( !game::collision().valid( ) )
		{
			lua_pushnil( state );
			return 1;
		}
		const auto start = read_vec3( state, 1 );
		const auto end = read_vec3( state, 2 );
		const auto trace = game::collision().trace_ray( start, end );
		lua_createtable( state, 0, 7 );
		lua_pushboolean( state, trace.hit ); lua_setfield( state, -2, "hit" );
		lua_pushnumber( state, trace.fraction ); lua_setfield( state, -2, "fraction" );
		lua_pushnumber( state, trace.distance ); lua_setfield( state, -2, "distance" );
		push_vec3( state, trace.end_pos ); lua_setfield( state, -2, "position" );
		push_vec3( state, trace.normal ); lua_setfield( state, -2, "normal" );
		lua_pushnumber( state, trace.surface.penetration ); lua_setfield( state, -2, "penetration" );
		lua_pushinteger( state, trace.surface.surface_type ); lua_setfield( state, -2, "surface_type" );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_bomb_damage( lua_State* state )
	{
		const auto position = read_vec3( state, 1 );
		const auto site = static_cast<std::int32_t>( luaL_optinteger( state, 2,
			game::blast_damage().site_for_position( position ) ) );
		const auto angles = lua_istable( state, 3 ) ? read_vec3( state, 3 ) : foundation::vec3{};
		const auto ducked = lua_toboolean( state, 4 ) != 0;
		lua_pushinteger( state, game::blast_damage().predicted_damage(
			position, site, angles, ducked ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_predict_grenade( lua_State* state )
	{
		const auto origin = read_vec3( state, 1 );
		const auto velocity = read_vec3( state, 2 );
		std::uintptr_t weapon{};
		if ( lua_isinteger( state, 3 ) )
			weapon = static_cast<std::uintptr_t>( lua_tointeger( state, 3 ) );
		else
		{
			const std::string_view name = luaL_checkstring( state, 3 );
			if ( name == "he" || name == "he_grenade" || name == "weapon_hegrenade" ) weapon = "weapon_hegrenade"_id;
			else if ( name == "flash" || name == "flashbang" || name == "weapon_flashbang" ) weapon = "weapon_flashbang"_id;
			else if ( name == "smoke" || name == "smoke_grenade" || name == "weapon_smokegrenade" ) weapon = "weapon_smokegrenade"_id;
			else if ( name == "molotov" || name == "weapon_molotov" ) weapon = "weapon_molotov"_id;
			else if ( name == "incendiary" || name == "weapon_incgrenade" ) weapon = "weapon_incgrenade"_id;
			else if ( name == "decoy" || name == "weapon_decoy" ) weapon = "weapon_decoy"_id;
		}
		if ( !weapon ) { lua_pushnil( state ); lua_pushliteral( state, "unknown grenade type" ); return 2; }
		const auto lifetime = static_cast<float>( luaL_optnumber( state, 4, -1.0 ) );
		const simulation::grenade_trajectory_engine engine{};
		const auto path = engine.predict( origin, velocity, weapon, lifetime );
		lua_createtable( state, 0, 6 );
		lua_pushboolean( state, path.valid ); lua_setfield( state, -2, "valid" );
		lua_pushnumber( state, path.duration ); lua_setfield( state, -2, "duration" );
		lua_pushinteger( state, path.end_tick ); lua_setfield( state, -2, "end_tick" );
		push_vec3( state, path.end_pos ); lua_setfield( state, -2, "end_position" );
		lua_createtable( state, static_cast<int>( path.points.size( ) ), 0 );
		for ( std::size_t i = 0; i < path.points.size( ); ++i )
		{
			push_vec3( state, path.points[ i ] );
			lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
		}
		lua_setfield( state, -2, "points" );
		lua_createtable( state, static_cast<int>( path.bounces.size( ) ), 0 );
		for ( std::size_t i = 0; i < path.bounces.size( ); ++i )
		{
			push_vec3( state, path.bounces[ i ] );
			lua_rawseti( state, -2, static_cast<lua_Integer>( i + 1 ) );
		}
		lua_setfield( state, -2, "bounces" );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_radar_overview( lua_State* state )
	{
		auto* script = self( state );
		const auto map_name = sanitize_id( luaL_checkstring( state, 1 ) );
		const auto output_stem = sanitize_id( luaL_optstring( state, 2, "overview" ) );
		const auto directory = script->owner.data_root / std::filesystem::path(
			wide_utf8( script->id ) );
		const auto value = radar_asset::export_overview(
			map_name, directory, output_stem );
		if ( !value.valid )
		{
			lua_pushnil( state );
			lua_pushstring( state, value.error.empty( )
				? "CS2 radar overview is unavailable" : value.error.c_str( ) );
			return 2;
		}
		lua_createtable( state, 0, 10 );
		lua_pushnumber( state, value.pos_x ); lua_setfield( state, -2, "pos_x" );
		lua_pushnumber( state, value.pos_y ); lua_setfield( state, -2, "pos_y" );
		lua_pushnumber( state, value.scale ); lua_setfield( state, -2, "scale" );
		lua_pushinteger( state, value.width ); lua_setfield( state, -2, "width" );
		lua_pushinteger( state, value.height ); lua_setfield( state, -2, "height" );
		lua_pushboolean( state, value.has_lower ); lua_setfield( state, -2, "has_lower" );
		lua_pushnumber( state, value.lower_altitude_max );
		lua_setfield( state, -2, "lower_altitude_max" );
		lua_pushstring( state, path_utf8( value.primary_path ).c_str( ) );
		lua_setfield( state, -2, "primary_path" );
		if ( value.has_lower )
		{
			lua_pushstring( state, path_utf8( value.lower_path ).c_str( ) );
			lua_setfield( state, -2, "lower_path" );
		}
		return 1;
	}

	int runtime_t::implementation::script_instance::api_penetration_damage( lua_State* state )
	{
		const auto start = read_vec3( state, 1 );
		const auto finish = read_vec3( state, 2 );
		const auto context = simulation::ballistics().ctx( );
		if ( !context.valid || !context.weapon || !context.weapon_vdata )
			{ lua_pushnil( state ); return 1; }
		simulation::ballistics_t::penetration query{};
		query.prepare( context.weapon_vdata, context.weapon );
		const auto target_handle = static_cast<std::uint32_t>( luaL_optinteger( state, 3, 0 ) );
		if ( target_handle )
		{
			const auto frame = self( state )->owner.current_frame.load( std::memory_order_acquire );
			if ( !frame || !frame->players ) { lua_pushnil( state ); return 1; }
			const auto target = std::ranges::find( *frame->players, target_handle,
				&game::player_snapshot::pawn_handle );
			if ( target == frame->players->end( ) ) { lua_pushnil( state ); return 1; }
			simulation::ballistics_t::penetration::result result{};
			if ( !query.run( start, finish, *target, target->bones, result ) )
				{ lua_pushnil( state ); return 1; }
			lua_createtable( state, 0, 4 );
			lua_pushnumber( state, result.damage ); lua_setfield( state, -2, "damage" );
			lua_pushnumber( state, result.distance ); lua_setfield( state, -2, "distance" );
			lua_pushinteger( state, result.hitbox ); lua_setfield( state, -2, "hitbox" );
			lua_pushboolean( state, result.penetrated ); lua_setfield( state, -2, "penetrated" );
			return 1;
		}
		const auto delta = finish - start;
		if ( delta.length_sqr( ) <= 0.0001f ) { lua_pushnil( state ); return 1; }
		float damage{};
		if ( !query.can( start, delta.normalized( ), damage ) ) { lua_pushnil( state ); return 1; }
		lua_createtable( state, 0, 2 );
		lua_pushnumber( state, damage ); lua_setfield( state, -2, "damage" );
		lua_pushboolean( state, true ); lua_setfield( state, -2, "penetrated" );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_config_get( lua_State* state )
	{
		auto* script = self( state );
		script->config_demand.store( true, std::memory_order_release );
		const auto path = luaL_checkstring( state, 1 );
		const auto snapshot = script->owner.config_snapshot.load( std::memory_order_acquire );
		if ( !snapshot ) { lua_pushnil( state ); return 1; }
		const auto* value = get_json_path( *snapshot, path );
		if ( !value ) lua_pushnil( state ); else push_json( state, *value );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_config_set( lua_State* state )
	{
		auto* script = self( state );
		script->config_demand.store( true, std::memory_order_release );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		const auto path = luaL_checkstring( state, 1 );
		json value{};
		if ( !lua_to_json( state, 2, value ) ) { lua_pushboolean( state, false ); return 1; }
		script->owner.queue_config( path, std::move( value ) );
		lua_pushboolean( state, true );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_config_patch( lua_State* state )
	{
		auto* script = self( state );
		script->config_demand.store( true, std::memory_order_release );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		luaL_checktype( state, 1, LUA_TTABLE );
		const auto table = lua_absindex( state, 1 );
		lua_pushnil( state );
		while ( lua_next( state, table ) != 0 )
		{
			if ( !lua_isstring( state, -2 ) ) { lua_pop( state, 2 ); lua_pushboolean( state, false ); return 1; }
			json value{};
			if ( !lua_to_json( state, -1, value ) ) { lua_pop( state, 2 ); lua_pushboolean( state, false ); return 1; }
			script->owner.queue_config( lua_tostring( state, -2 ), std::move( value ) );
			lua_pop( state, 1 );
		}
		lua_pushboolean( state, true );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_config_snapshot( lua_State* state )
	{
		auto* script = self( state );
		script->config_demand.store( true, std::memory_order_release );
		const auto snapshot = script->owner.config_snapshot.load( std::memory_order_acquire );
		if ( !snapshot ) lua_pushnil( state ); else push_json( state, *snapshot );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_config_schema( lua_State* state )
	{
		auto* script = self( state );
		script->config_demand.store( true, std::memory_order_release );
		const auto snapshot = script->owner.config_snapshot.load( std::memory_order_acquire );
		if ( !snapshot ) lua_pushnil( state ); else push_json( state, describe_json( *snapshot ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_storage_get( lua_State* state )
	{
		const auto* script = self( state );
		const auto key = luaL_checkstring( state, 1 );
		const auto found = script->storage.find( key );
		if ( found == script->storage.end( ) ) lua_pushnil( state ); else push_json( state, *found );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_storage_set( lua_State* state )
	{
		auto* script = self( state );
		const auto key = luaL_checkstring( state, 1 );
		json value{};
		if ( !lua_to_json( state, 2, value ) ) { lua_pushboolean( state, false ); return 1; }
		script->storage[ key ] = std::move( value );
		script->save_storage( );
		lua_pushboolean( state, true );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_storage_remove( lua_State* state )
	{
		auto* script = self( state );
		const auto erased = script->storage.erase( luaL_checkstring( state, 1 ) ) > 0;
		if ( erased ) script->save_storage( );
		lua_pushboolean( state, erased );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_helpers_start( lua_State* state )
	{
		auto* script = self( state );
		if ( script->provisional )
			{ lua_pushboolean( state, false ); lua_pushliteral( state, "helper launch is unavailable during validation" ); return 2; }
		const auto relative = std::filesystem::u8path( luaL_checkstring( state, 1 ) );
		if ( relative.empty( ) || relative.is_absolute( ) )
			{ lua_pushboolean( state, false ); lua_pushliteral( state, "helper path must be relative" ); return 2; }
		std::error_code error{};
		const auto root = std::filesystem::weakly_canonical( script->path.parent_path( ), error );
		const auto target = std::filesystem::weakly_canonical( root / relative, error );
		if ( error || !std::filesystem::is_regular_file( target, error )
			|| _wcsicmp( target.extension().c_str( ), L".ps1" ) != 0 )
			{ lua_pushboolean( state, false ); lua_pushliteral( state, "PowerShell helper not found" ); return 2; }
		auto root_it = root.begin( ); auto target_it = target.begin( );
		for ( ; root_it != root.end( ) && target_it != target.end( ); ++root_it, ++target_it )
			if ( _wcsicmp( root_it->c_str( ), target_it->c_str( ) ) != 0 ) break;
		if ( root_it != root.end( ) )
			{ lua_pushboolean( state, false ); lua_pushliteral( state, "helper path escapes script directory" ); return 2; }

		std::wstring command{ L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File " };
		command += quote_windows_argument( target.wstring( ) );
		if ( !lua_isnoneornil( state, 2 ) )
		{
			luaL_checktype( state, 2, LUA_TTABLE );
			const auto count = std::min<std::size_t>( lua_rawlen( state, 2 ), 32 );
			for ( std::size_t index = 1; index <= count; ++index )
			{
				lua_rawgeti( state, 2, static_cast<lua_Integer>( index ) );
				size_t length{}; const auto* argument = luaL_checklstring( state, -1, &length );
				if ( length > 2048 ) { lua_pop( state, 1 ); return luaL_error( state, "helper argument is too long" ); }
				const auto wide = wide_utf8( { argument, length } );
				lua_pop( state, 1 );
				command.push_back( L' ' ); command += quote_windows_argument( wide );
			}
		}
		std::vector<wchar_t> mutable_command( command.begin( ), command.end( ) );
		mutable_command.push_back( L'\0' );
		STARTUPINFOW startup{ sizeof( startup ) }; PROCESS_INFORMATION process{};
		const auto created = ::CreateProcessW( nullptr, mutable_command.data( ), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, root.c_str( ), &startup, &process );
		if ( !created )
		{
			lua_pushboolean( state, false );
			lua_pushfstring( state, "CreateProcess failed (%d)", static_cast<int>( ::GetLastError( ) ) );
			return 2;
		}
		::CloseHandle( process.hThread ); ::CloseHandle( process.hProcess );
		lua_pushboolean( state, true ); return 1;
	}

	int runtime_t::implementation::script_instance::api_helpers_copy_text( lua_State* state )
	{
		size_t length{}; const auto* text = luaL_checklstring( state, 1, &length );
		const auto wide = wide_utf8( { text, length } );
		if ( length && wide.empty( ) ) { lua_pushboolean( state, false ); return 1; }
		if ( !::OpenClipboard( nullptr ) ) { lua_pushboolean( state, false ); return 1; }
		::EmptyClipboard( );
		const auto bytes = ( wide.size( ) + 1 ) * sizeof( wchar_t );
		const auto memory = ::GlobalAlloc( GMEM_MOVEABLE, bytes );
		if ( !memory ) { ::CloseClipboard( ); lua_pushboolean( state, false ); return 1; }
		if ( const auto destination = ::GlobalLock( memory ) )
		{
			std::memcpy( destination, wide.c_str( ), bytes ); ::GlobalUnlock( memory );
		}
		else { ::GlobalFree( memory ); ::CloseClipboard( ); lua_pushboolean( state, false ); return 1; }
		const auto accepted = ::SetClipboardData( CF_UNICODETEXT, memory ) != nullptr;
		if ( !accepted ) ::GlobalFree( memory );
		::CloseClipboard( ); lua_pushboolean( state, accepted ); return 1;
	}

	int runtime_t::implementation::script_instance::api_assets_weapon_font( lua_State* state )
	{
		lua_pushlstring( state,
			reinterpret_cast<const char*>( resources::fonts::weapons ),
			sizeof( resources::fonts::weapons ) );
		return 1;
	}

	namespace {

		runtime_t::implementation::script_instance* add_or_find_control(
			lua_State* state, const control_kind kind, const control_value& initial,
			double minimum = 0.0, double maximum = 1.0, double step = 0.01,
			std::vector<std::string> options = {} )
		{
			auto* script = runtime_t::implementation::script_instance::self( state );
			const std::string id = luaL_checkstring( state, 1 );
			const std::string label = luaL_optstring( state, 2, id.c_str( ) );
			std::scoped_lock lock( script->controls_mutex );
			const auto found = std::ranges::find( script->controls, id, &ui_control::id );
			if ( found == script->controls.end( ) )
			{
				if ( script->controls.size( ) >= k_ui_limit ) luaL_error( state, "UI control limit exceeded" );
				script->controls.push_back( { id, label, kind, initial, {},
					minimum, maximum, step, std::move( options ), 0 } );
			}
			return script;
		}

		runtime_t::implementation::draw_command& add_draw(
			lua_State* state, const runtime_t::implementation::draw_kind kind )
		{
			auto* script = runtime_t::implementation::script_instance::self( state );
			if ( script->building_draw.size( ) >= k_draw_limit ) luaL_error( state, "draw command limit exceeded" );
			script->building_draw.push_back( {} );
			auto& result = script->building_draw.back( );
			result.kind = kind;
			return result;
		}

	}

	int runtime_t::implementation::script_instance::api_ui_text( lua_State* state )
	{
		auto* script = add_or_find_control( state, control_kind::text,
			std::string( luaL_optstring( state, 3, "" ) ) );
		const std::string id = luaL_checkstring( state, 1 );
		const std::string value = luaL_optstring( state, 3, "" );
		std::scoped_lock lock( script->controls_mutex );
		const auto found = std::ranges::find( script->controls, id, &ui_control::id );
		if ( found != script->controls.end( ) ) found->value = value;
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_button( lua_State* state )
	{
		auto* script = add_or_find_control( state, control_kind::button, false );
		const std::string id = luaL_checkstring( state, 1 );
		const std::string action = luaL_optstring( state, 3, "Run" );
		std::scoped_lock lock( script->controls_mutex );
		const auto found = std::ranges::find( script->controls, id, &ui_control::id );
		if ( found != script->controls.end( ) ) found->action_text = action;
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_toggle( lua_State* state )
	{
		add_or_find_control( state, control_kind::toggle, lua_toboolean( state, 3 ) != 0 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_slider( lua_State* state )
	{
		const auto initial = luaL_optnumber( state, 3, 0.0 );
		const auto minimum = luaL_optnumber( state, 4, 0.0 );
		const auto maximum = luaL_optnumber( state, 5, 1.0 );
		const auto step = luaL_optnumber( state, 6, 0.01 );
		add_or_find_control( state, control_kind::slider, initial, minimum, maximum, step );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_select( lua_State* state )
	{
		luaL_checktype( state, 3, LUA_TTABLE );
		std::vector<std::string> options{};
		const auto count = lua_rawlen( state, 3 );
		options.reserve( count );
		for ( std::size_t i = 1; i <= count; ++i )
		{
			lua_rawgeti( state, 3, static_cast<lua_Integer>( i ) );
			options.emplace_back( luaL_checkstring( state, -1 ) );
			lua_pop( state, 1 );
		}
		add_or_find_control( state, control_kind::select,
			static_cast<int>( luaL_optinteger( state, 4, 0 ) ), 0.0,
			std::max<double>( 0.0, static_cast<double>( options.size( ) - 1 ) ), 1.0,
			std::move( options ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_input( lua_State* state )
	{
		add_or_find_control( state, control_kind::input,
			std::string( luaL_optstring( state, 3, "" ) ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_color( lua_State* state )
	{
		add_or_find_control( state, control_kind::color,
			read_color_table( state, 3 ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_keybind( lua_State* state )
	{
		add_or_find_control( state, control_kind::keybind,
			static_cast<int>( std::clamp<lua_Integer>( luaL_optinteger( state, 3, 0 ), 0, 255 ) ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_separator( lua_State* state )
	{
		add_or_find_control( state, control_kind::separator, std::string{} );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_ui_get( lua_State* state )
	{
		auto* script = self( state );
		const std::string id = luaL_checkstring( state, 1 );
		std::scoped_lock lock( script->controls_mutex );
		const auto found = std::ranges::find( script->controls, id, &ui_control::id );
		if ( found == script->controls.end( ) ) lua_pushnil( state );
		else push_control_value( state, found->value );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_ui_consume( lua_State* state )
	{
		auto* script = self( state );
		const std::string id = luaL_checkstring( state, 1 );
		std::scoped_lock lock( script->controls_mutex );
		auto& presses = script->button_presses[ id ];
		const auto result = presses > 0;
		if ( result ) --presses;
		lua_pushboolean( state, result );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_draw_line( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::line );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.b = { static_cast<float>( luaL_checknumber( state, 3 ) ), static_cast<float>( luaL_checknumber( state, 4 ) ), 0.0f };
		command.color = read_color( state, 5 );
		command.thickness = static_cast<float>( luaL_optnumber( state, 9, 1.0 ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_rect( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::rect );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.width = static_cast<float>( luaL_checknumber( state, 3 ) ); command.height = static_cast<float>( luaL_checknumber( state, 4 ) );
		command.color = read_color( state, 5 ); command.thickness = static_cast<float>( luaL_optnumber( state, 9, 1.0 ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_filled_rect( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::filled_rect );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.width = static_cast<float>( luaL_checknumber( state, 3 ) ); command.height = static_cast<float>( luaL_checknumber( state, 4 ) );
		command.color = read_color( state, 5 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_circle( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::circle );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.radius = static_cast<float>( luaL_checknumber( state, 3 ) ); command.color = read_color( state, 4 );
		command.thickness = static_cast<float>( luaL_optnumber( state, 8, 1.0 ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_filled_circle( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::filled_circle );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.radius = static_cast<float>( luaL_checknumber( state, 3 ) ); command.color = read_color( state, 4 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_text( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::text );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ), static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		size_t length{}; const auto* text = luaL_checklstring( state, 3, &length ); command.text.assign( text, length );
		command.color = read_color( state, 4 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_world_line( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::world_line );
		command.a = read_vec3( state, 1 ); command.b = read_vec3( state, 2 );
		command.color = read_color( state, 3 ); command.thickness = static_cast<float>( luaL_optnumber( state, 7, 1.0 ) );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_world_text( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::world_text );
		command.a = read_vec3( state, 1 );
		size_t length{}; const auto* text = luaL_checklstring( state, 2, &length ); command.text.assign( text, length );
		command.color = read_color( state, 3 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_load_texture( lua_State* state )
	{
		auto* script = self( state );
		const auto relative = std::filesystem::u8path( luaL_checkstring( state, 1 ) );
		if ( relative.empty( ) || relative.is_absolute( ) )
			{ lua_pushnil( state ); lua_pushliteral( state, "texture path must be relative" ); return 2; }
		std::error_code error{};
		const auto root = std::filesystem::weakly_canonical( script->path.parent_path( ), error );
		const auto target = std::filesystem::weakly_canonical( root / relative, error );
		if ( error || !std::filesystem::is_regular_file( target, error ) )
			{ lua_pushnil( state ); lua_pushliteral( state, "texture file not found" ); return 2; }
		auto root_it = root.begin( );
		auto target_it = target.begin( );
		for ( ; root_it != root.end( ) && target_it != target.end( ); ++root_it, ++target_it )
			if ( _wcsicmp( root_it->c_str( ), target_it->c_str( ) ) != 0 ) break;
		if ( root_it != root.end( ) )
			{ lua_pushnil( state ); lua_pushliteral( state, "texture path escapes script directory" ); return 2; }
		auto extension = target.extension().wstring( );
		std::ranges::transform( extension, extension.begin( ), []( const wchar_t c ) { return std::towlower( c ); } );
		if ( extension != L".png" && extension != L".jpg" && extension != L".jpeg" && extension != L".bmp" )
			{ lua_pushnil( state ); lua_pushliteral( state, "unsupported texture format" ); return 2; }
		const auto bytes = std::filesystem::file_size( target, error );
		if ( error || bytes > 16u * 1024u * 1024u )
			{ lua_pushnil( state ); lua_pushliteral( state, "texture exceeds 16 MiB" ); return 2; }
		const auto path_text = path_utf8( target );
		const auto key = script->id + ":" + std::to_string( std::hash<std::string>{}( path_text ) );
		{
			std::scoped_lock lock( script->owner.textures_mutex );
			if ( script->owner.textures.size( ) >= 64 && !script->owner.textures.contains( key ) )
				{ lua_pushnil( state ); lua_pushliteral( state, "texture limit exceeded" ); return 2; }
			script->owner.textures.try_emplace( key,
				runtime_t::implementation::texture_entry{ target } );
		}
		lua_pushlstring( state, key.data( ), key.size( ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_draw_image( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::image );
		command.texture = luaL_checkstring( state, 1 );
		command.a = { static_cast<float>( luaL_checknumber( state, 2 ) ),
			static_cast<float>( luaL_checknumber( state, 3 ) ), 0.0f };
		command.width = static_cast<float>( luaL_checknumber( state, 4 ) );
		command.height = static_cast<float>( luaL_checknumber( state, 5 ) );
		command.color = read_color( state, 6 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_draw_panel( lua_State* state )
	{
		auto& command = add_draw( state, draw_kind::panel );
		command.a = { static_cast<float>( luaL_checknumber( state, 1 ) ),
			static_cast<float>( luaL_checknumber( state, 2 ) ), 0.0f };
		command.width = std::max( 80.0f, static_cast<float>( luaL_checknumber( state, 3 ) ) );
		command.height = std::max( 36.0f, static_cast<float>( luaL_checknumber( state, 4 ) ) );
		size_t length{};
		const auto* title = luaL_checklstring( state, 5, &length );
		command.text.assign( title, length );
		command.color = read_color( state, 6 );
		return 0;
	}

	int runtime_t::implementation::script_instance::api_input_is_down( lua_State* state )
	{
		const auto key = static_cast<int>( luaL_checkinteger( state, 1 ) );
		lua_pushboolean( state, key > 0 && key < 256
			&& ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0 );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_input_binding( lua_State* state )
	{
		const auto action_text = std::string_view( luaL_checkstring( state, 1 ) );
		const auto action = parse_action( action_text );
		if ( action == game::input_action::count ) { lua_pushnil( state ); return 1; }
		const auto binding = game::input_bindings().resolve( action );
		if ( !binding ) { lua_pushnil( state ); return 1; }
		lua_createtable( state, 0, 3 );
		lua_pushinteger( state, binding.virtual_key ); lua_setfield( state, -2, "virtual_key" );
		lua_pushinteger( state, static_cast<int>( binding.device ) ); lua_setfield( state, -2, "device" );
		lua_pushlstring( state, binding.name.data( ), binding.name.size( ) ); lua_setfield( state, -2, "name" );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_input_key( lua_State* state )
	{
		auto* script = self( state );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		const auto key = static_cast<std::uint16_t>( std::clamp<lua_Integer>(
			luaL_checkinteger( state, 1 ), 1, 255 ) );
		const auto pressed = lua_toboolean( state, 2 ) != 0;
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( pressed && ( !frame || frame->menu_open || !frame->input_ready ) )
		{
			lua_pushboolean( state, false ); return 1;
		}
		const auto result = app::context().input.key( key, pressed );
		if ( result )
		{
			if ( pressed ) script->held_keys.insert( key ); else script->held_keys.erase( key );
		}
		lua_pushboolean( state, result );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_input_tap( lua_State* state )
	{
		auto* script = self( state );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		const auto key = static_cast<std::uint16_t>( std::clamp<lua_Integer>(
			luaL_checkinteger( state, 1 ), 1, 255 ) );
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( !frame || frame->menu_open || !frame->input_ready ) { lua_pushboolean( state, false ); return 1; }
		const platform::windows::input_gateway::key_transition transitions[]{ { key, true }, { key, false } };
		lua_pushboolean( state, app::context().input.keys( transitions ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_input_mouse_move( lua_State* state )
	{
		auto* script = self( state );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( !frame || frame->menu_open || !frame->input_ready ) { lua_pushboolean( state, false ); return 1; }
		const auto dx = static_cast<int>( luaL_checkinteger( state, 1 ) );
		const auto dy = static_cast<int>( luaL_checkinteger( state, 2 ) );
		lua_pushboolean( state, app::context().input.pointer(
			dx, dy, platform::windows::pointer_action::relative_move ) );
		return 1;
	}

	int runtime_t::implementation::script_instance::api_input_mouse_button( lua_State* state )
	{
		auto* script = self( state );
		if ( script->provisional ) { lua_pushboolean( state, false ); return 1; }
		const auto button = std::string_view( luaL_checkstring( state, 1 ) );
		const auto pressed = lua_toboolean( state, 2 ) != 0;
		const auto frame = script->owner.current_frame.load( std::memory_order_acquire );
		if ( pressed && ( !frame || frame->menu_open || !frame->input_ready ) )
		{
			lua_pushboolean( state, false ); return 1;
		}
		auto action = platform::windows::pointer_action::none;
		if ( button == "primary" ) action = pressed
			? platform::windows::pointer_action::primary_down
			: platform::windows::pointer_action::primary_up;
		else if ( button == "secondary" ) action = pressed
			? platform::windows::pointer_action::secondary_down
			: platform::windows::pointer_action::secondary_up;
		else { lua_pushboolean( state, false ); return 1; }
		const auto result = app::context().input.pointer( 0, 0, action );
		if ( result )
		{
			if ( button == "primary" ) script->primary_down = pressed;
			else script->secondary_down = pressed;
		}
		lua_pushboolean( state, result );
		return 1;
	}

	void runtime_t::implementation::load_runtime_state( )
	{
		try
		{
			std::ifstream stream( state_path( ) );
			if ( !stream ) return;
			json document{};
			stream >> document;
			std::scoped_lock lock( scripts_mutex );
			for ( const auto& instance : instances )
			{
				const auto found = document.find( instance->id );
				if ( found == document.end( ) || !found->is_object( ) ) continue;
				instance->autoload.store( found->value( "autoload", false ) );
				instance->hot_reload.store( found->value( "hot_reload", true ) );
			}
		}
		catch ( ... ) {}
	}

	void runtime_t::implementation::save_runtime_state( ) const
	{
		try
		{
			json document = json::object( );
			{
				std::scoped_lock lock( scripts_mutex );
				for ( const auto& instance : instances )
					document[ instance->id ] = {
						{ "autoload", instance->autoload.load( ) },
						{ "hot_reload", instance->hot_reload.load( ) } };
			}
			auto pending = state_path( ); pending += L".tmp";
			{
				std::ofstream stream( pending, std::ios::trunc );
				if ( !stream ) return;
				stream << document.dump( 2 );
			}
			::MoveFileExW( pending.c_str( ), state_path( ).c_str( ),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH );
		}
		catch ( ... ) {}
	}

	namespace {

		void materialize_texture( runtime_t::implementation::texture_entry& entry,
			ID3D11Device* device )
		{
			if ( entry.attempted || !device ) return;
			entry.attempted = true;
			IWICImagingFactory* factory{};
			IWICBitmapDecoder* decoder{};
			IWICBitmapFrameDecode* frame{};
			IWICFormatConverter* converter{};
			ID3D11Texture2D* texture{};
			const auto cleanup = [ & ]
			{
				if ( texture ) texture->Release( );
				if ( converter ) converter->Release( );
				if ( frame ) frame->Release( );
				if ( decoder ) decoder->Release( );
				if ( factory ) factory->Release( );
			};
			if ( FAILED( ::CoCreateInstance( CLSID_WICImagingFactory2, nullptr,
				CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) ) ) { cleanup( ); return; }
			if ( FAILED( factory->CreateDecoderFromFilename( entry.path.c_str( ), nullptr,
				GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder ) )
				|| FAILED( decoder->GetFrame( 0, &frame ) )
				|| FAILED( factory->CreateFormatConverter( &converter ) )
				|| FAILED( converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA,
					WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) )
				{ cleanup( ); return; }
			UINT width{}, height{};
			if ( FAILED( converter->GetSize( &width, &height ) ) || !width || !height
				|| width > 4096 || height > 4096 ) { cleanup( ); return; }
			const auto pitch = width * 4u;
			std::vector<std::uint8_t> pixels( static_cast<std::size_t>( pitch ) * height );
			if ( FAILED( converter->CopyPixels( nullptr, pitch,
				static_cast<UINT>( pixels.size( ) ), pixels.data( ) ) ) ) { cleanup( ); return; }
			D3D11_TEXTURE2D_DESC description{};
			description.Width = width; description.Height = height;
			description.MipLevels = 1; description.ArraySize = 1;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			const D3D11_SUBRESOURCE_DATA initial{ pixels.data( ), pitch, 0 };
			if ( FAILED( device->CreateTexture2D( &description, &initial, &texture ) )
				|| FAILED( device->CreateShaderResourceView( texture, nullptr, &entry.view ) ) )
				{ cleanup( ); return; }
			entry.width = width; entry.height = height;
			cleanup( );
		}

	}

	runtime_t::runtime_t( ) : m_impl( std::make_unique<implementation>( ) ) {}
	runtime_t::~runtime_t( ) { shutdown( ); }

	bool runtime_t::initialize( )
	{
		if ( m_impl->initialized.exchange( true ) ) return true;
		m_impl->root = platform::windows::runtime_storage::area( "lua" );
		if ( m_impl->root.empty( ) ) { m_impl->initialized.store( false ); return false; }
		m_impl->scripts_root = m_impl->root / L"scripts";
		m_impl->modules_root = m_impl->root / L"modules";
		m_impl->data_root = m_impl->root / L"data";
		m_impl->logs_root = m_impl->root / L"logs";
		std::error_code error{};
		for ( const auto& path : { m_impl->scripts_root, m_impl->modules_root,
			m_impl->data_root, m_impl->logs_root } )
		{
			std::filesystem::create_directories( path, error );
			if ( error ) { m_impl->initialized.store( false ); return false; }
		}
		m_impl->config_snapshot.store(
			std::make_shared<const json>( config::build_config_json( ) ),
			std::memory_order_release );
		refresh( );
		m_impl->load_runtime_state( );
		if ( config::general_settings.lua_enabled )
		{
			std::vector<std::shared_ptr<implementation::script_instance>> scripts{};
			{
				std::scoped_lock lock( m_impl->scripts_mutex ); scripts = m_impl->instances;
			}
			for ( const auto& script : scripts ) if ( script->autoload.load( ) ) script->start( );
		}
		return true;
	}

	void runtime_t::shutdown( ) noexcept
	{
		if ( !m_impl || !m_impl->initialized.exchange( false ) ) return;
		std::vector<std::shared_ptr<implementation::script_instance>> scripts{};
		{
			std::scoped_lock lock( m_impl->scripts_mutex ); scripts = m_impl->instances;
		}
		for ( const auto& script : scripts ) script->stop( );
		game::world().set_script_demand( game::script_data_demand::none );
		m_impl->save_runtime_state( );
		{
			std::scoped_lock lock( m_impl->textures_mutex );
			for ( auto& [ _, texture ] : m_impl->textures )
				if ( texture.view ) texture.view->Release( );
			m_impl->textures.clear( );
		}
	}

	void runtime_t::refresh( )
	{
		if ( !m_impl->initialized.load( ) ) return;
		struct descriptor
		{
			std::string id, name, version, author, description;
			int api_version{ 1 };
			std::filesystem::path entry;
		};
		std::vector<descriptor> found{};
		std::error_code error{};
		for ( const auto& item : std::filesystem::directory_iterator(
			m_impl->scripts_root, std::filesystem::directory_options::skip_permission_denied, error ) )
		{
			if ( item.is_regular_file( ) && item.path().extension( ) == L".lua" )
			{
				const auto stem = path_utf8( item.path().stem( ) );
				found.push_back( { sanitize_id( stem ), stem, "1.0", {}, {}, 1, item.path( ) } );
			}
			else if ( item.is_directory( ) )
			{
				try
				{
					const auto manifest_path = item.path( ) / L"manifest.json";
					if ( !std::filesystem::exists( manifest_path ) ) continue;
					std::ifstream stream( manifest_path );
					json manifest{}; stream >> manifest;
					const auto folder = path_utf8( item.path().filename( ) );
					const auto id = sanitize_id( manifest.value( "id", folder ) );
					const auto entry = item.path( ) / std::filesystem::u8path(
						manifest.value( "entry", std::string( "main.lua" ) ) );
					if ( !std::filesystem::is_regular_file( entry ) ) continue;
					found.push_back( { id, manifest.value( "name", id ),
						manifest.value( "version", std::string( "1.0" ) ),
						manifest.value( "author", std::string{} ),
						manifest.value( "description", std::string{} ),
						manifest.value( "api_version", 1 ), entry } );
				}
				catch ( ... ) {}
			}
		}

		std::vector<std::shared_ptr<implementation::script_instance>> removed{};
		{
			std::scoped_lock lock( m_impl->scripts_mutex );
			for ( const auto& descriptor : found )
			{
				const auto existing = std::ranges::find( m_impl->instances,
					descriptor.id, &implementation::script_instance::id );
				if ( existing != m_impl->instances.end( ) ) continue;
				auto script = std::make_shared<implementation::script_instance>( *m_impl );
				script->id = descriptor.id; script->name = descriptor.name;
				script->version = descriptor.version; script->author = descriptor.author;
				script->description = descriptor.description; script->api_major = descriptor.api_version;
				script->path = descriptor.entry;
				m_impl->instances.push_back( std::move( script ) );
			}
			std::erase_if( m_impl->instances, [ & ]( const auto& instance )
			{
				const auto present = std::ranges::any_of( found,
					[ & ]( const descriptor& value ) { return value.id == instance->id; } );
				if ( !present ) removed.push_back( instance );
				return !present;
			} );
			std::ranges::sort( m_impl->instances, {}, &implementation::script_instance::name );
		}
		for ( const auto& script : removed ) script->stop( );
	}

	std::vector<script_info> runtime_t::scripts( ) const
	{
		std::vector<script_info> result{};
		std::scoped_lock lock( m_impl->scripts_mutex );
		result.reserve( m_impl->instances.size( ) );
		for ( const auto& script : m_impl->instances ) result.push_back( script->info( ) );
		return result;
	}

	std::vector<ui_control> runtime_t::controls( const std::string_view id ) const
	{
		std::scoped_lock lock( m_impl->scripts_mutex );
		const auto found = std::ranges::find( m_impl->instances, id,
			&implementation::script_instance::id );
		return found == m_impl->instances.end( ) ? std::vector<ui_control>{}
			: ( *found )->controls_snapshot( );
	}

	void runtime_t::set_control( const std::string_view script_id,
		const std::string_view control_id, const control_value& value )
	{
		std::shared_ptr<implementation::script_instance> script{};
		{
			std::scoped_lock lock( m_impl->scripts_mutex );
			const auto found = std::ranges::find( m_impl->instances, script_id,
				&implementation::script_instance::id );
			if ( found != m_impl->instances.end( ) ) script = *found;
		}
		if ( !script ) return;
		std::scoped_lock lock( script->controls_mutex );
		const auto found = std::ranges::find( script->controls, control_id, &ui_control::id );
		if ( found != script->controls.end( ) ) { found->value = value; ++found->revision; }
	}

	void runtime_t::press_control( const std::string_view script_id,
		const std::string_view control_id )
	{
		std::shared_ptr<implementation::script_instance> script{};
		{
			std::scoped_lock lock( m_impl->scripts_mutex );
			const auto found = std::ranges::find( m_impl->instances, script_id,
				&implementation::script_instance::id );
			if ( found != m_impl->instances.end( ) ) script = *found;
		}
		if ( script ) { std::scoped_lock lock( script->controls_mutex ); ++script->button_presses[ std::string( control_id ) ]; }
	}

	namespace {

		std::shared_ptr<runtime_t::implementation::script_instance> find_script(
			runtime_t::implementation& runtime, const std::string_view id )
		{
			std::scoped_lock lock( runtime.scripts_mutex );
			const auto found = std::ranges::find( runtime.instances, id,
				&runtime_t::implementation::script_instance::id );
			return found == runtime.instances.end( ) ? nullptr : *found;
		}

	}

	void runtime_t::set_enabled( const std::string_view id, const bool enabled )
	{
		const auto script = find_script( *m_impl, id );
		if ( !script ) return;
		if ( enabled && config::general_settings.lua_enabled ) script->start( );
		else script->request_stop( );
	}

	void runtime_t::set_autoload( const std::string_view id, const bool enabled )
	{
		if ( const auto script = find_script( *m_impl, id ) ) script->autoload.store( enabled );
		m_impl->save_runtime_state( );
	}

	void runtime_t::set_hot_reload( const std::string_view id, const bool enabled )
	{
		if ( const auto script = find_script( *m_impl, id ) ) script->hot_reload.store( enabled );
		m_impl->save_runtime_state( );
	}

	void runtime_t::reload( const std::string_view id )
	{
		if ( const auto script = find_script( *m_impl, id ) )
		{
			if ( script->enabled.load( ) ) script->reload_requested.store( true );
			else script->start( );
		}
	}

	bool runtime_t::import_script( const std::filesystem::path& source )
	{
		if ( !m_impl->initialized.load( std::memory_order_acquire ) ) return false;
		std::error_code error{};
		const auto resolved = std::filesystem::weakly_canonical( source, error );
		if ( error || !std::filesystem::is_regular_file( resolved, error )
			|| _wcsicmp( resolved.extension().c_str( ), L".lua" ) != 0 ) return false;
		const auto size = std::filesystem::file_size( resolved, error );
		if ( error || size == 0 || size > 8u * 1024u * 1024u ) return false;

		const auto target = m_impl->scripts_root / resolved.filename( );
		if ( std::filesystem::equivalent( resolved, target, error ) && !error )
		{
			refresh( );
			return true;
		}
		error.clear( );
		auto pending = target;
		pending += L".importing";
		std::filesystem::copy_file( resolved, pending,
			std::filesystem::copy_options::overwrite_existing, error );
		if ( error ) return false;
		if ( !::MoveFileExW( pending.c_str( ), target.c_str( ),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
		{
			std::filesystem::remove( pending, error );
			return false;
		}
		refresh( );
		return true;
	}

	std::filesystem::path runtime_t::root_path( ) const { return m_impl->root; }
	std::filesystem::path runtime_t::scripts_path( ) const { return m_impl->scripts_root; }

	const char* runtime_t::state_name( const script_state value ) noexcept
	{
		switch ( value )
		{
		case script_state::starting: return "Starting";
		case script_state::running: return "Running";
		case script_state::error: return "Error";
		case script_state::over_budget: return "Over budget";
		default: return "Stopped";
		}
	}

	void runtime_t::render( zdraw::draw_list& draw_list,
		const std::uint32_t width, const std::uint32_t height )
	{
		if ( !m_impl->initialized.load( std::memory_order_acquire ) ) return;
		std::vector<std::shared_ptr<implementation::script_instance>> scripts{};
		{
			std::scoped_lock lock( m_impl->scripts_mutex ); scripts = m_impl->instances;
		}
		if ( !config::general_settings.lua_enabled )
		{
			game::world().set_script_demand( game::script_data_demand::none );
			for ( const auto& script : scripts ) if ( script->enabled.load( ) ) script->request_stop( );
			return;
		}

		const auto any_active = std::ranges::any_of( scripts,
			[]( const auto& script ) { return script->enabled.load( std::memory_order_acquire ); } );
		std::uint32_t demand{};
		bool config_requested{};
		for ( const auto& script : scripts )
			if ( script->enabled.load( std::memory_order_acquire ) )
			{
				demand |= script->data_demand.load( std::memory_order_acquire );
				config_requested = config_requested
					|| script->config_demand.load( std::memory_order_acquire );
			}
		game::world().set_script_demand(
			static_cast<game::script_data_demand>( demand ) );
		if ( !any_active )
			return;

		const auto now = clock::now( );
		std::vector<implementation::config_patch> patches{};
		{
			std::scoped_lock lock( m_impl->config_mutex );
			patches.swap( m_impl->config_patches );
		}
		bool config_changed{};
		if ( !patches.empty( ) )
		{
			auto document = config::build_config_json( );
			bool changed{};
			for ( const auto& patch : patches ) changed |= set_json_path(
				document, patch.path, patch.value );
			if ( changed )
			{
				config::apply_config_json( document );
				m_impl->config_revision.fetch_add( 1, std::memory_order_acq_rel );
				config_changed = true;
			}
		}
		if ( config_changed || ( config_requested
			&& ( m_impl->last_config_publish == clock::time_point{}
				|| now - m_impl->last_config_publish >= std::chrono::milliseconds( 100 ) ) ) )
		{
			m_impl->last_config_publish = now;
			m_impl->config_snapshot.store(
				std::make_shared<const json>( config::build_config_json( ) ),
				std::memory_order_release );
		}

		if ( m_impl->last_frame_publish == clock::time_point{}
			|| now - m_impl->last_frame_publish >= k_tick_period )
		{
			m_impl->last_frame_publish = now;
			auto frame = std::make_shared<implementation::frame>( );
			frame->sequence = m_impl->frame_sequence.fetch_add( 1,
				std::memory_order_relaxed ) + 1;
			frame->timestamp = now; frame->width = width; frame->height = height;
			frame->game_valid = game::local_player().valid( );
			frame->menu_open = app::context().menu.is_open( );
			frame->input_ready = app::context().overlay.combat_input_ready( );
			frame->local_alive = game::local_player().alive( );
			frame->local_name = local_player_name( );
			frame->local_team = game::local_player().team( );
			frame->local_health = game::local_player().health( );
			frame->local_tick_base = game::local_player().tick_base( );
			frame->local_weapon_type = game::local_player().weapon_type( );
			frame->local_game_time = game::local_player().game_time( );
			frame->local_flash_alpha = game::local_player().flash_alpha( );
			if ( const auto map = app::workers::current_map( ) ) frame->map = *map;
			static_cast<void>( game::camera().sample_presentation( frame->camera ) );
			frame->players = game::world().players( ); frame->items = game::world().items( );
			frame->projectiles = game::world().projectiles( ); frame->spectators = game::world().spectators( );
			frame->bomb = features::visuals::bomb().info_snapshot( );
			frame->bomb_damage = features::visuals::bomb().player_damage_snapshot( );
			frame->confirmed_hit = features::visuals::bullet_impacts().latest_confirmed_hit( );
			m_impl->current_frame.store( std::move( frame ), std::memory_order_release );
		}
		const auto menu_open = app::context().menu.is_open( );
		const auto input_ready = app::context().overlay.combat_input_ready( );

		for ( const auto& script : scripts )
		{
			if ( menu_open || !input_ready ) script->release_requested.store( true );
			const auto commands = script->published_draw.load( std::memory_order_acquire );
			if ( !commands ) continue;
			for ( const auto& command : *commands )
			{
				switch ( command.kind )
				{
				case implementation::draw_kind::line:
					draw_list.add_line( command.a.x, command.a.y, command.b.x, command.b.y, command.color, command.thickness ); break;
				case implementation::draw_kind::rect:
					draw_list.add_rect( command.a.x, command.a.y, command.width, command.height, command.color, command.thickness ); break;
				case implementation::draw_kind::filled_rect:
					draw_list.add_rect_filled( command.a.x, command.a.y, command.width, command.height, command.color ); break;
				case implementation::draw_kind::circle:
					draw_list.add_circle( command.a.x, command.a.y, command.radius, command.color, 0, command.thickness ); break;
				case implementation::draw_kind::filled_circle:
					draw_list.add_circle_filled( command.a.x, command.a.y, command.radius, command.color, 0 ); break;
				case implementation::draw_kind::text:
					draw_list.add_text( command.a.x, command.a.y, command.text, zdraw::get_default_font( ), command.color ); break;
				case implementation::draw_kind::world_line:
				{
					const auto a = game::camera().project( command.a ); const auto b = game::camera().project( command.b );
					if ( game::camera().projection_valid( a ) && game::camera().projection_valid( b ) )
						draw_list.add_line( a.x, a.y, b.x, b.y, command.color, command.thickness );
					break;
				}
				case implementation::draw_kind::world_text:
				{
					const auto position = game::camera().project( command.a );
					if ( game::camera().projection_valid( position ) ) draw_list.add_text(
						position.x, position.y, command.text, zdraw::get_default_font( ), command.color );
					break;
				}
				case implementation::draw_kind::image:
				{
					std::scoped_lock lock( m_impl->textures_mutex );
					const auto found = m_impl->textures.find( command.texture );
					if ( found == m_impl->textures.end( ) ) break;
					materialize_texture( found->second, app::context().overlay.device( ) );
					if ( !found->second.view || !draw_list.m_im_draw_list ) break;
					const auto texture_id = static_cast<ImTextureID>(
						reinterpret_cast<std::uintptr_t>( found->second.view ) );
					draw_list.m_im_draw_list->AddImage( ImTextureRef{ texture_id },
						{ command.a.x, command.a.y },
						{ command.a.x + command.width, command.a.y + command.height },
						{ 0.0f, 0.0f }, { 1.0f, 1.0f },
						zdraw::draw_list::to_im_color( draw_list.scaled( command.color ) ) );
					break;
				}
				case implementation::draw_kind::panel:
				{
					auto background = zdraw::rgba{ 15, 16, 22, 218 };
					auto border = zdraw::rgba{ 54, 57, 69, 210 };
					draw_list.add_rect_filled( command.a.x, command.a.y,
						command.width, command.height, background );
					draw_list.add_rect( command.a.x, command.a.y,
						command.width, command.height, border, 1.0f );
					draw_list.add_rect_filled( command.a.x, command.a.y,
						command.width, 2.0f, command.color );
					draw_list.add_text( command.a.x + 10.0f, command.a.y + 8.0f,
						command.text, zdraw::get_default_font( ), { 236, 237, 242, 255 } );
					break;
				}
				}
			}
		}
	}

}
