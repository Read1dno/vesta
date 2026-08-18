#include <stdafx.hpp>

namespace foundation {

	void source_random::seed( int value ) noexcept
	{
		const auto magnitude = value < 0 ? -static_cast<std::int64_t>( value ) :
			static_cast<std::int64_t>( value );
		m_state = -static_cast<int>( magnitude % 2147483647LL );
		m_shuffle = 0;
		m_pool.fill( 0 );
		m_ready = false;
	}

	int source_random::advance( int value ) noexcept
	{
		const auto quotient = value / 127773;
		auto result = 16807 * ( value - quotient * 127773 ) - 2836 * quotient;
		return result < 0 ? result + 2147483647 : result;
	}

	int source_random::next_integer( ) noexcept
	{
		if ( !m_ready )
		{
			auto value = std::max( -m_state, 1 );
			for ( int warmup = 39; warmup >= 0; --warmup )
			{
				value = advance( value );
				if ( warmup < static_cast<int>( m_pool.size( ) ) )
					m_pool[ warmup ] = value;
			}
			m_state = value;
			m_shuffle = m_pool.front( );
			m_ready = true;
		}

		m_state = advance( m_state );
		const auto slot = m_shuffle / 0x4000000;
		m_shuffle = m_pool[ slot ];
		m_pool[ slot ] = m_state;
		return m_shuffle;
	}

	float source_random::uniform( float minimum, float maximum ) noexcept
	{
		const auto unit = std::min( 0.99999988f,
			static_cast<float>( next_integer( ) ) * 4.6566129e-10f );
		return std::lerp( minimum, maximum, unit );
	}

	std::uint32_t sha1_first_word( std::span<const std::byte> message ) noexcept
	{
		if ( message.size( ) > 55 ) return 0;
		std::array<std::uint8_t, 64> block{};
		std::memcpy( block.data( ), message.data( ), message.size( ) );
		block[ message.size( ) ] = 0x80;
		const auto bit_count = static_cast<std::uint64_t>( message.size( ) ) * 8;
		for ( int index = 0; index < 8; ++index )
			block[ 63 - index ] = static_cast<std::uint8_t>( bit_count >> ( index * 8 ) );

		std::array<std::uint32_t, 80> words{};
		for ( std::size_t index = 0; index < 16; ++index )
		{
			const auto offset = index * 4;
			words[ index ] =
				static_cast<std::uint32_t>( block[ offset ] ) << 24 |
				static_cast<std::uint32_t>( block[ offset + 1 ] ) << 16 |
				static_cast<std::uint32_t>( block[ offset + 2 ] ) << 8 |
				static_cast<std::uint32_t>( block[ offset + 3 ] );
		}
		for ( std::size_t index = 16; index < words.size( ); ++index )
			words[ index ] = std::rotl( words[ index - 3 ] ^ words[ index - 8 ] ^
				words[ index - 14 ] ^ words[ index - 16 ], 1 );

		std::uint32_t a = 0x67452301u;
		std::uint32_t b = 0xEFCDAB89u;
		std::uint32_t c = 0x98BADCFEu;
		std::uint32_t d = 0x10325476u;
		std::uint32_t e = 0xC3D2E1F0u;
		for ( std::size_t round = 0; round < words.size( ); ++round )
		{
			std::uint32_t mix{};
			std::uint32_t constant{};
			if ( round < 20 ) { mix = ( b & c ) | ( ~b & d ); constant = 0x5A827999u; }
			else if ( round < 40 ) { mix = b ^ c ^ d; constant = 0x6ED9EBA1u; }
			else if ( round < 60 ) { mix = ( b & c ) | ( b & d ) | ( c & d ); constant = 0x8F1BBCDCu; }
			else { mix = b ^ c ^ d; constant = 0xCA62C1D6u; }
			const auto next = std::rotl( a, 5 ) + mix + e + constant + words[ round ];
			e = d; d = c; c = std::rotl( b, 30 ); b = a; a = next;
		}

		return std::byteswap( 0x67452301u + a );
	}

}
