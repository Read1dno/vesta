#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/crosshair_sync.hpp>

namespace features::visuals {

	void crosshair_t::on_render( zdraw::draw_list& draw_list )
	{
		auto cfg = config::visual_settings.m_crosshair;
		if ( !cfg.enabled )
		{
			return;
		}
		const auto [ sw, sh ] = zdraw::get_display_size( );
		if ( sw <= 0 || sh <= 0 ) return;

		if ( cfg.sync && !cfg.copy_game )
		{
			const auto& weapon_ctx = simulation::ballistics().ctx( );
			if ( weapon_ctx.valid )
			{

				bool game_has_crosshair = true;
				switch ( weapon_ctx.weapon_type )
				{
				case game::rules::precision:
					game_has_crosshair = weapon_ctx.is_scoped;
					break;
				case game::rules::objective:
					game_has_crosshair = false;
					break;
				default:
					game_has_crosshair = true;
					break;
				}

				if ( game_has_crosshair )
				{
					return;
				}
			}
		}

		auto crosshair_color = cfg.color;
        bool penetration_override{};
		if ( cfg.penetration_enabled )
		{

			const auto& ctx = simulation::ballistics().ctx( );
			if ( ctx.valid && game::rules::is_firearm( ctx.weapon_type ) )
			{
				penetration_override = true;
                const auto eye_pos = game::camera().origin( );
				const auto view_angles = game::camera().angles( );
				foundation::vec3 forward{};
				view_angles.to_directions( &forward, nullptr, nullptr );

				const auto range = simulation::ballistics().pen( ).get_weapon_data( ).range;
				const auto first_hit = game::collision().trace_ray( eye_pos, eye_pos + forward * range );
				if ( first_hit.hit )
				{
					// A wall is in the way: green only if it can be shot through for
					// enough damage, red otherwise.
					auto pen_damage{ 0.0f };
					const auto can_pen = simulation::ballistics().pen( ).can( eye_pos, forward, pen_damage );
					crosshair_color = ( can_pen && pen_damage >= cfg.penetration_min_damage )
						? cfg.penetration_color_yes
						: cfg.penetration_color_no;
				}
				else
				{
					// Clear line of sight - a normal shot lands, so show the "yes" colour.
					crosshair_color = cfg.penetration_color_yes;
				}
			}
		}

        if (cfg.copy_game) {
            draw_game_crosshair(draw_list,static_cast<float>(sw),static_cast<float>(sh),
                penetration_override ? &crosshair_color : nullptr);
            return;
        }
        const auto center_x=static_cast<float>(sw)*0.5f;
        const auto center_y=static_cast<float>(sh)*0.5f;

		const auto bar = std::max( 1.0f, std::round( cfg.thickness ) );
		const auto center_lo_x = center_x - bar * 0.5f;
		const auto center_hi_x = center_lo_x + bar;
		const auto center_lo_y = center_y - bar * 0.5f;
		const auto center_hi_y = center_lo_y + bar;

		const auto gap = std::clamp( std::round( cfg.gap ), -128.0f, 256.0f );
		const auto length = std::max( 0.0f, std::round( cfg.length ) );
		const auto outline = ( cfg.outline && cfg.outline_color.a > 0 )
			? std::max( 1.0f, std::round( cfg.outline_thickness ) ) : 0.0f;

		struct rect { float x0, y0, x1, y1; };
		std::array<rect, 5> pieces{};
		std::size_t count{};

		const auto push = [ & ]( float x0, float y0, float x1, float y1 )
		{
			if ( x1 > x0 && y1 > y0 && count < pieces.size( ) )
			{
				pieces[ count++ ] = { x0, y0, x1, y1 };
			}
		};

		if ( cfg.lines && length > 0.0f )
		{
			// Arms are measured outward from the centre bar's own edges, so the
			// pattern stays symmetric about the centre pixel at any thickness.
			push( center_lo_x - gap - length, center_lo_y, center_lo_x - gap, center_hi_y );
			push( center_hi_x + gap, center_lo_y, center_hi_x + gap + length, center_hi_y );

			push( center_lo_x, center_hi_y + gap, center_hi_x, center_hi_y + gap + length );
			if ( !cfg.t_style )
			{
				push( center_lo_x, center_lo_y - gap - length, center_hi_x, center_lo_y - gap );
			}
		}

		if ( cfg.dot )
		{
			push( center_lo_x, center_lo_y, center_hi_x, center_hi_y );
		}

		if ( outline > 0.0f )
		{
			for ( std::size_t i = 0; i < count; ++i )
			{
				const auto& p = pieces[ i ];
				draw_list.add_rect_filled( p.x0 - outline, p.y0 - outline,
					( p.x1 - p.x0 ) + outline * 2.0f, ( p.y1 - p.y0 ) + outline * 2.0f, cfg.outline_color );
			}
		}

		for ( std::size_t i = 0; i < count; ++i )
		{
			const auto& p = pieces[ i ];
			draw_list.add_rect_filled( p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0, crosshair_color );
		}
	}

} // namespace features::visuals
