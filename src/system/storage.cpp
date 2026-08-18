#include <stdafx.hpp>

namespace platform::windows::runtime_storage {

	std::filesystem::path root( )
	{
		std::error_code error{};
		const auto temporary = std::filesystem::temp_directory_path( error );
		return error ? std::filesystem::path{} : temporary / "vesta";
	}

	std::filesystem::path area( std::string_view name )
	{
		const auto base = root( );
		return base.empty( ) ? std::filesystem::path{} : base / name;
	}

}
