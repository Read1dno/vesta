#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace config {

	struct configuration_store
	{
		static constexpr const char* k_config_ext = ".cfg";
		static constexpr int k_max_name_len = 64;

		char name_buffer[ k_max_name_len ]{ "config" };

		bool write_to( const std::string& path ) const;
		bool read_from( const std::string& path );
		bool write_cache( ) const;
		bool read_cache( );
		[[nodiscard]] std::string cache_path( ) const;

		void set_active_path( const std::string& full_path );
	};

}
