#include <stdafx.hpp>
#include <render/chams/animation.hpp>
#include <core/assets/kv3.hpp>
#include <core/assets/resource.hpp>

namespace chams {

	namespace {

		[[nodiscard]] foundation::rotation make_quat( float x, float y, float z, float w )
		{
			foundation::rotation q{};
			q.x = x; q.y = y; q.z = z; q.w = w;
			return q;
		}

		[[nodiscard]] foundation::rotation decode_quaternion48( const std::uint8_t* b )
		{
			const auto i1 = b[ 0 ] + ( ( b[ 1 ] & 127 ) << 8 );
			const auto i2 = b[ 2 ] + ( ( b[ 3 ] & 127 ) << 8 );
			const auto i3 = b[ 4 ] + ( ( b[ 5 ] & 127 ) << 8 );

			const auto swap_a = ( b[ 1 ] & 128 ) != 0;
			const auto swap_b = ( b[ 3 ] & 128 ) != 0;
			const auto negate = ( b[ 5 ] & 128 ) != 0;

			constexpr auto scale = 0.70710678f / 16384.0f;
			const auto x = scale * static_cast< float >( i1 - 16384 );
			const auto y = scale * static_cast< float >( i2 - 16384 );
			const auto z = scale * static_cast< float >( i3 - 16384 );

			auto w = std::sqrt( std::max( 0.0f, 1.0f - x * x - y * y - z * z ) );
			if ( negate )
			{
				w = -w;
			}

			const auto slot = ( swap_a ? 2 : 0 ) | ( swap_b ? 1 : 0 );
			switch ( slot )
			{
			case 0:  return make_quat( w, x, y, z );
			case 1:  return make_quat( x, w, y, z );
			case 2:  return make_quat( x, y, w, z );
			default: return make_quat( x, y, z, w );
			}
		}

		[[nodiscard]] float dequantize( std::uint16_t raw, float start, float length )
		{
			return start + ( static_cast< float >( raw ) / 65535.0f ) * length;
		}

		[[nodiscard]] foundation::rotation quat_multiply( const foundation::rotation& a, const foundation::rotation& b )
		{
			return make_quat( a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z );
		}

		[[nodiscard]] foundation::rotation quat_conjugate( const foundation::rotation& q )
		{
			return make_quat( -q.x, -q.y, -q.z, q.w );
		}

		[[nodiscard]] foundation::rotation quat_slerp( const foundation::rotation& a, foundation::rotation b, float t )
		{
			auto dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			if ( dot < 0.0f )
			{

				b = make_quat( -b.x, -b.y, -b.z, -b.w );
				dot = -dot;
			}

			if ( dot > 0.9995f )
			{

				auto out = make_quat(
					a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t,
					a.z + ( b.z - a.z ) * t, a.w + ( b.w - a.w ) * t );
				const auto len = std::sqrt( out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w );
				if ( len > 1e-8f ) { out.x /= len; out.y /= len; out.z /= len; out.w /= len; }
				return out;
			}

			const auto theta = std::acos( std::clamp( dot, -1.0f, 1.0f ) );
			const auto sin_theta = std::sin( theta );
			const auto wa = std::sin( ( 1.0f - t ) * theta ) / sin_theta;
			const auto wb = std::sin( t * theta ) / sin_theta;
			return make_quat( a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb );
		}

		[[nodiscard]] bone_matrix rotation_matrix( const foundation::rotation& q )
		{
			bone_matrix out{};
			const auto xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const auto xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const auto wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			out.m[ 0 ][ 0 ] = 1.0f - 2.0f * ( yy + zz ); out.m[ 0 ][ 1 ] = 2.0f * ( xy - wz );       out.m[ 0 ][ 2 ] = 2.0f * ( xz + wy );
			out.m[ 1 ][ 0 ] = 2.0f * ( xy + wz );        out.m[ 1 ][ 1 ] = 1.0f - 2.0f * ( xx + zz ); out.m[ 1 ][ 2 ] = 2.0f * ( yz - wx );
			out.m[ 2 ][ 0 ] = 2.0f * ( xz - wy );        out.m[ 2 ][ 1 ] = 2.0f * ( yz + wx );       out.m[ 2 ][ 2 ] = 1.0f - 2.0f * ( xx + yy );
			return out;
		}

		[[nodiscard]] bool decode_data_block( vpk_archive& vpk, const std::string& path,
			resource& res, kv3::document& doc )
		{
			auto full = path;
			if ( !full.ends_with( "_c" ) )
			{
				full += "_c";
			}

			const auto* entry = vpk.find( full );
			if ( !entry || !res.parse( vpk.read( *entry ) ) )
			{
				return false;
			}

			const auto* block = res.find( "DATA" );
			if ( !block )
			{
				return false;
			}

			doc = kv3::decode( res.bytes( *block ), block->size );
			return true;
		}

	}

	nm_skeleton load_nm_skeleton( vpk_archive& vpk, const std::string& archive_path )
	{
		nm_skeleton out{};

		try
		{
			resource res{};
			kv3::document doc{};
			if ( !decode_data_block( vpk, archive_path, res, doc ) )
			{
				return out;
			}

			const auto* names = doc.root.find( "m_boneIDs" );
			const auto* parents = doc.root.find( "m_parentIndices" );
			const auto* rest = doc.root.find( "m_parentSpaceReferencePose" );
			if ( !names || !parents || !rest || names->size( ) == 0 )
			{
				return out;
			}

			out.bone_names.reserve( names->size( ) );
			out.parent_indices.reserve( names->size( ) );
			out.rest_positions.reserve( names->size( ) );

			for ( std::size_t i = 0; i < names->size( ); ++i )
			{
				out.bone_names.push_back( names->at( i )->as_string( ) );
				out.parent_indices.push_back( i < parents->size( )
					? static_cast< int >( parents->at( i )->as_int( ) ) : -1 );

				foundation::vec3 p{};
				if ( i < rest->size( ) )
				{
					const auto* entry = rest->at( i );
					if ( entry && entry->size( ) >= 3 )
					{
						p = foundation::vec3{
							static_cast< float >( entry->at( 0 )->as_double( ) ),
							static_cast< float >( entry->at( 1 )->as_double( ) ),
							static_cast< float >( entry->at( 2 )->as_double( ) ) };
					}
				}
				out.rest_positions.push_back( p );
			}

			return out;
		}
		catch ( const std::exception& )
		{
			return {};
		}
	}

	nm_clip load_nm_clip( vpk_archive& vpk, const std::string& archive_path )
	{
		nm_clip out{};

		try
		{
			resource res{};
			kv3::document doc{};
			if ( !decode_data_block( vpk, archive_path, res, doc ) )
			{
				return out;
			}

			const auto& root = doc.root;
			const auto* frames = root.find( "m_nNumFrames" );
			const auto* duration = root.find( "m_flDuration" );
			const auto* pose_data = root.find( "m_compressedPoseData" );
			const auto* offsets = root.find( "m_compressedPoseOffsets" );
			const auto* settings = root.find( "m_trackCompressionSettings" );

			if ( !frames || !pose_data || !offsets || !settings )
			{
				return out;
			}

			out.frame_count = static_cast< std::uint32_t >( frames->as_int( ) );
			out.duration = duration ? static_cast< float >( duration->as_double( ) ) : 0.0f;
			if ( const auto* additive = root.find( "m_bIsAdditive" ) )
			{
				out.additive = additive->as_bool( );
			}

			if ( out.additive || out.frame_count == 0 )
			{
				return {};
			}

			const auto* blob = std::get_if<std::vector<std::uint8_t>>( &pose_data->m_value );
			if ( !blob || blob->size( ) < 2 )
			{
				return {};
			}

			out.words.resize( blob->size( ) / 2 );
			std::memcpy( out.words.data( ), blob->data( ), out.words.size( ) * 2 );

			out.frame_offsets.reserve( offsets->size( ) );
			for ( std::size_t i = 0; i < offsets->size( ); ++i )
			{
				out.frame_offsets.push_back( static_cast< std::uint32_t >( offsets->at( i )->as_int( ) ) );
			}

			const auto range = [ ]( const kv3::object* obj, const char* name, float& start, float& length )
				{
					const auto* r = obj ? obj->find( name ) : nullptr;
					if ( !r ) return;
					if ( const auto* s = r->find( "m_flRangeStart" ) ) start = static_cast< float >( s->as_double( ) );
					if ( const auto* l = r->find( "m_flRangeLength" ) ) length = static_cast< float >( l->as_double( ) );
				};

			out.tracks.reserve( settings->size( ) );
			for ( std::size_t i = 0; i < settings->size( ); ++i )
			{
				const auto* item = settings->at( i );
				nm_clip::track t{};

				range( item, "m_translationRangeX", t.translation_start[ 0 ], t.translation_length[ 0 ] );
				range( item, "m_translationRangeY", t.translation_start[ 1 ], t.translation_length[ 1 ] );
				range( item, "m_translationRangeZ", t.translation_start[ 2 ], t.translation_length[ 2 ] );
				range( item, "m_scaleRange", t.scale_start, t.scale_length );

				if ( const auto* v = item->find( "m_nTrackReadOffset" ) ) t.read_offset = static_cast< std::uint32_t >( v->as_int( ) );
				if ( const auto* v = item->find( "m_bIsRotationStatic" ) ) t.rotation_static = v->as_bool( );
				if ( const auto* v = item->find( "m_bIsTranslationStatic" ) ) t.translation_static = v->as_bool( );
				if ( const auto* v = item->find( "m_bIsScaleStatic" ) ) t.scale_static = v->as_bool( );

				if ( const auto* c = item->find( "m_constantRotation" ); c && c->size( ) >= 4 )
				{
					t.constant_rotation = make_quat( static_cast< float >( c->at( 0 )->as_double( ) ), static_cast< float >( c->at( 1 )->as_double( ) ), static_cast< float >( c->at( 2 )->as_double( ) ), static_cast< float >( c->at( 3 )->as_double( ) ) );
				}

				out.tracks.push_back( t );
			}

			return out;
		}
		catch ( const std::exception& )
		{
			return {};
		}
	}

	std::vector<int> map_tracks_to_model( const nm_skeleton& skeleton, const skinned_mesh& mesh )
	{
		std::unordered_map<std::string, int> by_name{};
		for ( std::size_t i = 0; i < mesh.bones.size( ); ++i )
		{
			by_name.emplace( mesh.bones[ i ].name, static_cast< int >( i ) );
		}

		std::vector<int> out( skeleton.bone_names.size( ), -1 );
		for ( std::size_t i = 0; i < skeleton.bone_names.size( ); ++i )
		{
			if ( const auto it = by_name.find( skeleton.bone_names[ i ] ); it != by_name.end( ) )
			{
				out[ i ] = it->second;
			}
		}
		return out;
	}

	void sample_pose( const nm_clip& clip, const nm_skeleton& skeleton, const skinned_mesh& mesh,
		const std::vector<int>& track_to_bone, float time,
		std::vector<bone_matrix>& out, std::vector<bone_matrix>& world )
	{
		const auto bone_count = mesh.bones.size( );
		out.assign( bone_count, bone_matrix::identity( ) );
		world.assign( bone_count, bone_matrix::identity( ) );

		const auto track_count = clip.tracks.size( );
		if ( !clip.valid( ) || bone_count == 0 || track_count == 0
			|| skeleton.parent_indices.size( ) < track_count )
		{
			return;
		}

		const auto duration = clip.duration > 0.001f ? clip.duration : 1.0f;
		auto phase = std::fmod( time, duration ) / duration;
		if ( phase < 0.0f ) phase += 1.0f;

		const auto exact = phase * static_cast< float >( clip.frame_count );
		const auto frame_a = static_cast< std::uint32_t >( exact ) % clip.frame_count;
		const auto frame_b = ( frame_a + 1 ) % clip.frame_count;
		const auto blend = exact - std::floor( exact );

		if ( frame_a >= clip.frame_offsets.size( ) || frame_b >= clip.frame_offsets.size( ) )
		{
			return;
		}

		const auto read = [ & ]( std::uint32_t frame, std::size_t index, foundation::rotation& rotation, foundation::vec3& translation )
			{
				const auto& track = clip.tracks[ index ];
				auto cursor = clip.frame_offsets[ frame ] + track.read_offset;

				rotation = track.constant_rotation;
				if ( !track.rotation_static )
				{
					if ( cursor + 3 <= clip.words.size( ) )
					{
						std::uint8_t bytes[ 6 ]{};
						std::memcpy( bytes, clip.words.data( ) + cursor, 6 );
						rotation = decode_quaternion48( bytes );
					}
					cursor += 3;
				}

				translation = skeleton.rest_positions[ index ];
				if ( !track.translation_static && cursor + 3 <= clip.words.size( ) )
				{
					translation = foundation::vec3{
						dequantize( clip.words[ cursor + 0 ], track.translation_start[ 0 ], track.translation_length[ 0 ] ),
						dequantize( clip.words[ cursor + 1 ], track.translation_start[ 1 ], track.translation_length[ 1 ] ),
						dequantize( clip.words[ cursor + 2 ], track.translation_start[ 2 ], track.translation_length[ 2 ] ) };
				}
			};

		std::vector<bone_matrix> clip_world( track_count, bone_matrix::identity( ) );
		for ( std::size_t i = 0; i < track_count; ++i )
		{
			foundation::rotation rot_a{}, rot_b{};
			foundation::vec3 pos_a{}, pos_b{};
			read( frame_a, i, rot_a, pos_a );
			read( frame_b, i, rot_b, pos_b );

			auto local = rotation_matrix( quat_slerp( rot_a, rot_b, blend ) );
			local.m[ 0 ][ 3 ] = pos_a.x + ( pos_b.x - pos_a.x ) * blend;
			local.m[ 1 ][ 3 ] = pos_a.y + ( pos_b.y - pos_a.y ) * blend;
			local.m[ 2 ][ 3 ] = pos_a.z + ( pos_b.z - pos_a.z ) * blend;

			const auto parent = skeleton.parent_indices[ i ];
			clip_world[ i ] = parent < 0 || static_cast< std::size_t >( parent ) >= i
				? local
				: clip_world[ static_cast< std::size_t >( parent ) ] * local;
		}

		for ( std::size_t i = 0; i < track_count && i < track_to_bone.size( ); ++i )
		{
			const auto bone = track_to_bone[ i ];
			if ( bone < 0 || static_cast< std::size_t >( bone ) >= bone_count )
			{
				continue;
			}

			world[ static_cast< std::size_t >( bone ) ] = clip_world[ i ];
			out[ static_cast< std::size_t >( bone ) ] = clip_world[ i ] * mesh.bones[ static_cast< std::size_t >( bone ) ].inverse_bind;
		}

		std::vector<bool> driven( bone_count, false );
		for ( std::size_t i = 0; i < track_count && i < track_to_bone.size( ); ++i )
		{
			const auto bone = track_to_bone[ i ];
			if ( bone >= 0 && static_cast< std::size_t >( bone ) < bone_count )
			{
				driven[ static_cast< std::size_t >( bone ) ] = true;
			}
		}

		for ( std::size_t i = 0; i < bone_count; ++i )
		{
			if ( driven[ i ] )
			{
				continue;
			}

			const auto bind = mesh.bones[ i ].inverse_bind.inverse_rigid( );
			const auto parent = mesh.bones[ i ].parent;

			if ( parent < 0 || static_cast< std::size_t >( parent ) >= bone_count )
			{
				world[ i ] = bind;
			}
			else
			{

				const auto local = mesh.bones[ static_cast< std::size_t >( parent ) ].inverse_bind * bind;
				world[ i ] = world[ static_cast< std::size_t >( parent ) ] * local;
			}

			out[ i ] = world[ i ] * mesh.bones[ i ].inverse_bind;
		}
	}

}
