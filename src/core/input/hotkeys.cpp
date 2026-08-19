#include <stdafx.hpp>
#include <core/input/hotkeys.hpp>

namespace platform::windows {

	namespace {

		[[nodiscard]] std::string trim( std::string value )
		{
			const auto whitespace = [ ]( const unsigned char character )
			{
				return std::isspace( character ) != 0;
			};
			while ( !value.empty( ) && whitespace( value.front( ) ) )
				value.erase( value.begin( ) );
			while ( !value.empty( ) && whitespace( value.back( ) ) )
				value.pop_back( );
			return value;
		}

		[[nodiscard]] std::uint16_t parse_key_name( std::string name )
		{
			name = trim( std::move( name ) );
			if ( name.size( ) >= 2 && ( ( name.front( ) == '"' && name.back( ) == '"' )
				|| ( name.front( ) == '\'' && name.back( ) == '\'' ) ) )
				name = name.substr( 1, name.size( ) - 2 );
			std::ranges::transform( name, name.begin( ), [ ]( const unsigned char value )
			{
				return static_cast<char>( std::toupper( value ) );
			} );
			name.erase( std::remove_if( name.begin( ), name.end( ), [ ]( const char value )
			{
				return value == ' ' || value == '_' || value == '-';
			} ), name.end( ) );

			if ( name.size( ) == 1 )
			{
				const auto value = name.front( );
				if ( ( value >= 'A' && value <= 'Z' ) || ( value >= '0' && value <= '9' ) )
					return static_cast<std::uint16_t>( value );
			}
			if ( name.size( ) >= 2 && name.front( ) == 'F' )
			{
				int number{};
				for ( auto index = std::size_t{ 1 }; index < name.size( ); ++index )
				{
					if ( name[index] < '0' || name[index] > '9' )
					{
						number = 0;
						break;
					}
					number = number * 10 + name[index] - '0';
				}
				if ( number >= 1 && number <= 24 )
					return static_cast<std::uint16_t>( VK_F1 + number - 1 );
			}

			static constexpr std::array names{
				std::pair{ std::string_view{ "INSERT" }, std::uint16_t{ VK_INSERT } },
				std::pair{ std::string_view{ "INS" }, std::uint16_t{ VK_INSERT } },
				std::pair{ std::string_view{ "DELETE" }, std::uint16_t{ VK_DELETE } },
				std::pair{ std::string_view{ "DEL" }, std::uint16_t{ VK_DELETE } },
				std::pair{ std::string_view{ "HOME" }, std::uint16_t{ VK_HOME } },
				std::pair{ std::string_view{ "END" }, std::uint16_t{ VK_END } },
				std::pair{ std::string_view{ "PAGEUP" }, std::uint16_t{ VK_PRIOR } },
				std::pair{ std::string_view{ "PGUP" }, std::uint16_t{ VK_PRIOR } },
				std::pair{ std::string_view{ "PAGEDOWN" }, std::uint16_t{ VK_NEXT } },
				std::pair{ std::string_view{ "PGDN" }, std::uint16_t{ VK_NEXT } },
				std::pair{ std::string_view{ "PAUSE" }, std::uint16_t{ VK_PAUSE } },
				std::pair{ std::string_view{ "SCROLLLOCK" }, std::uint16_t{ VK_SCROLL } },
				std::pair{ std::string_view{ "NUMLOCK" }, std::uint16_t{ VK_NUMLOCK } },
				std::pair{ std::string_view{ "BACKQUOTE" }, std::uint16_t{ VK_OEM_3 } },
				std::pair{ std::string_view{ "TILDE" }, std::uint16_t{ VK_OEM_3 } },
			};
			for ( const auto& [ key_name, virtual_key ] : names )
				if ( name == key_name ) return virtual_key;
			return 0;
		}

		void create_default_file( const std::filesystem::path& path )
		{
			if ( path.empty( ) ) return;
			std::error_code error{};
			if ( std::filesystem::exists( path, error ) ) return;
			std::filesystem::create_directories( path.parent_path( ), error );
			if ( error ) return;
			std::ofstream output( path, std::ios::binary | std::ios::trunc );
			if ( !output ) return;
			output <<
				"# Vesta lifecycle hotkeys / Системные клавиши Vesta\n"
				"# EN: Change only the value after '=' and restart Vesta.\n"
				"# RU: Измените только значение после '=' и перезапустите Vesta.\n"
				"# Supported examples / Примеры: INSERT, DELETE, HOME, END, PGUP, PGDN, F1...F24.\n"
				"# Compact keyboard example / Пример для компактной клавиатуры:\n"
				"# menu = DELETE\n"
				"# exit = HOME\n\n"
				"menu = INSERT\n"
				"exit = END\n";
		}

		[[nodiscard]] lifecycle_key_bindings load_keys( )
		{
			lifecycle_key_bindings result{ VK_INSERT, VK_END };
			const auto path = runtime_storage::area( "hotkeys.cfg" );
			create_default_file( path );
			std::ifstream input( path, std::ios::binary );
			std::string line{};
			while ( std::getline( input, line ) )
			{
				if ( line.size( ) >= 3 && static_cast<unsigned char>( line[0] ) == 0xef
					&& static_cast<unsigned char>( line[1] ) == 0xbb
					&& static_cast<unsigned char>( line[2] ) == 0xbf ) line.erase( 0, 3 );
				line = trim( std::move( line ) );
				if ( line.empty( ) || line.front( ) == '#' || line.front( ) == ';' )
					continue;
				const auto separator = line.find( '=' );
				if ( separator == std::string::npos ) continue;
				auto name = trim( line.substr( 0, separator ) );
				std::ranges::transform( name, name.begin( ), [ ]( const unsigned char value )
				{
					return static_cast<char>( std::tolower( value ) );
				} );
				auto value = line.substr( separator + 1 );
				if ( const auto comment = value.find_first_of( "#;" );
					comment != std::string::npos ) value.resize( comment );
				const auto key = parse_key_name( std::move( value ) );
				if ( !key ) continue;
				if ( name == "menu" || name == "menu_key" ) result.menu = key;
				else if ( name == "exit" || name == "exit_key" ) result.exit = key;
			}
			if ( result.menu == result.exit )
				result.exit = result.menu == VK_END ? VK_HOME : VK_END;
			return result;
		}

	}

	const lifecycle_key_bindings& lifecycle_keys( )
	{
		static const auto value = load_keys( );
		return value;
	}

	bool is_lifecycle_key( const std::uint16_t virtual_key )
	{
		const auto& keys = lifecycle_keys( );
		return virtual_key == VK_ESCAPE || virtual_key == keys.menu
			|| virtual_key == keys.exit;
	}

}
