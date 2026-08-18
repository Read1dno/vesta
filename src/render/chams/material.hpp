#pragma once

#include <core/assets/vpk.hpp>

#include <string>
#include <vector>

namespace chams {

	struct material_data
	{
		std::string shader{};

		std::string color{};
		std::string normal{};
		std::string metalness{};
		std::string ambient_occlusion{};
		std::string gloss{};

		float uv_scale[ 2 ]{ 1.0f, 1.0f };
		float uv_offset[ 2 ]{ 0.0f, 0.0f };

		float color_tint[ 4 ]{ 1.0f, 1.0f, 1.0f, 1.0f };

		[[nodiscard]] bool valid( ) const { return !color.empty( ); }
	};

	[[nodiscard]] material_data load_material( vpk_archive& vpk, const std::string& archive_path );

}
