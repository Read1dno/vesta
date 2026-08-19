#include <render/overlay/ui.hpp>

#include <tlhelp32.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace
{
#if defined( _DEBUG ) || ( defined( VESTA_PERF_LOG ) && VESTA_PERF_LOG )
	void write_startup_event( const char* event )
	{
		wchar_t path[ MAX_PATH ]{};
		const DWORD length = ::GetTempPathW( MAX_PATH, path );
		if ( length == 0 || length >= MAX_PATH - 32 )
			return;
		::wcscat_s( path, L"vesta" );
		::CreateDirectoryW( path, nullptr );
		::wcscat_s( path, L"\\elevation.log" );

		const HWND foreground = ::GetForegroundWindow( );
		DWORD foreground_process{};
		if ( foreground )
			::GetWindowThreadProcessId( foreground, &foreground_process );
		char foreground_class[ 96 ]{};
		if ( foreground )
			::GetClassNameA( foreground, foreground_class,
				static_cast<int>( std::size( foreground_class ) ) );

		char line[ 384 ]{};
		const int bytes = ::sprintf_s( line,
			"tick=%llu pid=%lu event=%s foreground=0x%p foreground_pid=%lu class=%s\r\n",
			::GetTickCount64( ), ::GetCurrentProcessId( ), event,
			foreground, foreground_process, foreground_class );
		if ( bytes <= 0 )
			return;

		const HANDLE file = ::CreateFileW( path, FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( file == INVALID_HANDLE_VALUE )
			return;
		DWORD written{};
		::WriteFile( file, line, static_cast<DWORD>( bytes ), &written, nullptr );
		::CloseHandle( file );
	}
#else
	void write_startup_event( const char* ) {}
#endif

	DWORD current_image_path( std::wstring& output )
	{
		std::vector<wchar_t> buffer( 512 );
		for ( ;; )
		{
			const DWORD length = ::GetModuleFileNameW(
				nullptr, buffer.data( ), static_cast<DWORD>( buffer.size( ) ) );
			if ( length == 0 )
				return ::GetLastError( );

			if ( length < buffer.size( ) - 1 )
			{
				output.assign( buffer.data( ), length );
				return ERROR_SUCCESS;
			}

			if ( buffer.size( ) >= 32768 )
				return ERROR_INSUFFICIENT_BUFFER;
			buffer.resize( buffer.size( ) * 2 );
		}
	}

	DWORD duplicate_winlogon_token(
		const DWORD session_id,
		const DWORD desired_access,
		HANDLE& output )
	{
		output = nullptr;
		PRIVILEGE_SET privileges{};
		privileges.PrivilegeCount = 1;
		privileges.Control = PRIVILEGE_SET_ALL_NECESSARY;
		if ( !::LookupPrivilegeValueW(
			nullptr, L"SeTcbPrivilege", &privileges.Privilege[ 0 ].Luid ) )
		{
			return ::GetLastError( );
		}

		const HANDLE snapshot = ::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
		if ( snapshot == INVALID_HANDLE_VALUE )
			return ::GetLastError( );

		DWORD result = ERROR_NOT_FOUND;
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof( entry );
		for ( BOOL more = ::Process32FirstW( snapshot, &entry );
			more;
			more = ::Process32NextW( snapshot, &entry ) )
		{
			if ( ::lstrcmpiW( entry.szExeFile, L"winlogon.exe" ) != 0 )
				continue;

			const HANDLE process = ::OpenProcess(
				PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID );
			if ( !process )
				continue;

			HANDLE token{};
			if ( ::OpenProcessToken(
				process, TOKEN_QUERY | TOKEN_DUPLICATE, &token ) )
			{
				BOOL has_tcb{};
				DWORD token_session{};
				DWORD returned{};
				if ( ::PrivilegeCheck( token, &privileges, &has_tcb )
					&& has_tcb
					&& ::GetTokenInformation(
						token, TokenSessionId, &token_session,
						sizeof( token_session ), &returned )
					&& token_session == session_id )
				{
					if ( ::DuplicateTokenEx(
						token, desired_access, nullptr,
						SecurityImpersonation, TokenImpersonation, &output ) )
					{
						result = ERROR_SUCCESS;
					}
					else
					{
						result = ::GetLastError( );
					}
				}
				::CloseHandle( token );
			}
			::CloseHandle( process );
			if ( result != ERROR_NOT_FOUND )
				break;
		}

		::CloseHandle( snapshot );
		return result;
	}

	DWORD create_token( HANDLE& output )
	{
		output = nullptr;
		HANDLE self{};
		if ( !::OpenProcessToken(
			::GetCurrentProcess( ), TOKEN_QUERY | TOKEN_DUPLICATE, &self ) )
		{
			return ::GetLastError( );
		}

		DWORD session_id{};
		DWORD returned{};
		if ( !::GetTokenInformation(
			self, TokenSessionId, &session_id, sizeof( session_id ), &returned ) )
		{
			const DWORD result = ::GetLastError( );
			::CloseHandle( self );
			return result;
		}

		HANDLE system{};
		DWORD result = duplicate_winlogon_token(
			session_id, TOKEN_IMPERSONATE, system );
		if ( result != ERROR_SUCCESS )
		{
			::CloseHandle( self );
			return result;
		}

		if ( !::SetThreadToken( nullptr, system ) )
		{
			result = ::GetLastError( );
			::CloseHandle( system );
			::CloseHandle( self );
			return result;
		}

		if ( !::DuplicateTokenEx(
			self,
			TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
			nullptr, SecurityAnonymous, TokenPrimary, &output ) )
		{
			result = ::GetLastError( );
		}
		else
		{
			BOOL value = TRUE;
			if ( !::SetTokenInformation(
				output, TokenUIAccess, &value, sizeof( value ) ) )
			{
				result = ::GetLastError( );
				::CloseHandle( output );
				output = nullptr;
			}
			else
			{
				result = ERROR_SUCCESS;
			}
		}

		::RevertToSelf( );
		::CloseHandle( system );
		::CloseHandle( self );
		return result;
	}
}

bool ui_access::elevated( )
{
	HANDLE token{};
	if ( !::OpenProcessToken( ::GetCurrentProcess( ), TOKEN_QUERY, &token ) )
		return false;

	TOKEN_ELEVATION elevation{};
	DWORD returned{};
	const BOOL success = ::GetTokenInformation(
		token, TokenElevation, &elevation, sizeof( elevation ), &returned );
	::CloseHandle( token );
	return success && elevation.TokenIsElevated != FALSE;
}

DWORD ui_access::elevate( )
{
	if ( elevated( ) )
		return ERROR_SUCCESS;
	write_startup_event( "bootstrap.elevation_request" );

	std::wstring application_name;
	DWORD result = current_image_path( application_name );
	if ( result != ERROR_SUCCESS )
		return result;

	SHELLEXECUTEINFOW execute{};
	execute.cbSize = sizeof( execute );
	execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	execute.lpVerb = L"runas";
	execute.lpFile = application_name.c_str( );
	execute.lpParameters = L"--vesta-elevated";
	execute.nShow = SW_SHOWNORMAL;
	if ( !::ShellExecuteExW( &execute ) )
		return ::GetLastError( );
	write_startup_event( "bootstrap.elevation_started" );

	if ( execute.hProcess )
		::CloseHandle( execute.hProcess );
	return ERROR_SUCCESS;
}

bool ui_access::enabled( )
{
	HANDLE token{};
	if ( !::OpenProcessToken( ::GetCurrentProcess( ), TOKEN_QUERY, &token ) )
		return false;

	BOOL value{};
	DWORD returned{};
	const BOOL success = ::GetTokenInformation(
		token, TokenUIAccess, &value, sizeof( value ), &returned );
	::CloseHandle( token );
	return success && value != FALSE;
}

DWORD ui_access::prepare( )
{
	write_startup_event( enabled( )
		? "uiaccess.worker_enter" : "elevated.worker_enter" );
	if ( enabled( ) )
		return ERROR_SUCCESS;

	HANDLE token{};
	DWORD result = create_token( token );
	if ( result != ERROR_SUCCESS )
		return result;

	std::wstring application_name;
	result = current_image_path( application_name );
	if ( result != ERROR_SUCCESS )
	{
		::CloseHandle( token );
		return result;
	}

	STARTUPINFOW startup{};
	startup.cb = sizeof( startup );
	wchar_t interactive_desktop[]{ L"winsta0\\default" };
	startup.lpDesktop = interactive_desktop;
	PROCESS_INFORMATION process{};
	std::wstring command_line = ::GetCommandLineW( );
	if ( !::CreateProcessAsUserW(
		token, application_name.c_str( ), command_line.data( ), nullptr, nullptr, FALSE,
		0, nullptr, nullptr, &startup, &process ) )
	{
		result = ::GetLastError( );
		::CloseHandle( token );
		return result;
	}
	write_startup_event( "elevated.uiaccess_started" );

	::CloseHandle( process.hThread );
	::CloseHandle( process.hProcess );
	::CloseHandle( token );
	::ExitProcess( EXIT_SUCCESS );
}
