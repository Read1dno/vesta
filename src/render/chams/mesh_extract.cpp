#include <stdafx.hpp>
#include <render/chams/mesh_extract.hpp>
#include <core/assets/resource.hpp>
#include <core/assets/kv3.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace chams {

	namespace {

		[[nodiscard]] bool contains_ci( const std::string& haystack, const char* needle )
		{
			auto it = std::search( haystack.begin( ), haystack.end( ),
				needle, needle + std::strlen( needle ),
				[ ]( char a, char b ) { return std::tolower( static_cast< unsigned char >( a ) ) == std::tolower( static_cast< unsigned char >( b ) ); } );
			return it != haystack.end( );
		}

		void quat_to_rotation( const float q[ 4 ], float out[ 3 ][ 3 ] )
		{
			const auto x = q[ 0 ], y = q[ 1 ], z = q[ 2 ], w = q[ 3 ];
			const auto xx = x * x, yy = y * y, zz = z * z;
			const auto xy = x * y, xz = x * z, yz = y * z;
			const auto wx = w * x, wy = w * y, wz = w * z;

			out[ 0 ][ 0 ] = 1.0f - 2.0f * ( yy + zz ); out[ 0 ][ 1 ] = 2.0f * ( xy - wz );       out[ 0 ][ 2 ] = 2.0f * ( xz + wy );
			out[ 1 ][ 0 ] = 2.0f * ( xy + wz );       out[ 1 ][ 1 ] = 1.0f - 2.0f * ( xx + zz ); out[ 1 ][ 2 ] = 2.0f * ( yz - wx );
			out[ 2 ][ 0 ] = 2.0f * ( xz - wy );       out[ 2 ][ 1 ] = 2.0f * ( yz + wx );       out[ 2 ][ 2 ] = 1.0f - 2.0f * ( xx + yy );
		}

		[[nodiscard]] bone_matrix compose_local( const float pos[ 3 ], const float rot[ 4 ], float scale )
		{
			float r[ 3 ][ 3 ]{};
			quat_to_rotation( rot, r );

			bone_matrix out{};
			for ( int row = 0; row < 3; ++row )
			{
				for ( int col = 0; col < 3; ++col )
				{
					out.m[ row ][ col ] = r[ row ][ col ] * scale;
				}
				out.m[ row ][ 3 ] = pos[ row ];
			}
			return out;
		}

		[[nodiscard]] bool build_skeleton( const kv3::object& data_root, std::vector<bone_info>& out )
		{
			const auto* skel = data_root.find( "m_modelSkeleton" );
			if ( !skel )
			{
				return false;
			}

			const auto* names = skel->find( "m_boneName" );
			const auto* parents = skel->find( "m_nParent" );
			const auto* pos = skel->find( "m_bonePosParent" );
			const auto* rot = skel->find( "m_boneRotParent" );
			const auto* scale = skel->find( "m_boneScaleParent" );

			if ( !names || !parents || !pos || !rot || !scale )
			{
				return false;
			}

			const auto count = names->size( );
			out.resize( count );

			std::vector<bone_matrix> bind_pose( count );

			for ( std::size_t i = 0; i < count; ++i )
			{
				const auto parent = static_cast< int >( parents->at( i )->as_int( ) );

				if ( parent >= static_cast< int >( i ) )
				{
					return false;
				}

				float p[ 3 ]{}, q[ 4 ]{};
				if ( !pos->at( i )->as_float3( p ) || !rot->at( i )->as_float4( q ) )
				{
					return false;
				}

				const auto local = compose_local( p, q, static_cast< float >( scale->at( i )->as_double( ) ) );
				bind_pose[ i ] = parent < 0 ? local : bind_pose[ static_cast< std::size_t >( parent ) ] * local;

				out[ i ].name = names->at( i )->as_string( );
				out[ i ].parent = parent;
				out[ i ].inverse_bind = bind_pose[ i ].inverse_rigid( );
			}

			return true;
		}

		struct buffer_desc
		{
			int block_index{ -1 };
			std::uint32_t count{};
			std::uint32_t stride{};
			bool meshopt{};

			int position_offset{ -1 };
			int blend_indices_offset{ -1 };
			int blend_weights_offset{ -1 };
			int uv_offset{ -1 };
			int uv_format{ -1 };
		};

		constexpr int k_format_r16g16_float{ 34 };
		constexpr int k_format_r16g16_snorm{ 37 };
		constexpr int k_format_r32g32_float{ 16 };

		[[nodiscard]] float half_to_float( std::uint16_t h )
		{
			const std::uint32_t sign = static_cast< std::uint32_t >( h & 0x8000 ) << 16;
			const std::uint32_t exponent = ( h >> 10 ) & 0x1F;
			const std::uint32_t mantissa = h & 0x3FF;

			std::uint32_t bits{};
			if ( exponent == 0 )
			{

				if ( mantissa != 0 )
				{
					auto e = -1;
					auto m = mantissa;
					do { ++e; m <<= 1; } while ( ( m & 0x400 ) == 0 );
					bits = sign | ( static_cast< std::uint32_t >( 127 - 15 - e ) << 23 ) | ( ( m & 0x3FF ) << 13 );
				}
				else
				{
					bits = sign;
				}
			}
			else if ( exponent == 0x1F )
			{
				bits = sign | 0x7F800000u | ( mantissa << 13 );
			}
			else
			{
				bits = sign | ( ( exponent + ( 127 - 15 ) ) << 23 ) | ( mantissa << 13 );
			}

			float out{};
			std::memcpy( &out, &bits, sizeof( out ) );
			return out;
		}

		[[nodiscard]] bool semantic_is( std::string_view name, std::string_view wanted )
		{

			if ( name.size( ) != wanted.size( ) )
			{
				return false;
			}

			for ( std::size_t i = 0; i < name.size( ); ++i )
			{
				const auto a = static_cast< char >( std::tolower( static_cast< unsigned char >( name[ i ] ) ) );
				const auto b = static_cast< char >( std::tolower( static_cast< unsigned char >( wanted[ i ] ) ) );
				if ( a != b ) return false;
			}

			return true;
		}

		[[nodiscard]] buffer_desc read_buffer_desc( const kv3::object& obj )
		{
			buffer_desc d{};
			d.block_index = static_cast< int >( obj.find( "m_nBlockIndex" )->as_int( ) );
			d.count = static_cast< std::uint32_t >( obj.find( "m_nElementCount" )->as_int( ) );
			d.stride = static_cast< std::uint32_t >( obj.find( "m_nElementSizeInBytes" )->as_int( ) );
			d.meshopt = obj.find( "m_bMeshoptCompressed" )->as_bool( );

			if ( const auto* fields = obj.find( "m_inputLayoutFields" ) )
			{
				for ( std::size_t i = 0; i < fields->size( ); ++i )
				{
					const auto* field = fields->at( i );
					const auto* name_kv = field ? field->find( "m_pSemanticName" ) : nullptr;
					const auto* offset_kv = field ? field->find( "m_nOffset" ) : nullptr;
					if ( !name_kv || !offset_kv )
					{
						continue;
					}

					auto name = name_kv->as_string( );
					if ( const auto nul = name.find( '\0' ); nul != std::string::npos )
					{
						name.resize( nul );
					}

					const auto offset = static_cast< int >( offset_kv->as_int( ) );
					const auto* format_kv = field->find( "m_Format" );
					const auto* index_kv = field->find( "m_nSemanticIndex" );
					const auto semantic_index = index_kv ? static_cast< int >( index_kv->as_int( ) ) : 0;

					if ( semantic_is( name, "POSITION" ) ) d.position_offset = offset;
					else if ( semantic_is( name, "BLENDINDICES" ) ) d.blend_indices_offset = offset;
					else if ( semantic_is( name, "BLENDWEIGHT" ) ) d.blend_weights_offset = offset;
					else if ( semantic_is( name, "TEXCOORD" ) && semantic_index == 0 )
					{

						d.uv_offset = offset;
						d.uv_format = format_kv ? static_cast< int >( format_kv->as_int( ) ) : -1;
					}
				}
			}

			return d;
		}

		[[nodiscard]] bool decode_vertex_buffer( const resource& res, const buffer_desc& desc,
			std::span<const std::uint32_t> bone_remap, std::vector<skinned_vertex>& out )
		{
			if ( desc.block_index < 0 || static_cast< std::size_t >( desc.block_index ) >= res.blocks( ).size( ) )
			{
				return false;
			}

			const auto& block = res.blocks( )[ static_cast< std::size_t >( desc.block_index ) ];
			const auto* src = res.bytes( block );

			std::vector<std::uint8_t> raw( static_cast< std::size_t >( desc.count ) * desc.stride );

			if ( desc.meshopt )
			{
				const auto rc = meshopt_decodeVertexBuffer( raw.data( ), desc.count, desc.stride, src, block.size );
				if ( rc != 0 )
				{
					return false;
				}
			}
			else
			{
				if ( block.size < raw.size( ) )
				{
					return false;
				}
				std::memcpy( raw.data( ), src, raw.size( ) );
			}

			const auto fits = [ & ]( int offset, std::uint32_t size )
				{
					return offset >= 0 && static_cast< std::uint32_t >( offset ) + size <= desc.stride;
				};

			if ( !fits( desc.position_offset, 12 ) || !fits( desc.blend_indices_offset, 4 ) ||
				!fits( desc.blend_weights_offset, 4 ) )
			{
				return false;
			}

			const auto uv_size = desc.uv_format == k_format_r32g32_float ? 8u : 4u;
			const auto has_uv = desc.uv_format >= 0 && fits( desc.uv_offset, uv_size );

			out.resize( desc.count );
			for ( std::uint32_t i = 0; i < desc.count; ++i )
			{
				const auto* v = raw.data( ) + static_cast< std::size_t >( i ) * desc.stride;
				auto& sv = out[ i ];
				std::memcpy( sv.position, v + desc.position_offset, 12 );
				std::memcpy( sv.bone_indices, v + desc.blend_indices_offset, 4 );
				std::memcpy( sv.bone_weights, v + desc.blend_weights_offset, 4 );

				if ( has_uv )
				{
					const auto* uv = v + desc.uv_offset;
					switch ( desc.uv_format )
					{
					case k_format_r16g16_float:
					{
						std::uint16_t raw16[ 2 ]{};
						std::memcpy( raw16, uv, 4 );
						sv.uv[ 0 ] = half_to_float( raw16[ 0 ] );
						sv.uv[ 1 ] = half_to_float( raw16[ 1 ] );
						break;
					}
					case k_format_r16g16_snorm:
					{
						std::int16_t raw16[ 2 ]{};
						std::memcpy( raw16, uv, 4 );
						sv.uv[ 0 ] = std::max( raw16[ 0 ] / 32767.0f, -1.0f );
						sv.uv[ 1 ] = std::max( raw16[ 1 ] / 32767.0f, -1.0f );
						break;
					}
					case k_format_r32g32_float:
						std::memcpy( sv.uv, uv, 8 );
						break;
					default:
						break;
					}
				}

				for ( auto& index : sv.bone_indices )
				{
					if ( index >= bone_remap.size( ) )
					{
						return false;
					}

					index = static_cast< std::uint8_t >( bone_remap[ index ] );
				}
			}

			return true;
		}

		[[nodiscard]] bool decode_index_buffer( const resource& res, const buffer_desc& desc, std::vector<std::uint32_t>& out )
		{
			if ( desc.block_index < 0 || static_cast< std::size_t >( desc.block_index ) >= res.blocks( ).size( ) )
			{
				return false;
			}

			if ( desc.stride != 2 && desc.stride != 4 )
			{
				return false;
			}

			const auto& block = res.blocks( )[ static_cast< std::size_t >( desc.block_index ) ];
			const auto* src = res.bytes( block );

			out.resize( desc.count );

			if ( desc.stride == 2 )
			{
				std::vector<std::uint16_t> raw16( desc.count );
				if ( desc.meshopt )
				{
					if ( meshopt_decodeIndexBuffer( raw16.data( ), desc.count, 2, src, block.size ) != 0 ) return false;
				}
				else
				{
					if ( block.size < raw16.size( ) * 2 ) return false;
					std::memcpy( raw16.data( ), src, raw16.size( ) * 2 );
				}
				for ( std::uint32_t i = 0; i < desc.count; ++i ) out[ i ] = raw16[ i ];
			}
			else
			{
				if ( desc.meshopt )
				{
					if ( meshopt_decodeIndexBuffer( out.data( ), desc.count, 4, src, block.size ) != 0 ) return false;
				}
				else
				{
					if ( block.size < out.size( ) * 4 ) return false;
					std::memcpy( out.data( ), src, out.size( ) * 4 );
				}
			}

			return true;
		}

		void compute_normals( skinned_mesh& mesh )
		{
			for ( auto& v : mesh.vertices )
			{
				v.normal[ 0 ] = v.normal[ 1 ] = v.normal[ 2 ] = 0.0f;
				v.tangent[ 0 ] = v.tangent[ 1 ] = v.tangent[ 2 ] = v.tangent[ 3 ] = 0.0f;
			}

			std::vector<float> tangent_accum( mesh.vertices.size( ) * 3, 0.0f );

			const auto vertex_count = mesh.vertices.size( );
			for ( std::size_t i = 0; i + 2 < mesh.indices.size( ); i += 3 )
			{
				const std::size_t idx[ 3 ]{ mesh.indices[ i ], mesh.indices[ i + 1 ], mesh.indices[ i + 2 ] };
				if ( idx[ 0 ] >= vertex_count || idx[ 1 ] >= vertex_count || idx[ 2 ] >= vertex_count )
				{
					continue;
				}

				const auto* a = mesh.vertices[ idx[ 0 ] ].position;
				const auto* b = mesh.vertices[ idx[ 1 ] ].position;
				const auto* c = mesh.vertices[ idx[ 2 ] ].position;

				const float u[ 3 ]{ b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ], b[ 2 ] - a[ 2 ] };
				const float v[ 3 ]{ c[ 0 ] - a[ 0 ], c[ 1 ] - a[ 1 ], c[ 2 ] - a[ 2 ] };

				const float n[ 3 ]{
					u[ 1 ] * v[ 2 ] - u[ 2 ] * v[ 1 ],
					u[ 2 ] * v[ 0 ] - u[ 0 ] * v[ 2 ],
					u[ 0 ] * v[ 1 ] - u[ 1 ] * v[ 0 ] };

				for ( const auto vi : idx )
				{
					mesh.vertices[ vi ].normal[ 0 ] += n[ 0 ];
					mesh.vertices[ vi ].normal[ 1 ] += n[ 1 ];
					mesh.vertices[ vi ].normal[ 2 ] += n[ 2 ];
				}

				const auto* uv_a = mesh.vertices[ idx[ 0 ] ].uv;
				const auto* uv_b = mesh.vertices[ idx[ 1 ] ].uv;
				const auto* uv_c = mesh.vertices[ idx[ 2 ] ].uv;
				const float du1 = uv_b[ 0 ] - uv_a[ 0 ], dv1 = uv_b[ 1 ] - uv_a[ 1 ];
				const float du2 = uv_c[ 0 ] - uv_a[ 0 ], dv2 = uv_c[ 1 ] - uv_a[ 1 ];
				const auto det = du1 * dv2 - du2 * dv1;

				if ( std::abs( det ) > 1e-12f )
				{
					const auto inv = 1.0f / det;
					const float t[ 3 ]{
						( u[ 0 ] * dv2 - v[ 0 ] * dv1 ) * inv,
						( u[ 1 ] * dv2 - v[ 1 ] * dv1 ) * inv,
						( u[ 2 ] * dv2 - v[ 2 ] * dv1 ) * inv };

					for ( const auto vi : idx )
					{
						tangent_accum[ vi * 3 + 0 ] += t[ 0 ];
						tangent_accum[ vi * 3 + 1 ] += t[ 1 ];
						tangent_accum[ vi * 3 + 2 ] += t[ 2 ];
					}
				}
			}

			for ( auto& vert : mesh.vertices )
			{
				auto* n = vert.normal;
				const auto length = std::sqrt( n[ 0 ] * n[ 0 ] + n[ 1 ] * n[ 1 ] + n[ 2 ] * n[ 2 ] );
				if ( length > 1e-12f )
				{
					n[ 0 ] /= length;
					n[ 1 ] /= length;
					n[ 2 ] /= length;
				}
				else
				{

					n[ 0 ] = 0.0f; n[ 1 ] = 0.0f; n[ 2 ] = 1.0f;
				}

				const auto index = static_cast< std::size_t >( &vert - mesh.vertices.data( ) );
				float t[ 3 ]{
					tangent_accum[ index * 3 + 0 ],
					tangent_accum[ index * 3 + 1 ],
					tangent_accum[ index * 3 + 2 ] };

				const auto dot = t[ 0 ] * n[ 0 ] + t[ 1 ] * n[ 1 ] + t[ 2 ] * n[ 2 ];
				t[ 0 ] -= n[ 0 ] * dot;
				t[ 1 ] -= n[ 1 ] * dot;
				t[ 2 ] -= n[ 2 ] * dot;

				const auto t_len = std::sqrt( t[ 0 ] * t[ 0 ] + t[ 1 ] * t[ 1 ] + t[ 2 ] * t[ 2 ] );
				if ( t_len > 1e-12f )
				{
					vert.tangent[ 0 ] = t[ 0 ] / t_len;
					vert.tangent[ 1 ] = t[ 1 ] / t_len;
					vert.tangent[ 2 ] = t[ 2 ] / t_len;
				}
				else
				{

					const float axis[ 3 ]{ std::abs( n[ 0 ] ) < 0.9f ? 1.0f : 0.0f, std::abs( n[ 0 ] ) < 0.9f ? 0.0f : 1.0f, 0.0f };
					vert.tangent[ 0 ] = axis[ 1 ] * n[ 2 ] - axis[ 2 ] * n[ 1 ];
					vert.tangent[ 1 ] = axis[ 2 ] * n[ 0 ] - axis[ 0 ] * n[ 2 ];
					vert.tangent[ 2 ] = axis[ 0 ] * n[ 1 ] - axis[ 1 ] * n[ 0 ];
				}

				vert.tangent[ 3 ] = 1.0f;
			}
		}

	}

	skinned_mesh extract_mesh( vpk_archive& vpk, const std::string& model_path )
	{
		skinned_mesh result{};

		try
		{
			const auto* entry = vpk.find( model_path );
			if ( !entry )
			{
				return result;
			}

			const auto file_data = vpk.read( *entry );

			resource res{};
			if ( !res.parse( file_data ) )
			{
				return result;
			}

			const auto* data_block = res.find( "DATA" );
			const auto* ctrl_block = res.find( "CTRL" );
			if ( !data_block || !ctrl_block )
			{
				return result;
			}

			const auto data_doc = kv3::decode( res.bytes( *data_block ), data_block->size );
			if ( !build_skeleton( data_doc.root, result.bones ) )
			{
				return result;
			}

			const auto* remap_kv = data_doc.root.find( "m_remappingTable" );
			const auto* remap_starts_kv = data_doc.root.find( "m_remappingTableStarts" );
			if ( !remap_kv || !remap_starts_kv )
			{
				return result;
			}

			std::vector<std::uint32_t> remap_table( remap_kv->size( ) );
			for ( std::size_t i = 0; i < remap_kv->size( ); ++i )
			{
				remap_table[ i ] = static_cast< std::uint32_t >( remap_kv->at( i )->as_int( ) );
				if ( remap_table[ i ] >= result.bones.size( ) )
				{
					return result;
				}
			}

			std::vector<std::size_t> remap_starts( remap_starts_kv->size( ) );
			for ( std::size_t i = 0; i < remap_starts_kv->size( ); ++i )
			{
				remap_starts[ i ] = static_cast< std::size_t >( remap_starts_kv->at( i )->as_int( ) );
				if ( remap_starts[ i ] > remap_table.size( ) )
				{
					return result;
				}
			}

			const auto ctrl_doc = kv3::decode( res.bytes( *ctrl_block ), ctrl_block->size );
			const auto* embedded = ctrl_doc.root.find( "embedded_meshes" );
			if ( !embedded )
			{
				return result;
			}

			for ( std::size_t mesh_i = 0; mesh_i < embedded->size( ); ++mesh_i )
			{
				const auto* mesh_entry = embedded->at( mesh_i );
				const auto name = mesh_entry->find( "m_Name" )->as_string( );

				if ( contains_ci( name, "firstperson" ) )
				{
					continue;
				}

				if ( mesh_i >= remap_starts.size( ) )
				{
					continue;
				}

				const auto remap_begin = remap_starts[ mesh_i ];
				const auto remap_end = mesh_i + 1 < remap_starts.size( )
					? remap_starts[ mesh_i + 1 ]
					: remap_table.size( );
				if ( remap_end < remap_begin )
				{
					continue;
				}

				const std::span<const std::uint32_t> bone_remap{
					remap_table.data( ) + remap_begin, remap_end - remap_begin };

				const auto data_block_index = static_cast< std::size_t >( mesh_entry->find( "m_nDataBlock" )->as_int( ) );
				if ( data_block_index >= res.blocks( ).size( ) )
				{
					continue;
				}

				const auto& mdat_block = res.blocks( )[ data_block_index ];
				kv3::document mdat_doc{};
				try
				{
					mdat_doc = kv3::decode( res.bytes( mdat_block ), mdat_block.size );
				}
				catch ( const std::exception& )
				{
					continue;
				}

				const auto* scene_objects = mdat_doc.root.find( "m_sceneObjects" );
				const auto* scene0 = scene_objects ? scene_objects->at( 0 ) : nullptr;
				const auto* draw_calls_kv = scene0 ? scene0->find( "m_drawCalls" ) : nullptr;
				if ( !draw_calls_kv || draw_calls_kv->size( ) == 0 )
				{
					continue;
				}

				const auto* vb_descs_kv = mesh_entry->find( "m_vertexBuffers" );
				const auto* ib_descs_kv = mesh_entry->find( "m_indexBuffers" );
				if ( !vb_descs_kv || !ib_descs_kv )
				{
					continue;
				}

				std::unordered_map<int, std::vector<skinned_vertex>> decoded_vbs{};
				std::unordered_map<int, std::size_t> vb_global_offset{};
				std::unordered_map<int, std::vector<std::uint32_t>> decoded_ibs{};

				bool group_ok = true;

				for ( std::size_t i = 0; i < vb_descs_kv->size( ) && group_ok; ++i )
				{
					const auto desc = read_buffer_desc( *vb_descs_kv->at( i ) );
					std::vector<skinned_vertex> verts{};
					if ( !decode_vertex_buffer( res, desc, bone_remap, verts ) )
					{
						group_ok = false;
						break;
					}

					vb_global_offset[ static_cast< int >( i ) ] = result.vertices.size( );
					result.vertices.insert( result.vertices.end( ), verts.begin( ), verts.end( ) );
					decoded_vbs[ static_cast< int >( i ) ] = std::move( verts );
				}

				if ( !group_ok ) continue;

				for ( std::size_t i = 0; i < ib_descs_kv->size( ) && group_ok; ++i )
				{
					const auto desc = read_buffer_desc( *ib_descs_kv->at( i ) );
					std::vector<std::uint32_t> idx{};
					if ( !decode_index_buffer( res, desc, idx ) )
					{
						group_ok = false;
						break;
					}
					decoded_ibs[ static_cast< int >( i ) ] = std::move( idx );
				}

				if ( !group_ok ) continue;

				for ( std::size_t dc_i = 0; dc_i < draw_calls_kv->size( ); ++dc_i )
				{
					const auto* dc = draw_calls_kv->at( dc_i );

					const auto* dc_vbs = dc->find( "m_vertexBuffers" );
					const auto* dc_ib = dc->find( "m_indexBuffer" );
					if ( !dc_vbs || dc_vbs->size( ) == 0 || !dc_ib )
					{
						continue;
					}

					const auto vb_local = static_cast< int >( dc_vbs->at( 0 )->find( "m_hBuffer" )->as_int( ) );
					const auto ib_local = static_cast< int >( dc_ib->find( "m_hBuffer" )->as_int( ) );

					const auto vb_it = decoded_vbs.find( vb_local );
					const auto ib_it = decoded_ibs.find( ib_local );
					if ( vb_it == decoded_vbs.end( ) || ib_it == decoded_ibs.end( ) )
					{
						continue;
					}

					const auto start_index = static_cast< std::uint32_t >( dc->find( "m_nStartIndex" )->as_int( ) );
					const auto index_count = static_cast< std::uint32_t >( dc->find( "m_nIndexCount" )->as_int( ) );
					const auto global_vertex_offset = static_cast< std::uint32_t >( vb_global_offset[ vb_local ] );

					const auto& raw_indices = ib_it->second;
					if ( static_cast< std::uint64_t >( start_index ) + index_count > raw_indices.size( ) )
					{
						continue;
					}

					draw_range range{};
					range.index_offset = static_cast< std::uint32_t >( result.indices.size( ) );
					range.index_count = index_count;
					range.material = dc->find( "m_material" )->as_string( );

					result.indices.reserve( result.indices.size( ) + index_count );
					for ( std::uint32_t k = 0; k < index_count; ++k )
					{
						result.indices.push_back( raw_indices[ start_index + k ] + global_vertex_offset );
					}

					result.draw_calls.push_back( std::move( range ) );
				}
			}

			compute_normals( result );

			result.valid = !result.vertices.empty( ) && !result.indices.empty( ) && !result.draw_calls.empty( );
		}
		catch ( const std::exception& )
		{

			result = skinned_mesh{};
		}

		return result;
	}

}
