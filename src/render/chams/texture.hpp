#pragma once

#include <core/assets/resource.hpp>
#include <core/assets/vpk.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chams {

	struct texture_data
	{
		std::uint32_t width{};
		std::uint32_t height{};
		std::uint32_t mip_count{};
		std::uint32_t dxgi_format{}; // DXGI_FORMAT, already translated
		std::uint32_t block_size{};  // bytes per 4x4 block, 0 for uncompressed formats

		// Mip 0 first, matching the order D3D11's subresource array expects --
		// i.e. already reversed relative to how the file stores them.
		std::vector<std::vector<std::uint8_t>> mips{};

		[[nodiscard]] bool valid( ) const { return !mips.empty( ) && width > 0 && height > 0; }
	};

	[[nodiscard]] texture_data load_texture( vpk_archive& vpk, const std::string& archive_path );

} // namespace chams
