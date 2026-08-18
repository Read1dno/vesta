#pragma once

#include <filesystem>
#include <string>

namespace scripting::radar_asset {

	struct overview
	{
		bool valid{};
		std::string error{};
		float pos_x{};
		float pos_y{};
		float scale{ 1.0f };
		float lower_altitude_max{};
		bool has_lower{};
		std::filesystem::path primary_path{};
		std::filesystem::path lower_path{};
		std::uint32_t width{};
		std::uint32_t height{};
	};

	[[nodiscard]] overview export_overview( const std::string& map_name,
		const std::filesystem::path& output_directory,
		const std::string& output_stem );

}
