#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>

namespace features::visuals {
namespace {

	using projectile_kind = game::projectile_kind;

	struct projectile_presentation
	{
		std::string_view icon;
		std::string_view label;
	};

	struct countdown
	{
		float seconds_left{};
		float completion{};
	};

	[[nodiscard]] constexpr projectile_presentation describe( projectile_kind kind ) noexcept
	{
		switch ( kind )
		{
		case projectile_kind::he_grenade:    return { "j", "he" };
		case projectile_kind::flashbang:     return { "i", "flash" };
		case projectile_kind::smoke_grenade: return { "k", "smoke" };
		case projectile_kind::molotov:       return { "l", "molotov" };
		case projectile_kind::molotov_fire:  return { "l", "fire" };
		case projectile_kind::decoy:         return { "m", "decoy" };
		default:                              return { "?", "grenade" };
		}
	}

	[[nodiscard]] zdraw::rgba color_for( projectile_kind kind, const config::visual_profile::projectile& palette ) noexcept
	{
		switch ( kind )
		{
		case projectile_kind::he_grenade:    return palette.color_he;
		case projectile_kind::flashbang:     return palette.color_flash;
		case projectile_kind::smoke_grenade: return palette.color_smoke;
		case projectile_kind::molotov:
		case projectile_kind::molotov_fire:  return palette.color_molotov;
		case projectile_kind::decoy:         return palette.color_decoy;
		default:                              return palette.default_color;
		}
	}

	[[nodiscard]] zdraw::rgba blend( const zdraw::rgba& from, const zdraw::rgba& to, float amount ) noexcept
	{
		const auto channel = [ amount ]( std::uint8_t first, std::uint8_t second )
		{
			return static_cast<std::uint8_t>( first + ( second - first ) * amount );
		};

		return { channel( from.r, to.r ), channel( from.g, to.g ),
			channel( from.b, to.b ), channel( from.a, to.a ) };
	}

	[[nodiscard]] std::optional<countdown> countdown_for(
		const game::projectile_snapshot& projectile, float game_time,
		std::optional<float> decoy_start_time ) noexcept
	{
		float duration{};
		float remaining{};

		switch ( projectile.subtype )
		{
		case projectile_kind::he_grenade:
			if ( projectile.spawn_time <= 0.0f
				|| projectile.detonate_time <= game_time )
				return std::nullopt;
			duration = std::max( 0.1f,
				projectile.detonate_time - projectile.spawn_time );
			remaining = projectile.detonate_time - game_time;
			break;

		case projectile_kind::molotov_fire:
			if ( projectile.expire_time <= 0.0f )
				return std::nullopt;
			duration = 7.0f;
			remaining = projectile.expire_time - game_time;
			break;

		case projectile_kind::smoke_grenade:
			if ( !projectile.smoke_active )
				return std::nullopt;
			duration = 18.0f;
			remaining = duration - ( game_time - projectile.effect_tick_begin / 64.0f );
			break;

		case projectile_kind::decoy:
			if ( !decoy_start_time )
				return std::nullopt;
			duration = 15.0f;
			remaining = duration - ( game_time - *decoy_start_time );
			break;

		default:
			return std::nullopt;
		}

		remaining = std::max( 0.0f, remaining );
		return countdown{ remaining,
			std::clamp( remaining / duration, 0.0f, 1.0f ) };
	}

	[[nodiscard]] zdraw::rgba alpha_scaled( const zdraw::rgba& color, float scale ) noexcept
	{
		return { color.r, color.g, color.b, static_cast<std::uint8_t>(
			std::clamp( static_cast<float>( color.a ) * scale, 0.0f, 255.0f ) ) };
	}

	void draw_projectile_indicator( zdraw::draw_list& output,
		const foundation::vec2& anchor, projectile_kind kind,
		const projectile_presentation& presentation, const zdraw::rgba& color,
		const std::optional<countdown>& timer,
		const config::visual_profile::projectile& settings )
	{
		auto* draw = output.m_im_draw_list;
		auto* base_font = app::context().overlay.fonts( ).weapons_15;
		if ( !draw || !base_font || !base_font->im_font )
			return;

		const auto inferno = kind == projectile_kind::molotov_fire;
		const auto radius = inferno ? 16.0f : 14.0f;
		const auto center = ImVec2{ std::floor( anchor.x ) + 0.5f,
			std::floor( anchor.y ) + 0.5f };
		const auto background = zdraw::draw_list::to_im_color( settings.indicator_background );
		const auto shadow = IM_COL32( 0, 0, 0, 105 );

		draw->AddCircleFilled( center, radius + 2.0f, shadow, 32 );
		draw->AddCircleFilled( center, radius, background, 32 );
		draw->AddCircle( center, radius, zdraw::draw_list::to_im_color( alpha_scaled( color, 0.34f ) ), 32, 1.0f );

		if ( timer && settings.show_timer_ring )
		{
			const auto ring_radius = radius + 2.5f;
			const auto fraction = std::clamp( timer->completion, 0.0f, 1.0f );
			const auto ring_color = blend( settings.timer_low_color,
				settings.timer_high_color, fraction );
			draw->AddCircle( center, ring_radius, IM_COL32( 0, 0, 0, 155 ), 40, 2.4f );
			if ( fraction > 0.002f )
			{
				constexpr auto start = -std::numbers::pi_v<float> * 0.5f;
				draw->PathArcTo( center, ring_radius, start,
					start + 2.0f * std::numbers::pi_v<float> * fraction, 40 );
				draw->PathStroke( zdraw::draw_list::to_im_color( ring_color ), 0, 2.4f );
			}
		}

		if ( settings.show_icon )
		{
			auto font = *base_font;
			font.font_size = inferno ? 21.0f : 18.0f;
			const auto [ width, height ] = zdraw::measure_text( presentation.icon, &font );
			output.add_text( std::floor( center.x - width * 0.5f ),
				std::floor( center.y - height * 0.5f ), presentation.icon,
				&font, color, zdraw::text_style::outlined );
		}
	}

	void draw_projectile_text( zdraw::draw_list& output,
		const foundation::vec2& anchor,
		const projectile_presentation& presentation, const zdraw::rgba& color,
		const std::optional<countdown>& timer,
		const config::visual_profile::projectile& settings )
	{
		auto* label_font = app::context().overlay.fonts( ).esp_text_11;
		auto* timer_font = app::context().overlay.fonts( ).notosans_medium_12;
		if ( !label_font || !label_font->im_font || !timer_font || !timer_font->im_font )
			return;

		const auto show_label = settings.show_icon;
		const auto show_timer = timer.has_value( ) && settings.show_timer_ring;
		if ( !show_label && !show_timer )
			return;

		auto y = std::floor( anchor.y );
		if ( show_label && show_timer )
			y -= 7.0f;

		if ( show_label )
		{
			const auto [ width, height ] = zdraw::measure_text( presentation.label, label_font );
			output.add_text( std::floor( anchor.x - width * 0.5f ), y,
				presentation.label, label_font, color, zdraw::text_style::outlined );
			y += std::max( 8.0f, height - 3.0f );
		}

		if ( show_timer )
		{
			const auto text = std::format( "{:.1f}s", timer->seconds_left );
			const auto timer_color = blend( settings.timer_low_color,
				settings.timer_high_color, timer->completion );
			const auto [ width, height ] = zdraw::measure_text( text, timer_font );
			output.add_text( std::floor( anchor.x - width * 0.5f ),
				show_label ? y : std::floor( anchor.y - height * 0.5f ),
				text, timer_font, timer_color, zdraw::text_style::outlined );
		}
	}

	void convex_outline( std::vector<foundation::vec2>& points,
		std::vector<foundation::vec2>& result )
	{
		result.clear( );
		if ( points.size( ) < 3 )
			return;

		std::ranges::sort( points, []( const foundation::vec2& lhs, const foundation::vec2& rhs )
			{
				return lhs.x < rhs.x || ( lhs.x == rhs.x && lhs.y < rhs.y );
			} );

		const auto orientation = []( const foundation::vec2& origin,
			const foundation::vec2& first, const foundation::vec2& second )
			{
				return ( first.x - origin.x ) * ( second.y - origin.y )
					- ( first.y - origin.y ) * ( second.x - origin.x );
			};

		result.reserve( points.size( ) * 2 );
		const auto append = [ & ]( const foundation::vec2& point )
		{
			while ( result.size( ) >= 2
				&& orientation( result[ result.size( ) - 2 ], result.back( ), point ) <= 0.0f )
			{
				result.pop_back( );
			}
			result.push_back( point );
		};

		for ( const auto& point : points )
			append( point );

		const auto lower_end = result.size( );
		for ( auto it = std::next( points.rbegin( ) ); it != points.rend( ); ++it )
		{
			while ( result.size( ) > lower_end
				&& orientation( result[ result.size( ) - 2 ], result.back( ), *it ) <= 0.0f )
			{
				result.pop_back( );
			}
			result.push_back( *it );
		}

		if ( result.size( ) < 4 )
		{
			result.clear( );
			return;
		}
		result.pop_back( );
	}

	void smooth_closed_outline( std::vector<foundation::vec2>& points, int passes )
	{
		if ( points.size( ) < 3 )
			return;

		static thread_local std::vector<foundation::vec2> next{};
		for ( int pass = 0; pass < passes; ++pass )
		{
			next.clear( );
			next.reserve( points.size( ) * 2 );
			for ( std::size_t i = 0; i < points.size( ); ++i )
			{
				const auto& current = points[ i ];
				const auto& following = points[ ( i + 1 ) % points.size( ) ];
				next.push_back( current * 0.75f + following * 0.25f );
				next.push_back( current * 0.25f + following * 0.75f );
			}
			points.swap( next );
		}
	}

	void draw_inward_gradient( ImDrawList& draw,
		std::span<const foundation::vec2> outline, const zdraw::rgba& color,
		float width, float opacity )
	{
		if ( outline.size( ) < 3 )
			return;

		auto center = foundation::vec2{};
		for ( const auto& point : outline ) center += point;
		center /= static_cast<float>( outline.size( ) );

		static thread_local std::vector<foundation::vec2> inner{};
		inner.clear( );
		inner.reserve( outline.size( ) );
		for ( const auto& point : outline )
		{
			const auto toward_center = center - point;
			const auto distance = toward_center.length( );
			const auto amount = distance > 0.001f
				? std::min( 1.0f, std::clamp( width, 8.0f, 80.0f ) / distance ) : 1.0f;
			inner.push_back( point + toward_center * amount );
		}

		const auto uv = ImGui::GetFontTexUvWhitePixel( );
		const auto edge = zdraw::draw_list::to_im_color( alpha_scaled( color,
			std::clamp( opacity, 0.0f, 100.0f ) / 100.0f ) );
		const auto transparent = IM_COL32( color.r, color.g, color.b, 0 );
		for ( std::size_t i = 0; i < outline.size( ); ++i )
		{
			const auto next = ( i + 1 ) % outline.size( );
			const auto base = draw._VtxCurrentIdx;
			draw.PrimReserve( 6, 4 );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base ) );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base + 1 ) );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base + 2 ) );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base ) );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base + 2 ) );
			draw.PrimWriteIdx( static_cast<ImDrawIdx>( base + 3 ) );
			draw.PrimWriteVtx( { outline[ i ].x, outline[ i ].y }, uv, edge );
			draw.PrimWriteVtx( { outline[ next ].x, outline[ next ].y }, uv, edge );
			draw.PrimWriteVtx( { inner[ next ].x, inner[ next ].y }, uv, transparent );
			draw.PrimWriteVtx( { inner[ i ].x, inner[ i ].y }, uv, transparent );
		}
	}

}

	void projectile_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& settings = config::visual_settings.m_projectile;
		if ( !settings.enabled )
		{
			this->m_decoy_start_times.clear( );
			return;
		}

		const auto game_time = game::local_player().game_time( );
		const auto projectiles = game::world().projectiles( );
		static thread_local std::vector<std::uintptr_t> active_decoys{};
		active_decoys.clear( );
		if ( active_decoys.capacity( ) < projectiles->size( ) )
			active_decoys.reserve( projectiles->size( ) );

		for ( const auto& projectile : *projectiles )
		{
			if ( projectile.detonated || projectile.origin.length_sqr( ) < 1.0f )
				continue;

			if ( projectile.subtype == projectile_kind::molotov_fire
				&& settings.show_inferno_bounds && !projectile.fire_points.empty( ) )
			{
				draw_inferno_bounds( draw_list, projectile, settings );
			}

			std::optional<float> decoy_start_time{};
			if ( projectile.subtype == projectile_kind::decoy )
			{
				active_decoys.push_back( projectile.entity );
				if ( const auto existing = this->m_decoy_start_times.find( projectile.entity );
					existing != this->m_decoy_start_times.end( ) )
				{
					decoy_start_time = existing->second;
				}
				else if ( projectile.effect_tick_begin > 0 )
				{
					auto start_time = projectile.effect_tick_begin / 64.0f;

					if ( projectile.detonate_time > 0.0f
						&& projectile.detonate_time <= game_time
						&& game_time - projectile.detonate_time <= 15.25f )
					{
						start_time = projectile.detonate_time;
					}
					this->m_decoy_start_times.emplace( projectile.entity, start_time );
					decoy_start_time = start_time;
				}
			}

			const auto screen = game::camera().project( projectile.origin );
			if ( !game::camera().projection_valid( screen ) )
				continue;

			const auto presentation = describe( projectile.subtype );
			const auto color = color_for( projectile.subtype, settings );
			const auto timer = countdown_for( projectile, game_time, decoy_start_time );
			if ( settings.display_mode
				== config::visual_profile::projectile::text_only )
			{
				draw_projectile_text( draw_list, screen, presentation,
					color, timer, settings );
			}
			else if ( settings.show_icon || ( timer && settings.show_timer_ring ) )
				draw_projectile_indicator( draw_list, screen, projectile.subtype,
					presentation, color, timer, settings );
		}

		std::erase_if( this->m_decoy_start_times, [ & ]( const auto& entry )
			{
				return std::ranges::find( active_decoys, entry.first )
					== active_decoys.end( );
			} );
	}

	void projectile_t::draw_inferno_bounds( zdraw::draw_list& draw_list,
		const game::projectile_snapshot& projectile,
		const config::visual_profile::projectile& settings ) const
	{
		constexpr float fire_radius = 60.0f;
		constexpr int samples_per_fire = 24;
		static thread_local std::vector<foundation::vec2> projected_points{};
		static thread_local std::vector<foundation::vec2> outline_points{};
		projected_points.clear( );
		projected_points.reserve( projectile.fire_points.size( ) * samples_per_fire );

		for ( const auto& center : projectile.fire_points )
		{
			for ( int sample = 0; sample < samples_per_fire; ++sample )
			{
				const auto angle = sample * ( 2.0f * std::numbers::pi_v<float> / samples_per_fire );
				const auto edge = center + foundation::vec3{ std::cos( angle ) * fire_radius,
					std::sin( angle ) * fire_radius, 0.0f };
				const auto screen = game::camera().project( edge );
				if ( game::camera().projection_valid( screen ) )
					projected_points.push_back( screen );
			}
		}

		convex_outline( projected_points, outline_points );
		if ( outline_points.empty( ) )
			return;
		smooth_closed_outline( outline_points, 2 );

		const auto polygon = std::span<const float>(
			reinterpret_cast<const float*>( outline_points.data( ) ), outline_points.size( ) * 2 );
		if ( draw_list.m_im_draw_list )
			draw_inward_gradient( *draw_list.m_im_draw_list, outline_points,
				settings.color_molotov, settings.inferno_gradient_width,
				settings.inferno_gradient_opacity );

		draw_list.add_polyline( polygon, alpha_scaled( settings.color_molotov, 0.10f ), true, 7.0f );
		draw_list.add_polyline( polygon, alpha_scaled( settings.color_molotov, 0.26f ), true, 3.5f );
		draw_list.add_polyline( polygon, settings.color_molotov, true, 1.5f );
	}

}
