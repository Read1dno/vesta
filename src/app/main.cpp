#include <stdafx.hpp>
#include <app/context.hpp>
#include <app/workers.hpp>
#include <render/overlay/ui.hpp>
#include <scripting/runtime.hpp>

#include <timeapi.h>
#pragma comment( lib, "winmm.lib" )

namespace
{
	struct timer_period_guard
	{
		timer_period_guard( ) { timeBeginPeriod( 1 ); }
		~timer_period_guard( ) { timeEndPeriod( 1 ); }
	};

	struct single_instance_guard
	{
		single_instance_guard( )
		{
			handle = CreateMutexW( nullptr, FALSE, L"Local\\vesta.overlay.single-instance" );
			acquired = handle != nullptr && GetLastError( ) != ERROR_ALREADY_EXISTS;
		}

		~single_instance_guard( )
		{
			if ( handle != nullptr )
			{
				CloseHandle( handle );
			}
		}

		HANDLE handle{};
		bool acquired{};
	};

}

int main( )
{
	::SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
#if defined( VESTA_PERF_LOG ) && VESTA_PERF_LOG

	const DWORD ui_access_result = ERROR_SUCCESS;
#else
	const DWORD ui_access_result = ui_access::prepare( );
#endif
	const single_instance_guard instance{};
	if ( !instance.acquired )
	{
		return 0;
	}

	{
		if ( !app::context().diagnostics.open( " :> " ) )
		{
			return 1;
		}

		if ( ui_access_result != ERROR_SUCCESS )
		{
			app::context().diagnostics.warning(
				"UIAccess initialization failed with Win32 error {}; continuing without UIAccess.",
				ui_access_result );
		}
	}

	const timer_period_guard timer_period{};

	{

		if ( !app::context().process.attach( L"cs2.exe" ) )
		{
			return 1;
		}

		if ( !app::context().input.connect( ) )
		{
			return 1;
		}
	}

	{
		if ( !app::context().modules.discover( app::context().process ) )
		{
			return 1;
		}

		if ( !app::context().addresses.initialize( ) )
		{
			return 1;
		}

		if ( !game::fields( ).initialize( ) )
		{
			return 1;
		}
	}

	{

		config::apply_default_config( );
		if ( !config::storage.read_cache( ) )
		{
			config::storage.write_cache( );
		}
		if ( !scripting::runtime().initialize( ) )
		{
			app::context().diagnostics.warning(
				"Lua runtime storage could not be initialized; scripting is disabled." );
			config::general_settings.lua_enabled = false;
		}

		const auto game_process = static_cast<HANDLE>(
			app::context().process.native_handle( ) );
		std::thread( [game_process]
		{
			if ( ::WaitForSingleObject( game_process, INFINITE ) != WAIT_OBJECT_0 )
				return;

			::ClipCursor( nullptr );
			app::context().input.set_key_gate( 0, false );
			app::context().input.set_movement_gate( {}, false );
			app::context().input.key( VK_ESCAPE, false );
			const std::array movement_releases{
				platform::windows::input_gateway::key_transition{ VK_CONTROL, false },
				platform::windows::input_gateway::key_transition{ VK_F24, false },
			};
			app::context().input.keys( movement_releases );
			app::context().input.pointer(
				0, 0,
				platform::windows::pointer_action::primary_up
					| platform::windows::pointer_action::secondary_up );

			app::context().overlay.request_shutdown( );
		} ).detach( );

		std::thread( app::workers::game ).detach( );
		std::thread( app::workers::pose_sampler ).detach( );
		std::thread( app::workers::movement ).detach( );
		std::thread( app::workers::combat ).detach( );
		std::thread( app::workers::nade_helper ).detach( );
		std::thread( app::workers::seed_trigger ).detach( );
#if defined( VESTA_ENABLE_CONSOLE ) && VESTA_ENABLE_CONSOLE
		std::thread( app::workers::watchdog ).detach( );
#endif

		if ( !app::context().overlay.launch( ) )
		{
			scripting::runtime().shutdown( );
			return 1;
		}
	}
	scripting::runtime().shutdown( );

	::ExitProcess( EXIT_SUCCESS );
}
