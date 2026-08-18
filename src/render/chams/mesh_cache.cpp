#include <stdafx.hpp>
#include <render/chams/mesh_cache.hpp>
#include <render/chams/mesh_extract.hpp>

#include <filesystem>
#include <fstream>

namespace chams {

	namespace {

		constexpr std::uint32_t k_cache_magic{ 0x4D414843 };

		constexpr std::uint32_t k_cache_version{ 6 };

		template <typename T>
		void write_pod( std::ofstream& f, const T& value )
		{
			f.write( reinterpret_cast< const char* >( &value ), sizeof( T ) );
		}

		template <typename T>
		[[nodiscard]] bool read_pod( std::ifstream& f, T& value )
		{
			f.read( reinterpret_cast< char* >( &value ), sizeof( T ) );
			return static_cast< bool >( f );
		}

		void write_string( std::ofstream& f, const std::string& s )
		{
			const auto len = static_cast< std::uint32_t >( s.size( ) );
			write_pod( f, len );
			if ( len ) f.write( s.data( ), len );
		}

		[[nodiscard]] bool read_string( std::ifstream& f, std::string& out )
		{
			std::uint32_t len{};
			if ( !read_pod( f, len ) || len > ( 1u << 20 ) )
			{
				return false;
			}
			out.resize( len );
			if ( len ) f.read( out.data( ), len );
			return static_cast< bool >( f );
		}

		[[nodiscard]] std::string sanitize_filename( const std::string& model_path )
		{
			std::string out{};
			out.reserve( model_path.size( ) );
			for ( const char c : model_path )
			{
				out.push_back( ( c == '/' || c == '\\' || c == ':' ) ? '_' : c );
			}
			return out;
		}

	}

	std::string mesh_cache::cache_file_path( const std::string& model_path )
	{

		auto dir = platform::windows::runtime_storage::area( "chams_cache" );
		if ( dir.empty( ) )
		{
			return {};
		}

		std::error_code ec{};
		std::filesystem::create_directories( dir, ec );

		return ( dir / ( sanitize_filename( model_path ) + ".chmesh" ) ).string( );
	}

	bool mesh_cache::load_from_disk( const std::string& path, skinned_mesh& out )
	{
		std::ifstream f( path, std::ios::binary );
		if ( !f.is_open( ) )
		{
			return false;
		}

		std::uint32_t magic{}, version{};
		if ( !read_pod( f, magic ) || magic != k_cache_magic ) return false;
		if ( !read_pod( f, version ) || version != k_cache_version ) return false;

		std::uint32_t vertex_count{}, index_count{}, draw_call_count{}, bone_count{};
		if ( !read_pod( f, vertex_count ) || !read_pod( f, index_count ) ||
			!read_pod( f, draw_call_count ) || !read_pod( f, bone_count ) )
		{
			return false;
		}

		out.vertices.resize( vertex_count );
		for ( auto& v : out.vertices )
		{
			if ( !read_pod( f, v.position ) || !read_pod( f, v.normal ) ||
				!read_pod( f, v.tangent ) || !read_pod( f, v.uv ) ||
				!read_pod( f, v.bone_indices ) || !read_pod( f, v.bone_weights ) )
			{
				return false;
			}
		}

		out.indices.resize( index_count );
		for ( auto& idx : out.indices )
		{
			if ( !read_pod( f, idx ) ) return false;
		}

		out.draw_calls.resize( draw_call_count );
		for ( auto& dc : out.draw_calls )
		{
			if ( !read_pod( f, dc.index_offset ) || !read_pod( f, dc.index_count ) || !read_string( f, dc.material ) )
			{
				return false;
			}
		}

		out.bones.resize( bone_count );
		for ( auto& b : out.bones )
		{
			if ( !read_string( f, b.name ) || !read_pod( f, b.parent ) || !read_pod( f, b.inverse_bind.m ) )
			{
				return false;
			}
		}

		out.valid = vertex_count > 0 && index_count > 0 && draw_call_count > 0;
		return out.valid;
	}

	void mesh_cache::save_to_disk( const std::string& path, const skinned_mesh& mesh )
	{
		if ( path.empty( ) )
		{
			return;
		}

		std::ofstream f( path, std::ios::binary | std::ios::trunc );
		if ( !f.is_open( ) )
		{
			return;
		}

		write_pod( f, k_cache_magic );
		write_pod( f, k_cache_version );
		write_pod( f, static_cast< std::uint32_t >( mesh.vertices.size( ) ) );
		write_pod( f, static_cast< std::uint32_t >( mesh.indices.size( ) ) );
		write_pod( f, static_cast< std::uint32_t >( mesh.draw_calls.size( ) ) );
		write_pod( f, static_cast< std::uint32_t >( mesh.bones.size( ) ) );

		for ( const auto& v : mesh.vertices )
		{
			write_pod( f, v.position );
			write_pod( f, v.normal );
			write_pod( f, v.tangent );
			write_pod( f, v.uv );
			write_pod( f, v.bone_indices );
			write_pod( f, v.bone_weights );
		}

		for ( const auto idx : mesh.indices )
		{
			write_pod( f, idx );
		}

		for ( const auto& dc : mesh.draw_calls )
		{
			write_pod( f, dc.index_offset );
			write_pod( f, dc.index_count );
			write_string( f, dc.material );
		}

		for ( const auto& b : mesh.bones )
		{
			write_string( f, b.name );
			write_pod( f, b.parent );
			write_pod( f, b.inverse_bind.m );
		}
	}

	const skinned_mesh& mesh_cache::get_or_build( vpk_archive& vpk, const std::string& model_path )
	{
		static const skinned_mesh pending_mesh{};

		if ( m_memory.empty( ) )
		{

			m_memory.reserve( 32 );
		}

		for ( auto pending = m_pending.begin( ); pending != m_pending.end( ); )
		{
			if ( pending->second.wait_for( std::chrono::milliseconds( 0 ) )
				== std::future_status::ready )
			{
				auto path = pending->first;
				auto mesh = pending->second.get( );
				pending = m_pending.erase( pending );
				m_memory.emplace( std::move( path ), std::move( mesh ) );
			}
			else
			{
				++pending;
			}
		}

		if ( const auto it = m_memory.find( model_path ); it != m_memory.end( ) )
		{
			return it->second;
		}

		if ( const auto pending = m_pending.find( model_path ); pending != m_pending.end( ) )
		{
			return pending_mesh;
		}

		if ( m_pending.empty( ) )
		{
			m_pending.reserve( 8 );
		}

		constexpr std::size_t k_max_extract_workers{ 2 };
		if ( m_pending.size( ) >= k_max_extract_workers )
		{
			return pending_mesh;
		}

		m_pending.emplace( model_path, std::async( std::launch::async,
			[ &vpk, model_path ]
			{
				const auto background_mode = ::SetThreadPriority(
					::GetCurrentThread( ), THREAD_MODE_BACKGROUND_BEGIN ) != FALSE;
				if ( !background_mode )
					::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_BELOW_NORMAL );

				const auto disk_path = cache_file_path( model_path );
				skinned_mesh mesh{};
				if ( !disk_path.empty( ) && load_from_disk( disk_path, mesh ) )
				{
					if ( background_mode )
						::SetThreadPriority( ::GetCurrentThread( ), THREAD_MODE_BACKGROUND_END );
					return mesh;
				}

				mesh = extract_mesh( vpk, model_path );
				if ( mesh.valid )
				{
					save_to_disk( disk_path, mesh );
				}
				if ( background_mode )
					::SetThreadPriority( ::GetCurrentThread( ), THREAD_MODE_BACKGROUND_END );
				return mesh;
			} ) );
		return pending_mesh;
	}

	void mesh_cache::clear_memory( )
	{
		m_memory.clear( );

	}

}
