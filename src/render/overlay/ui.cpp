#include <render/overlay/ui.hpp>

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
