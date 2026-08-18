#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chams {

	class resource
	{
	public:
		struct block
		{
			char type[ 5 ]{};
			std::uint32_t offset{};
			std::uint32_t size{};

			[[nodiscard]] bool is( const char* name ) const
			{
				return this->type[ 0 ] == name[ 0 ] && this->type[ 1 ] == name[ 1 ]
					&& this->type[ 2 ] == name[ 2 ] && this->type[ 3 ] == name[ 3 ];
			}
		};

		bool parse( std::vector<std::uint8_t> file_data );

		[[nodiscard]] bool valid( ) const { return this->m_valid; }
		[[nodiscard]] const std::vector<block>& blocks( ) const { return this->m_blocks; }
		[[nodiscard]] const std::vector<std::uint8_t>& data( ) const { return this->m_data; }

		[[nodiscard]] const block* find( const char* type ) const;
		[[nodiscard]] std::vector<const block*> find_all( const char* type ) const;

		[[nodiscard]] const std::uint8_t* bytes( const block& b ) const
		{
			return this->m_data.data( ) + b.offset;
		}

	private:
		std::vector<std::uint8_t> m_data{};
		std::vector<block> m_blocks{};
		bool m_valid{ false };
	};

}
