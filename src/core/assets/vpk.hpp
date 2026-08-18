#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace chams {

	class vpk_archive
	{
	public:
		struct entry
		{
			std::uint32_t crc{};
			std::uint16_t archive_index{};
			std::uint32_t offset{};
			std::uint32_t length{};

			std::vector<std::uint8_t> preload{};
		};

		bool open( const std::string& dir_vpk_path );

		bool open_selected( const std::string& dir_vpk_path,
			const std::vector<std::string>& archive_paths );

		[[nodiscard]] bool is_open( ) const { return this->m_open; }
		[[nodiscard]] std::size_t count( ) const { return this->m_entries.size( ); }
		[[nodiscard]] const std::string& path( ) const { return this->m_dir_path; }

		[[nodiscard]] const entry* find( const std::string& archive_path ) const;
		[[nodiscard]] std::vector<std::uint8_t> read( const entry& e ) const;
		[[nodiscard]] std::vector<std::string> list_prefix( const std::string& prefix ) const;

		[[nodiscard]] static std::string locate_cs2_pak( );

		[[nodiscard]] static std::string locate_map_vpk( const std::string& map_name );

	private:
		std::unordered_map<std::string, entry> m_entries{};
		std::string m_dir_path{};
		std::uint32_t m_data_start{};
		bool m_open{ false };
	};

}
