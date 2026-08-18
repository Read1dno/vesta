#pragma once

#include <render/chams/mesh.hpp>
#include <core/assets/vpk.hpp>

#include <string>
#include <future>
#include <unordered_map>

namespace chams {

	class mesh_cache
	{
	public:

		[[nodiscard]] const skinned_mesh& get_or_build( vpk_archive& vpk, const std::string& model_path );

		void clear_memory( );

	private:
		[[nodiscard]] static std::string cache_file_path( const std::string& model_path );
		[[nodiscard]] static bool load_from_disk( const std::string& path, skinned_mesh& out );
		static void save_to_disk( const std::string& path, const skinned_mesh& mesh );

		std::unordered_map<std::string, skinned_mesh> m_memory{};
		std::unordered_map<std::string, std::future<skinned_mesh>> m_pending{};
	};

	inline mesh_cache g_mesh_cache{};

}
