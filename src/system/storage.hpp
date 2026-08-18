#pragma once

#include <filesystem>

namespace platform::windows::runtime_storage {

	[[nodiscard]] std::filesystem::path root( );
	[[nodiscard]] std::filesystem::path area( std::string_view name );

}
