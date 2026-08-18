#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>

namespace features::visuals {

	void sound_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::visual_settings.m_sound;
		if ( !cfg.enabled )
		{
			this->m_events.clear( );
			return;
		}
		if ( config::visual_settings.m_player.spectator_sync
			&& game::world( ).local_spectated( ) )
		{
			this->m_events.clear( );
			return;
		}

		if ( cfg.local_sync != this->m_last_local_sync )
		{

			this->m_events.clear( );
			this->m_last_local_sync = cfg.local_sync;
		}

		const auto now = std::chrono::steady_clock::now( );
		const auto players = game::world().players( );
		const auto eye = game::camera().origin( );

		constexpr float footstep_audible_distance{ 1100.0f };
		constexpr float footstep_audible_sq =
			footstep_audible_distance * footstep_audible_distance;

		const auto locally_audible = [ & ]( const foundation::vec3& position )
			{
				const auto source = position + foundation::vec3{ 0.0f, 0.0f, 20.0f };
				const auto delta = source - eye;
				return delta.dot( delta ) <= footstep_audible_sq;
			};

		for ( const auto& player : *players )
		{
			if ( !game::local_player().is_enemy( player.team ) )
			{
				continue;
			}

			const auto it = this->m_last_emit.find( player.pawn );
			if ( it == this->m_last_emit.end( ) )
			{
				this->m_last_emit[ player.pawn ] = player.emit_sound_time;
				continue;
			}

			if ( player.emit_sound_time > it->second + 0.001f )
			{
				auto& last_event = this->m_last_event[ player.pawn ];
				const auto duplicate = last_event
					!= std::chrono::steady_clock::time_point{}
					&& now - last_event < std::chrono::milliseconds( 140 );
				if ( !duplicate
					&& ( !cfg.local_sync || locally_audible( player.origin ) ) )
				{
					this->m_events.push_back( { player.origin, now } );
					last_event = now;
				}
			}
			it->second = player.emit_sound_time;
		}

		if ( this->m_last_emit.size( ) > 256 )
		{
			this->m_last_emit.clear( );
			this->m_last_event.clear( );
		}

		const float duration = std::max( 0.2f, cfg.duration );

		std::erase_if( this->m_events, [ & ]( const event& e )
			{
				return std::chrono::duration<float>( now - e.spawn ).count( ) >= duration;
			} );

		for ( const auto& e : this->m_events )
		{
			const float age = std::chrono::duration<float>( now - e.spawn ).count( );
			const float t = std::clamp( age / duration, 0.0f, 1.0f );
			const float fade = 1.0f - t;
			const float ring_radius = cfg.radius * ( 0.35f + 0.65f * t );
			this->draw_ring( draw_list, e.position, ring_radius, cfg.color, fade );
		}
	}

	void sound_t::draw_ring( zdraw::draw_list& draw_list, const foundation::vec3& center, float radius, const zdraw::rgba& color, float fade )
	{
		constexpr int segments = 32;
		constexpr float tau = 6.2831853f;

		std::array<foundation::vec2, segments> points{};
		for ( int i = 0; i < segments; ++i )
		{
			const float angle = tau * ( static_cast<float>( i ) / segments );
			const foundation::vec3 world{
				center.x + std::cos( angle ) * radius,
				center.y + std::sin( angle ) * radius,
				center.z };

			const auto screen = game::camera().project( world );
			if ( !game::camera().projection_valid( screen ) )
			{

				return;
			}
			points[ i ] = screen;
		}

		const auto alpha = static_cast<std::uint8_t>(
			std::clamp( static_cast<float>( color.a ) * fade, 0.0f, 255.0f ) );
		const zdraw::rgba ring_color{ color.r, color.g, color.b, alpha };

		for ( int i = 0; i < segments; ++i )
		{
			const auto& a = points[ i ];
			const auto& b = points[ ( i + 1 ) % segments ];
			draw_list.add_line( a.x, a.y, b.x, b.y, ring_color, 1.6f );
		}
	}

}
