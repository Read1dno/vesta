#pragma once

#include <windows.h>

namespace ui_access
{
	[[nodiscard]] bool elevated( );
	[[nodiscard]] DWORD elevate( );
	[[nodiscard]] DWORD prepare( );
	[[nodiscard]] bool enabled( );
}
