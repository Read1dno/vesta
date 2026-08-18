#pragma once

namespace platform::windows {

	class diagnostic_sink
	{
	public:
		[[nodiscard]] bool open( std::string_view title );

		template <typename... args_t>
		void info( std::format_string<args_t...> pattern, args_t&&... args ) const
		{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
			write( severity::information,
				std::format( pattern, std::forward<args_t>( args )... ) );
#else
			(void)pattern;
			( (void)args, ... );
#endif
		}

		template <typename... args_t>
		void warning( std::format_string<args_t...> pattern, args_t&&... args ) const
		{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
			write( severity::warning,
				std::format( pattern, std::forward<args_t>( args )... ) );
#else
			(void)pattern;
			( (void)args, ... );
#endif
		}

		template <typename... args_t>
		void success( std::format_string<args_t...> pattern, args_t&&... args ) const
		{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
			write( severity::success,
				std::format( pattern, std::forward<args_t>( args )... ) );
#else
			(void)pattern;
			( (void)args, ... );
#endif
		}

		template <typename... args_t>
		[[noreturn]] void fatal( std::format_string<args_t...> pattern,
			args_t&&... args ) const
		{
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
			write( severity::failure,
				std::format( pattern, std::forward<args_t>( args )... ) );
#else
			(void)pattern;
			( (void)args, ... );
#endif
			terminate( );
		}

	private:
		enum class severity : std::uint8_t
		{
			information,
			warning,
			success,
			failure,
		};

		void write( severity level, std::string_view message ) const;
		[[noreturn]] void terminate( ) const;

		void* m_output{};
	};

}
