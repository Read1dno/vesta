#include <stdafx.hpp>

namespace platform::windows {

	bool diagnostic_sink::open( std::string_view title )
	{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
		m_output = ::GetStdHandle( STD_OUTPUT_HANDLE );
		if ( !m_output || m_output == INVALID_HANDLE_VALUE )
			return false;

		DWORD mode{};
		if ( ::GetConsoleMode( m_output, &mode ) )
			::SetConsoleMode( m_output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING );

		const std::string owned_title{ title };
		::SetConsoleTitleA( owned_title.c_str( ) );
		info( "diagnostics ready" );
#else
		(void)title;
#endif
		return true;
	}

	void diagnostic_sink::write( severity level, std::string_view message ) const
	{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
		const auto marker = [ level ]( ) -> std::string_view
		{
			switch ( level )
			{
			case severity::warning: return "[38;2;238;185;90mwarn";
			case severity::success: return "[38;2;92;205;145mok";
			case severity::failure: return "[38;2;235;105;105merror";
			default: return "[38;2;135;150;235minfo";
			}
		}( );
		std::printf( "  [%.*s[0m] %.*s\n",
			static_cast<int>( marker.size( ) ), marker.data( ),
			static_cast<int>( message.size( ) ), message.data( ) );
		std::fflush( stdout );
#else
		(void)level;
		(void)message;
#endif
	}

	void diagnostic_sink::terminate( ) const
	{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
		std::fputs( "\n  press Enter to close...", stdout );
		std::fflush( stdout );
		(void)std::getchar( );
		std::exit( EXIT_FAILURE );
#else
		std::terminate( );
#endif
	}

}
