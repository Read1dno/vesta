#include <stdafx.hpp>
#include <render/chams/material.hpp>
#include <core/assets/kv3.hpp>
#include <core/assets/resource.hpp>

namespace chams {

	namespace {

		[[nodiscard]] const kv3::object* find_param( const kv3::object* array, const char* name )
		{
			if ( !array )
			{
				return nullptr;
			}

			for ( std::size_t i = 0; i < array->size( ); ++i )
			{
				const auto* item = array->at( i );
				const auto* name_kv = item ? item->find( "m_name" ) : nullptr;
				if ( name_kv && name_kv->as_string( ) == name )
				{
					return item;
				}
			}

			return nullptr;
		}

		[[nodiscard]] std::string texture_path( const kv3::object* params, const char* slot )
		{
			const auto* param = find_param( params, slot );
			const auto* value = param ? param->find( "m_pValue" ) : nullptr;
			return value ? value->as_string( ) : std::string{};
		}

		void read_vector( const kv3::object* params, const char* name, float* out, int count )
		{
			const auto* param = find_param( params, name );
			const auto* value = param ? param->find( "m_value" ) : nullptr;
			if ( !value )
			{
				return;
			}

			float raw[ 4 ]{};
			if ( count == 4 && value->as_float4( raw ) )
			{
				for ( int i = 0; i < 4; ++i ) out[ i ] = raw[ i ];
			}
			else if ( count == 2 )
			{

				if ( value->as_float4( raw ) )
				{
					out[ 0 ] = raw[ 0 ];
					out[ 1 ] = raw[ 1 ];
				}
			}
		}

	}

	material_data load_material( vpk_archive& vpk, const std::string& archive_path )
	{
		material_data result{};

		try
		{
			auto path = archive_path;
			if ( path.ends_with( ".vmat" ) )
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

			const auto doc = kv3::decode( res.bytes( *data_block ), data_block->size );
			const auto& root = doc.root;

			if ( const auto* shader = root.find( "m_shaderName" ) )
			{
				result.shader = shader->as_string( );
			}

			const auto* textures = root.find( "m_textureParams" );
			result.color = texture_path( textures, "g_tColor" );
			result.normal = texture_path( textures, "g_tNormal" );
			result.metalness = texture_path( textures, "g_tMetalness" );
			result.ambient_occlusion = texture_path( textures, "g_tAmbientOcclusion" );
			result.gloss = texture_path( textures, "g_tAnisoGloss" );

			const auto* vectors = root.find( "m_vectorParams" );
			read_vector( vectors, "g_vTexCoordScale", result.uv_scale, 2 );
			read_vector( vectors, "g_vTexCoordOffset", result.uv_offset, 2 );
			read_vector( vectors, "g_vColorTint", result.color_tint, 4 );

			return result;
		}
		catch ( const std::exception& )
		{
			return {};
		}
	}

}
