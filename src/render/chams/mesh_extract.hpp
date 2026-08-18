#pragma once

#include <render/chams/mesh.hpp>
#include <core/assets/vpk.hpp>

#include <string>

namespace chams {

	[[nodiscard]] skinned_mesh extract_mesh( vpk_archive& vpk, const std::string& model_path );

}
