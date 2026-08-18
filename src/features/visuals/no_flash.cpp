#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>
#include <app/workers.hpp>

namespace features::visuals {

	namespace {

		constexpr std::size_t k_max_tris{ 2500 };

		constexpr float k_rebuild_distance{ 300.0f };
	}

	void no_flash_t::rebuild_cache( const foundation::vec3& eye_pos, float max_distance )
	{
		this->m_cached_tris.clear( );

		if ( !game::collision().valid( ) )
		{
			return;
		}

		const auto tris = game::collision().triangles( );
		const auto max_dist_sq = max_distance * max_distance;

		struct scored
		{
			float dist_sq;
			std::uint32_t index;
		};

		std::vector<scored> nearby{};
		nearby.reserve( 8192 );

		for ( std::uint32_t i = 0; i < tris.size( ); ++i )
		{
			const auto& t = tris[ i ];
			const auto centroid = ( t.v0 + t.v1 + t.v2 ) * ( 1.0f / 3.0f );
			const auto dist_sq = ( centroid - eye_pos ).length_sqr( );

			if ( dist_sq <= max_dist_sq )
			{
				nearby.push_back( { dist_sq, i } );
			}
		}

		if ( nearby.size( ) > k_max_tris )
		{
			std::nth_element( nearby.begin( ), nearby.begin( ) + k_max_tris, nearby.end( ),
				[ ]( const scored& a, const scored& b ) { return a.dist_sq < b.dist_sq; } );
			nearby.resize( k_max_tris );
		}

		this->m_cached_tris.reserve( nearby.size( ) );
		for ( const auto& s : nearby )
		{
			this->m_cached_tris.push_back( tris[ s.index ] );
		}

		this->m_cache_origin = eye_pos;
		this->m_cache_valid = true;
	}

	void no_flash_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::visual_settings.m_no_flash;
		if ( !cfg.enabled )
		{
			this->m_cache_valid = false;
			return;
		}

		const auto pawn = game::local_player().view_pawn( );
		if ( !pawn )
		{
			return;
		}

		const auto flash_alpha = game::local_player().flash_alpha( );

		if ( flash_alpha < 0.01f )
		{
			this->m_cache_valid = false;
			return;
		}

		const auto blind = std::clamp( flash_alpha > 1.5f ? flash_alpha / 255.0f : flash_alpha, 0.0f, 1.0f );

		const auto [sw, sh] = zdraw::get_display_size( );
		const auto bg_alpha = static_cast<std::uint8_t>( blind * static_cast<float>( cfg.background_color.a ) );

		draw_list.add_rect_filled(
			0.0f, 0.0f,
			static_cast<float>( sw ), static_cast<float>( sh ),
			zdraw::rgba( cfg.background_color.r, cfg.background_color.g, cfg.background_color.b, bg_alpha )
		);

		if ( blind < 0.5f )
		{
			return;
		}

		const auto eye_pos = game::camera().origin( );

		struct async_cache_state
		{
			std::future<std::unique_ptr<no_flash_t>> job{};
			float max_distance{};
			std::string job_map{};
			std::string applied_map{};
		};
		static async_cache_state async_cache{};
		const auto map_snapshot = app::workers::current_map( );
		static const std::string empty_map{};
		const auto& current_map = map_snapshot ? *map_snapshot : empty_map;

		if ( async_cache.job.valid( )
			&& async_cache.job.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready )
		{
			auto ready = async_cache.job.get( );
			if ( ready && ready->m_cache_valid
				&& async_cache.job_map == current_map
				&& std::abs( async_cache.max_distance - cfg.max_distance ) <= 0.5f
				&& ready->m_cache_origin.distance( eye_pos ) <= k_rebuild_distance )
			{
				this->m_cached_tris = std::move( ready->m_cached_tris );
				this->m_cache_origin = ready->m_cache_origin;
				this->m_cache_valid = true;
				async_cache.applied_map = current_map;
			}
		}

		if ( !this->m_cache_valid || async_cache.applied_map != current_map
			|| this->m_cache_origin.distance( eye_pos ) > k_rebuild_distance )
		{
			if ( !async_cache.job.valid( ) )
			{
				async_cache.max_distance = cfg.max_distance;
				async_cache.job_map = current_map;
				async_cache.job = std::async( std::launch::async,
					[ eye_pos, max_distance = cfg.max_distance ]
					{
						const auto background_mode = ::SetThreadPriority(
							::GetCurrentThread( ), THREAD_MODE_BACKGROUND_BEGIN ) != FALSE;
						if ( !background_mode )
							::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_BELOW_NORMAL );
						auto built = std::make_unique<no_flash_t>( );
						built->rebuild_cache( eye_pos, max_distance );
						if ( background_mode )
							::SetThreadPriority( ::GetCurrentThread( ), THREAD_MODE_BACKGROUND_END );
						return built;
					} );
			}
			return;
		}

		const auto wire_alpha = static_cast<std::uint8_t>( blind * static_cast<float>( cfg.wireframe_color.a ) );
		const auto wire_color = zdraw::rgba( cfg.wireframe_color.r, cfg.wireframe_color.g, cfg.wireframe_color.b, wire_alpha );

		for ( const auto& t : this->m_cached_tris )
		{
			const auto p0 = game::camera().project( t.v0 );
			const auto p1 = game::camera().project( t.v1 );
			const auto p2 = game::camera().project( t.v2 );

			if ( !game::camera().projection_valid( p0 )
				|| !game::camera().projection_valid( p1 )
				|| !game::camera().projection_valid( p2 ) )
			{
				continue;
			}

			draw_list.add_line( p0.x, p0.y, p1.x, p1.y, wire_color, 1.0f );
			draw_list.add_line( p1.x, p1.y, p2.x, p2.y, wire_color, 1.0f );
			draw_list.add_line( p2.x, p2.y, p0.x, p0.y, wire_color, 1.0f );
		}
	}

}
