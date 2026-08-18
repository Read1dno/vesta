#include <stdafx.hpp>
#include <core/assets/kv3.hpp>

#include <lz4.h>
#include <zstd.h>

#include <cstring>
#include <stdexcept>

namespace chams::kv3 {

	namespace {

		constexpr std::uint32_t k_magic0{ 0x03564B56 };
		constexpr std::uint32_t k_kv3_prefix{ 0x4B563300 };

		enum class node_type : std::uint8_t
		{
			null_value = 1,
			boolean = 2,
			int64 = 3,
			uint64 = 4,
			double_value = 5,
			string = 6,
			binary_blob = 7,
			array_value = 8,
			object_value = 9,
			array_typed = 10,
			int32 = 11,
			uint32 = 12,
			boolean_true = 13,
			boolean_false = 14,
			int64_zero = 15,
			int64_one = 16,
			double_zero = 17,
			double_one = 18,
			float_value = 19,
			int16 = 20,
			uint16 = 21,
			int32_as_byte = 23,
			array_type_byte_length = 24,
			array_type_auxiliary_buffer = 25,
		};

		class cursor
		{
		public:
			cursor( ) = default;
			cursor( const std::uint8_t* data, std::size_t start, std::size_t end )
				: m_data( data ), m_pos( start ), m_end( end ) { }

			[[nodiscard]] const std::uint8_t* read( std::size_t n )
			{
				if ( m_pos + n > m_end )
				{
					throw std::runtime_error( "kv3: cursor overrun" );
				}

				const auto* p = m_data + m_pos;
				m_pos += n;
				return p;
			}

			template <typename T>
			[[nodiscard]] T read_pod( )
			{
				T value{};
				std::memcpy( &value, read( sizeof( T ) ), sizeof( T ) );
				return value;
			}

			[[nodiscard]] std::size_t remaining( ) const { return m_end - m_pos; }
			[[nodiscard]] std::size_t pos( ) const { return m_pos; }

		private:
			const std::uint8_t* m_data{};
			std::size_t m_pos{};
			std::size_t m_end{};
		};

		struct buffers
		{
			cursor bytes1{};
			cursor bytes2{};
			cursor bytes4{};
			cursor bytes8{};
		};

		struct context
		{
			int version{};
			cursor types{};
			cursor object_lengths{};
			cursor binary_blobs{};
			cursor binary_blob_lengths{};
			std::vector<std::string> strings{};
			buffers* buffer{};
			buffers* auxiliary{};
		};

		[[nodiscard]] std::size_t align_up( std::size_t offset, std::size_t alignment )
		{
			--alignment;
			return ( offset + alignment ) & ~alignment;
		}

		[[nodiscard]] std::string read_null_term_utf8( cursor& c )
		{

			const auto* start = c.read( 0 );
			std::size_t len = 0;
			while ( len < c.remaining( ) && start[ len ] != 0 )
			{
				++len;
			}

			if ( len >= c.remaining( ) )
			{
				throw std::runtime_error( "kv3: unterminated string in string table" );
			}

			std::string result( reinterpret_cast< const char* >( start ), len );
			static_cast< void >( c.read( len + 1 ) );
			return result;
		}

		[[nodiscard]] std::vector<std::uint8_t> lz4_chain_decode(
			cursor& reader, const std::vector<std::uint16_t>& chunk_sizes,
			const std::int32_t* blob_lengths, std::size_t blob_count,
			std::uint16_t frame_size, std::uint32_t total_size )
		{
			std::vector<std::uint8_t> out{};
			out.reserve( total_size );

			LZ4_streamDecode_t stream{};
			LZ4_setStreamDecode( &stream, nullptr, 0 );

			std::size_t chunk_index = 0;
			for ( std::size_t bi = 0; bi < blob_count; ++bi )
			{
				auto remaining = static_cast< std::uint32_t >( blob_lengths[ bi ] );
				while ( remaining > 0 )
				{
					if ( chunk_index >= chunk_sizes.size( ) )
					{
						throw std::runtime_error( "kv3: LZ4 blob chunk table underrun" );
					}

					const auto comp_len = chunk_sizes[ chunk_index++ ];
					const auto want = std::min<std::uint32_t>( frame_size, remaining );
					const auto* compressed = reader.read( comp_len );

					const auto base = out.size( );
					out.resize( base + want );

					const auto written = LZ4_decompress_safe_continue(
						&stream, reinterpret_cast< const char* >( compressed ),
						reinterpret_cast< char* >( out.data( ) + base ),
						static_cast< int >( comp_len ), static_cast< int >( want ) );

					if ( written < 0 || static_cast< std::uint32_t >( written ) != want )
					{
						throw std::runtime_error( "kv3: chained LZ4 block decode failed" );
					}

					remaining -= want;
				}
			}

			if ( out.size( ) != total_size )
			{
				throw std::runtime_error( "kv3: binary blob chain size mismatch" );
			}

			return out;
		}

		[[nodiscard]] std::vector<std::uint8_t> lz4_block_decode(
			const std::uint8_t* compressed, std::size_t compressed_size, std::size_t uncompressed_size )
		{
			std::vector<std::uint8_t> out( uncompressed_size );

			const auto written = LZ4_decompress_safe(
				reinterpret_cast< const char* >( compressed ), reinterpret_cast< char* >( out.data( ) ),
				static_cast< int >( compressed_size ), static_cast< int >( uncompressed_size ) );

			if ( written < 0 || static_cast< std::size_t >( written ) != uncompressed_size )
			{
				throw std::runtime_error( "kv3: LZ4 block decode failed or size mismatch" );
			}

			return out;
		}

		[[nodiscard]] std::vector<std::uint8_t> zstd_decode(
			const std::uint8_t* compressed, std::size_t compressed_size, std::size_t uncompressed_size )
		{
			std::vector<std::uint8_t> out( uncompressed_size );

			const auto written = ZSTD_decompress(
				out.data( ), uncompressed_size, compressed, compressed_size );

			if ( ZSTD_isError( written ) || written != uncompressed_size )
			{
				throw std::runtime_error( "kv3: ZSTD decode failed or size mismatch" );
			}

			return out;
		}

		[[nodiscard]] std::pair<std::uint8_t, std::uint8_t> read_type( context& ctx )
		{
			auto databyte = ctx.types.read( 1 )[ 0 ];
			std::uint8_t flag{ 0 };

			if ( ctx.version >= 3 )
			{
				if ( databyte & 0x80 )
				{
					databyte &= 0x3F;
					flag = ctx.types.read( 1 )[ 0 ];
				}
			}
			else if ( databyte & 0x80 )
			{
				databyte &= 0x7F;
				flag = ctx.types.read( 1 )[ 0 ];
			}

			return { databyte, flag };
		}

		[[nodiscard]] object read_value( context& ctx, std::uint8_t datatype );

		void parse_member( context& ctx, object& parent )
		{
			const auto [ datatype, flag ] = read_type( ctx );
			( void ) flag;

			if ( parent.is_array( ) )
			{
				std::get<array>( parent.m_value ).push_back( read_value( ctx, datatype ) );
				return;
			}

			const auto string_id = ctx.buffer->bytes4.read_pod<std::int32_t>( );
			auto name = string_id == -1 ? std::string{} : ctx.strings.at( static_cast< std::size_t >( string_id ) );
			std::get<dict>( parent.m_value ).emplace( std::move( name ), read_value( ctx, datatype ) );
		}

		object read_value( context& ctx, std::uint8_t raw_type )
		{
			const auto type = static_cast< node_type >( raw_type );
			auto& buf = *ctx.buffer;

			switch ( type )
			{
			case node_type::null_value:      return object{};
			case node_type::boolean_true:    return object{ true };
			case node_type::boolean_false:   return object{ false };
			case node_type::int64_zero:      return object{ std::int64_t{ 0 } };
			case node_type::int64_one:       return object{ std::int64_t{ 1 } };
			case node_type::double_zero:     return object{ 0.0 };
			case node_type::double_one:      return object{ 1.0 };

			case node_type::boolean:
				return object{ buf.bytes1.read( 1 )[ 0 ] == 1 };
			case node_type::int32_as_byte:
				return object{ static_cast< std::int64_t >( buf.bytes1.read( 1 )[ 0 ] ) };

			case node_type::int16:
				return object{ static_cast< std::int64_t >( buf.bytes2.read_pod<std::int16_t>( ) ) };
			case node_type::uint16:
				return object{ static_cast< std::uint64_t >( buf.bytes2.read_pod<std::uint16_t>( ) ) };

			case node_type::int32:
				return object{ static_cast< std::int64_t >( buf.bytes4.read_pod<std::int32_t>( ) ) };
			case node_type::uint32:
				return object{ static_cast< std::uint64_t >( buf.bytes4.read_pod<std::uint32_t>( ) ) };
			case node_type::float_value:
				return object{ static_cast< double >( buf.bytes4.read_pod<float>( ) ) };

			case node_type::int64:
				return object{ buf.bytes8.read_pod<std::int64_t>( ) };
			case node_type::uint64:
				return object{ buf.bytes8.read_pod<std::uint64_t>( ) };
			case node_type::double_value:
				return object{ buf.bytes8.read_pod<double>( ) };

			case node_type::string:
			{
				const auto id = buf.bytes4.read_pod<std::int32_t>( );
				return object{ id == -1 ? std::string{} : ctx.strings.at( static_cast< std::size_t >( id ) ) };
			}

			case node_type::binary_blob:
			{
				if ( ctx.version < 2 )
				{
					const auto length = buf.bytes4.read_pod<std::int32_t>( );
					if ( length <= 0 ) return object{ std::vector<std::uint8_t>{} };
					const auto* p = buf.bytes1.read( static_cast< std::size_t >( length ) );
					return object{ std::vector<std::uint8_t>( p, p + length ) };
				}

				const auto length = ctx.binary_blob_lengths.read_pod<std::int32_t>( );
				if ( length <= 0 ) return object{ std::vector<std::uint8_t>{} };
				const auto* p = ctx.binary_blobs.read( static_cast< std::size_t >( length ) );
				return object{ std::vector<std::uint8_t>( p, p + length ) };
			}

			case node_type::array_value:
			{
				const auto length = buf.bytes4.read_pod<std::int32_t>( );
				object result{ array{} };
				auto& arr = std::get<array>( result.m_value );
				arr.reserve( static_cast< std::size_t >( std::max( length, 0 ) ) );
				for ( std::int32_t i = 0; i < length; ++i )
				{
					parse_member( ctx, result );
				}
				return result;
			}

			case node_type::array_typed:
			case node_type::array_type_byte_length:
			{
				const std::int32_t length = type == node_type::array_type_byte_length
					? static_cast< std::int32_t >( buf.bytes1.read( 1 )[ 0 ] )
					: buf.bytes4.read_pod<std::int32_t>( );

				const auto [ sub_type, sub_flag ] = read_type( ctx );
				( void ) sub_flag;

				object result{ array{} };
				auto& arr = std::get<array>( result.m_value );
				arr.reserve( static_cast< std::size_t >( std::max( length, 0 ) ) );
				for ( std::int32_t i = 0; i < length; ++i )
				{
					arr.push_back( read_value( ctx, sub_type ) );
				}
				return result;
			}

			case node_type::array_type_auxiliary_buffer:
			{
				const auto length = buf.bytes1.read( 1 )[ 0 ];
				const auto [ sub_type, sub_flag ] = read_type( ctx );
				( void ) sub_flag;

				std::swap( ctx.buffer, ctx.auxiliary );
				object result{ array{} };
				auto& arr = std::get<array>( result.m_value );
				arr.reserve( length );
				for ( int i = 0; i < length; ++i )
				{
					arr.push_back( read_value( ctx, sub_type ) );
				}
				std::swap( ctx.buffer, ctx.auxiliary );
				return result;
			}

			case node_type::object_value:
			{
				const auto length = ctx.version >= 5
					? ctx.object_lengths.read_pod<std::int32_t>( )
					: buf.bytes4.read_pod<std::int32_t>( );

				object result{ dict{} };
				for ( std::int32_t i = 0; i < length; ++i )
				{
					parse_member( ctx, result );
				}
				return result;
			}

			default:
				throw std::runtime_error( "kv3: unknown node type " + std::to_string( raw_type ) );
			}
		}

	}

	const object* object::find( const std::string& key ) const
	{
		const auto* d = std::get_if<dict>( &m_value );
		if ( !d )
		{
			return nullptr;
		}

		const auto it = d->find( key );
		return it == d->end( ) ? nullptr : &it->second;
	}

	const object* object::at( std::size_t index ) const
	{
		const auto* a = std::get_if<array>( &m_value );
		if ( !a || index >= a->size( ) )
		{
			return nullptr;
		}

		return &( *a )[ index ];
	}

	std::size_t object::size( ) const
	{
		if ( const auto* a = std::get_if<array>( &m_value ) ) return a->size( );
		if ( const auto* d = std::get_if<dict>( &m_value ) ) return d->size( );
		return 0;
	}

	std::string object::as_string( ) const
	{
		if ( const auto* s = std::get_if<std::string>( &m_value ) ) return *s;
		return {};
	}

	double object::as_double( ) const
	{
		if ( const auto* v = std::get_if<double>( &m_value ) ) return *v;
		if ( const auto* v = std::get_if<std::int64_t>( &m_value ) ) return static_cast< double >( *v );
		if ( const auto* v = std::get_if<std::uint64_t>( &m_value ) ) return static_cast< double >( *v );
		return 0.0;
	}

	std::int64_t object::as_int( ) const
	{
		if ( const auto* v = std::get_if<std::int64_t>( &m_value ) ) return *v;
		if ( const auto* v = std::get_if<std::uint64_t>( &m_value ) ) return static_cast< std::int64_t >( *v );
		if ( const auto* v = std::get_if<double>( &m_value ) ) return static_cast< std::int64_t >( *v );
		return 0;
	}

	bool object::as_bool( ) const
	{
		if ( const auto* v = std::get_if<bool>( &m_value ) ) return *v;
		return false;
	}

	bool object::as_float3( float out[ 3 ] ) const
	{
		const auto* a = std::get_if<array>( &m_value );
		if ( !a || a->size( ) != 3 )
		{
			return false;
		}

		for ( int i = 0; i < 3; ++i ) out[ i ] = static_cast< float >( ( *a )[ i ].as_double( ) );
		return true;
	}

	bool object::as_float4( float out[ 4 ] ) const
	{
		const auto* a = std::get_if<array>( &m_value );
		if ( !a || a->size( ) != 4 )
		{
			return false;
		}

		for ( int i = 0; i < 4; ++i ) out[ i ] = static_cast< float >( ( *a )[ i ].as_double( ) );
		return true;
	}

	document decode( const std::uint8_t* data, std::size_t size )
	{
		if ( size < 4 )
		{
			throw std::runtime_error( "kv3: block too small" );
		}

		std::uint32_t magic{};
		std::memcpy( &magic, data, 4 );

		if ( magic == k_magic0 )
		{
			throw std::runtime_error( "kv3: legacy text-wrapped KV3 (MAGIC0) not needed for compiled CS2 resources" );
		}

		const auto version = static_cast< int >( magic & 0xFF );
		if ( ( magic & 0xFFFFFF00 ) != k_kv3_prefix )
		{
			throw std::runtime_error( "kv3: not a KV3 block" );
		}

		if ( version < 1 || version > 5 )
		{
			throw std::runtime_error( "kv3: unsupported version " + std::to_string( version ) );
		}

		if ( version != 4 && version != 5 )
		{
			throw std::runtime_error( "kv3: only v4/v5 are implemented (got v" + std::to_string( version ) + ")" );
		}

		context ctx{};
		ctx.version = version;

		std::size_t p = 4;
		const auto read_u32 = [ & ]( ) { std::uint32_t v{}; std::memcpy( &v, data + p, 4 ); p += 4; return v; };
		const auto read_u16 = [ & ]( ) { std::uint16_t v{}; std::memcpy( &v, data + p, 2 ); p += 2; return v; };
		const auto read_i32 = [ & ]( ) { std::int32_t v{}; std::memcpy( &v, data + p, 4 ); p += 4; return v; };

		document doc{};
		std::memcpy( doc.format_guid.data( ), data + p, 16 );
		p += 16;

		const auto compression_method = read_u32( );
		const auto compression_dict_id = read_u16( );
		const auto compression_frame_size = read_u16( );
		const auto count_bytes1 = read_i32( );
		const auto count_bytes4 = read_i32( );
		const auto count_bytes8 = read_i32( );
		const auto count_types = read_i32( );
		const auto count_objects = read_u16( );
		const auto count_arrays = read_u16( );
		( void ) count_objects; ( void ) count_arrays; ( void ) compression_dict_id;
		const auto size_uncompressed_total = read_i32( );
		( void ) size_uncompressed_total;

		const auto size_compressed_total = static_cast< std::size_t >( read_i32( ) );
		const auto count_blocks = read_i32( );
		const auto size_binary_blobs_bytes = static_cast< std::uint32_t >( read_i32( ) );

		const auto count_bytes2 = read_i32( );
		read_i32( );

		std::size_t size_u1{}, size_c1{}, size_u2{}, size_c2{};
		std::int32_t cb1_2{}, cb2_2{}, cb4_2{}, cb8_2{}, count_objects_2{};
		if ( version >= 5 )
		{
			size_u1 = static_cast< std::size_t >( read_i32( ) );
			size_c1 = static_cast< std::size_t >( read_i32( ) );
			size_u2 = static_cast< std::size_t >( read_i32( ) );
			size_c2 = static_cast< std::size_t >( read_i32( ) );
			cb1_2 = read_i32( );
			cb2_2 = read_i32( );
			cb4_2 = read_i32( );
			cb8_2 = read_i32( );
			read_i32( );
			count_objects_2 = read_i32( );
			read_i32( );
			read_i32( );
		}

		if ( compression_method != 0 && compression_method != 1 && compression_method != 2 )
		{
			throw std::runtime_error( "kv3: unknown compression method " + std::to_string( compression_method ) );
		}

		const auto decode_buffer = [ & ]( std::size_t compressed_size, std::size_t uncompressed_size )
		{
			std::vector<std::uint8_t> out{};
			if ( compression_method == 0 )
			{
				out.assign( data + p, data + p + uncompressed_size );
				p += uncompressed_size;
			}
			else if ( compression_method == 1 )
			{
				out = lz4_block_decode( data + p, compressed_size, uncompressed_size );
				p += compressed_size;
			}
			else
			{
				out = zstd_decode( data + p, compressed_size, uncompressed_size );
				p += compressed_size;
			}
			return out;
		};

		if ( count_bytes4 <= 0 )
		{
			throw std::runtime_error( "kv3: countBytes4 must be > 0 (string count)" );
		}

		std::vector<std::uint8_t> buf1_storage{};
		std::vector<std::uint8_t> buf2_storage{};
		std::vector<std::uint8_t> single_storage{};
		std::vector<std::uint8_t> blob_storage{};
		std::vector<std::uint8_t> blob_lengths_storage{};
		cursor blob_table{};
		buffers buffer1{};
		buffers buffer2{};
		buffers buffer_single{};

		if ( version >= 5 )
		{

			buf1_storage = decode_buffer( size_c1, size_u1 );
			{
				std::size_t off = 0;
				if ( count_bytes1 > 0 )
				{
					buffer1.bytes1 = cursor( buf1_storage.data( ), off, off + static_cast< std::size_t >( count_bytes1 ) );
					off += static_cast< std::size_t >( count_bytes1 );
				}
				if ( count_bytes2 > 0 )
				{
					off = align_up( off, 2 );
					buffer1.bytes2 = cursor( buf1_storage.data( ), off, off + static_cast< std::size_t >( count_bytes2 ) * 2 );
					off += static_cast< std::size_t >( count_bytes2 ) * 2;
				}
				off = align_up( off, 4 );
				buffer1.bytes4 = cursor( buf1_storage.data( ), off, off + static_cast< std::size_t >( count_bytes4 ) * 4 );
				off += static_cast< std::size_t >( count_bytes4 ) * 4;
				if ( count_bytes8 > 0 )
				{
					off = align_up( off, 8 );
					buffer1.bytes8 = cursor( buf1_storage.data( ), off, off + static_cast< std::size_t >( count_bytes8 ) * 8 );
				}
			}

			const auto string_count = buffer1.bytes4.read_pod<std::int32_t>( );
			ctx.strings.resize( static_cast< std::size_t >( std::max( string_count, 0 ) ) );
			for ( std::int32_t i = 0; i < string_count; ++i )
			{
				ctx.strings[ static_cast< std::size_t >( i ) ] = read_null_term_utf8( buffer1.bytes1 );
			}

			buf2_storage = decode_buffer( size_c2, size_u2 );

			ctx.buffer = &buffer2;
			ctx.auxiliary = &buffer1;

			std::size_t off = static_cast< std::size_t >( count_objects_2 ) * 4;
			ctx.object_lengths = cursor( buf2_storage.data( ), 0, off );

			if ( cb1_2 > 0 ) { buffer2.bytes1 = cursor( buf2_storage.data( ), off, off + static_cast< std::size_t >( cb1_2 ) ); off += static_cast< std::size_t >( cb1_2 ); }
			if ( cb2_2 > 0 ) { off = align_up( off, 2 ); buffer2.bytes2 = cursor( buf2_storage.data( ), off, off + static_cast< std::size_t >( cb2_2 ) * 2 ); off += static_cast< std::size_t >( cb2_2 ) * 2; }
			if ( cb4_2 > 0 ) { off = align_up( off, 4 ); buffer2.bytes4 = cursor( buf2_storage.data( ), off, off + static_cast< std::size_t >( cb4_2 ) * 4 ); off += static_cast< std::size_t >( cb4_2 ) * 4; }
			if ( cb8_2 > 0 ) { off = align_up( off, 8 ); buffer2.bytes8 = cursor( buf2_storage.data( ), off, off + static_cast< std::size_t >( cb8_2 ) * 8 ); off += static_cast< std::size_t >( cb8_2 ) * 8; }

			ctx.types = cursor( buf2_storage.data( ), off, off + static_cast< std::size_t >( count_types ) );
			off += static_cast< std::size_t >( count_types );

			if ( count_blocks == 0 )
			{
				std::uint32_t trailer{};
				std::memcpy( &trailer, buf2_storage.data( ) + off, 4 );
				if ( trailer != 0xFFEEDD00 )
				{
					throw std::runtime_error( "kv3: missing v5 trailer after types stream" );
				}
			}
			else
			{

				blob_table = cursor( buf2_storage.data( ), off, buf2_storage.size( ) );
			}
		}
		else
		{

			single_storage = decode_buffer( size_compressed_total, static_cast< std::size_t >( size_uncompressed_total ) );

			ctx.buffer = &buffer_single;
			ctx.auxiliary = &buffer_single;

			std::size_t off = 0;
			if ( count_bytes1 > 0 )
			{
				buffer_single.bytes1 = cursor( single_storage.data( ), off, off + static_cast< std::size_t >( count_bytes1 ) );
				off += static_cast< std::size_t >( count_bytes1 );
			}
			if ( count_bytes2 > 0 )
			{
				off = align_up( off, 2 );
				buffer_single.bytes2 = cursor( single_storage.data( ), off, off + static_cast< std::size_t >( count_bytes2 ) * 2 );
				off += static_cast< std::size_t >( count_bytes2 ) * 2;
			}
			off = align_up( off, 4 );
			buffer_single.bytes4 = cursor( single_storage.data( ), off, off + static_cast< std::size_t >( count_bytes4 ) * 4 );
			off += static_cast< std::size_t >( count_bytes4 ) * 4;
			if ( count_bytes8 > 0 )
			{
				off = align_up( off, 8 );
				buffer_single.bytes8 = cursor( single_storage.data( ), off, off + static_cast< std::size_t >( count_bytes8 ) * 8 );
				off += static_cast< std::size_t >( count_bytes8 ) * 8;
			}

			const std::size_t region_start = off;
			const std::size_t region_end = region_start + static_cast< std::size_t >( count_types );
			const auto string_count = buffer_single.bytes4.read_pod<std::int32_t>( );
			ctx.strings.resize( static_cast< std::size_t >( std::max( string_count, 0 ) ) );
			cursor region( single_storage.data( ), region_start, region_end );
			for ( std::int32_t i = 0; i < string_count; ++i )
			{
				ctx.strings[ static_cast< std::size_t >( i ) ] = read_null_term_utf8( region );
			}
			ctx.types = cursor( single_storage.data( ), region.pos( ), region_end );

			if ( count_blocks == 0 )
			{
				std::uint32_t trailer{};
				std::memcpy( &trailer, single_storage.data( ) + region_end, 4 );
				if ( trailer != 0xFFEEDD00 )
				{
					throw std::runtime_error( "kv3: missing v4 trailer after types stream" );
				}
			}
			else
			{

				blob_table = cursor( single_storage.data( ), region_end, single_storage.size( ) );
			}
		}

		if ( count_blocks > 0 )
		{
			const auto* lengths = blob_table.read( static_cast< std::size_t >( count_blocks ) * 4 );
			blob_lengths_storage.assign( lengths, lengths + static_cast< std::size_t >( count_blocks ) * 4 );
			ctx.binary_blob_lengths = cursor( blob_lengths_storage.data( ), 0, blob_lengths_storage.size( ) );

			std::uint32_t trailer{};
			std::memcpy( &trailer, blob_table.read( 4 ), 4 );
			if ( trailer != 0xFFEEDD00 )
			{
				throw std::runtime_error( "kv3: missing trailer before binary blobs" );
			}

			if ( compression_method == 0 )
			{
				blob_storage.assign( data + p, data + p + size_binary_blobs_bytes );
				p += size_binary_blobs_bytes;
			}
			else if ( compression_method == 1 )
			{

				std::vector<std::uint16_t> chunk_sizes{};
				while ( blob_table.remaining( ) >= 2 )
				{
					std::uint16_t n{};
					std::memcpy( &n, blob_table.read( 2 ), 2 );
					chunk_sizes.push_back( n );
				}

				std::size_t total_compressed{};
				for ( const auto n : chunk_sizes ) total_compressed += n;

				cursor reader( data, p, p + total_compressed );
				p += total_compressed;

				const auto* blob_lengths = reinterpret_cast< const std::int32_t* >( blob_lengths_storage.data( ) );
				blob_storage = lz4_chain_decode( reader, chunk_sizes, blob_lengths,
					static_cast< std::size_t >( count_blocks ), compression_frame_size, size_binary_blobs_bytes );
			}
			else
			{

				const auto size_compressed_blobs = version >= 5
					? size_compressed_total - size_c1 - size_c2
					: ( size - 4 ) - p;
				blob_storage = zstd_decode( data + p, size_compressed_blobs, size_binary_blobs_bytes );
				p += size_compressed_blobs;
			}

			ctx.binary_blobs = cursor( blob_storage.data( ), 0, blob_storage.size( ) );

			std::uint32_t trailer2{};
			std::memcpy( &trailer2, data + p, 4 );
			p += 4;
			if ( trailer2 != 0xFFEEDD00 )
			{
				throw std::runtime_error( "kv3: missing trailer after binary blobs" );
			}
		}

		const auto [ root_type, root_flag ] = read_type( ctx );
		( void ) root_flag;
		doc.root = read_value( ctx, root_type );

		return doc;
	}

}
