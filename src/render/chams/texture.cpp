#include <stdafx.hpp>
#include <render/chams/texture.hpp>

#include <dxgiformat.h>
#include <lz4.h>

namespace chams {

	namespace {

		enum vtex_format : std::uint8_t
		{
			vtex_dxt1 = 1,
			vtex_dxt5 = 2,
			vtex_rgba8888 = 4,
			vtex_bgra8888 = 28,
			vtex_bc6h = 19,
			vtex_bc7 = 20,
			vtex_ati2n = 21,
			vtex_ati1n = 27,
		};

		struct format_info
		{
			std::uint32_t dxgi{};
			std::uint32_t block_size{};
			std::uint32_t bytes_per_pixel{};
		};

		[[nodiscard]] bool translate_format( std::uint8_t format, format_info& out )
		{
			switch ( format )
			{
			case vtex_dxt1:     out = { DXGI_FORMAT_BC1_UNORM, 8, 0 }; return true;
			case vtex_dxt5:     out = { DXGI_FORMAT_BC3_UNORM, 16, 0 }; return true;
			case vtex_bc7:      out = { DXGI_FORMAT_BC7_UNORM, 16, 0 }; return true;
			case vtex_bc6h:     out = { DXGI_FORMAT_BC6H_UF16, 16, 0 }; return true;
			case vtex_ati2n:    out = { DXGI_FORMAT_BC5_UNORM, 16, 0 }; return true;
			case vtex_ati1n:    out = { DXGI_FORMAT_BC4_UNORM, 8, 0 }; return true;
			case vtex_rgba8888: out = { DXGI_FORMAT_R8G8B8A8_UNORM, 0, 4 }; return true;
			case vtex_bgra8888: out = { DXGI_FORMAT_B8G8R8A8_UNORM, 0, 4 }; return true;
			default: return false;
			}
		}

		[[nodiscard]] std::size_t mip_byte_size( const format_info& info, std::uint32_t width, std::uint32_t height, std::uint32_t level )
		{
			const auto w = std::max<std::uint32_t>( 1, width >> level );
			const auto h = std::max<std::uint32_t>( 1, height >> level );

			if ( info.block_size == 0 )
			{
				return static_cast< std::size_t >( w ) * h * info.bytes_per_pixel;
			}

			const auto blocks_x = std::max<std::uint32_t>( 1, ( w + 3 ) / 4 );
			const auto blocks_y = std::max<std::uint32_t>( 1, ( h + 3 ) / 4 );
			return static_cast< std::size_t >( blocks_x ) * blocks_y * info.block_size;
		}

		constexpr std::uint32_t k_extra_compressed_mip_size{ 4 };

	}

	texture_data load_texture( vpk_archive& vpk, const std::string& archive_path )
	{
		texture_data result{};

		try
		{

			auto path = archive_path;
			if ( path.ends_with( ".vtex" ) )
			{
				path += "_c";
			}

			const auto* entry = vpk.find( path );
			if ( !entry )
			{
				return result;
			}

			resource res{};
			if ( !res.parse( vpk.read( *entry ) ) )
			{
				return result;
			}

			const auto* data_block = res.find( "DATA" );
			if ( !data_block )
			{
				return result;
			}

			const auto* header = res.bytes( *data_block );
			if ( !header || data_block->size < 40 )
			{
				return result;
			}

			const auto read_u16 = [ & ]( std::size_t offset ) {
				std::uint16_t v{}; std::memcpy( &v, header + offset, 2 ); return v; };
			const auto read_u32 = [ & ]( std::size_t offset ) {
				std::uint32_t v{}; std::memcpy( &v, header + offset, 4 ); return v; };

			if ( read_u16( 0 ) != 1 )
			{
				return result;
			}

			const auto width = read_u16( 20 );
			const auto height = read_u16( 22 );
			const auto depth = read_u16( 24 );
			const auto format = header[ 26 ];
			const auto mip_count = header[ 27 ];

			format_info info{};
			if ( !width || !height || depth != 1 || !mip_count || !translate_format( format, info ) )
			{
				return result;
			}

			const auto extra_offset = read_u32( 32 );
			const auto extra_count = read_u32( 36 );

			std::vector<std::uint32_t> stored_sizes{};
			for ( std::uint32_t i = 0; i < extra_count; ++i )
			{
				const auto entry_pos = 32 + extra_offset + static_cast< std::size_t >( i ) * 12;
				if ( entry_pos + 12 > data_block->size )
				{
					break;
				}

				const auto type = read_u32( entry_pos );
				const auto payload_offset = read_u32( entry_pos + 4 );
				if ( type != k_extra_compressed_mip_size )
				{
					continue;
				}

				const auto payload = entry_pos + 4 + payload_offset;
				if ( payload + 12 > data_block->size )
				{
					break;
				}

				const auto table_count = read_u32( payload + 8 );
				if ( table_count != mip_count || payload + 12 + table_count * 4u > data_block->size )
				{
					break;
				}

				stored_sizes.resize( table_count );
				for ( std::uint32_t m = 0; m < table_count; ++m )
				{
					stored_sizes[ m ] = read_u32( payload + 12 + static_cast< std::size_t >( m ) * 4 );
				}
				break;
			}

			const auto& owned = res.data( );
			const auto image_offset = static_cast< std::size_t >( data_block->offset ) + data_block->size;
			if ( image_offset > owned.size( ) )
			{
				return result;
			}

			const auto* image = owned.data( ) + image_offset;
			const auto image_size = owned.size( ) - image_offset;

			if ( stored_sizes.empty( ) )
			{

				stored_sizes.resize( mip_count );
				for ( std::uint32_t m = 0; m < mip_count; ++m )
				{
					stored_sizes[ m ] = static_cast< std::uint32_t >( mip_byte_size( info, width, height, m ) );
				}
			}

			result.mips.resize( mip_count );

			std::size_t cursor{};
			for ( int level = static_cast< int >( mip_count ) - 1; level >= 0; --level )
			{
				const auto stored = stored_sizes[ static_cast< std::size_t >( level ) ];
				const auto raw = mip_byte_size( info, width, height, static_cast< std::uint32_t >( level ) );

				if ( cursor + stored > image_size )
				{
					return {};
				}

				std::vector<std::uint8_t> mip( raw );

				if ( stored == raw )
				{

					std::memcpy( mip.data( ), image + cursor, raw );
				}
				else
				{
					const auto decoded = LZ4_decompress_safe(
						reinterpret_cast< const char* >( image + cursor ),
						reinterpret_cast< char* >( mip.data( ) ),
						static_cast< int >( stored ),
						static_cast< int >( raw ) );

					if ( decoded != static_cast< int >( raw ) )
					{
						return {};
					}
				}

				result.mips[ static_cast< std::size_t >( level ) ] = std::move( mip );
				cursor += stored;
			}

			result.width = width;
			result.height = height;
			result.mip_count = mip_count;
			result.dxgi_format = info.dxgi;
			result.block_size = info.block_size;
			return result;
		}
		catch ( const std::exception& )
		{
			return {};
		}
	}

}
