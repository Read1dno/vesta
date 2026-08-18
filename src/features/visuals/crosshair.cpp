#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>

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

		if ( cfg.copy_game )
		{
			struct game_crosshair_snapshot
			{
				bool dot{};
				bool t_style{};
				bool outline{};
				float length{ 5.0f };
				float thickness{ 1.0f };
				float gap{ 3.0f };
				float outline_thickness{ 1.0f };
				zdraw::rgba color{ 255, 255, 255, 255 };
			};
			static game_crosshair_snapshot game_cfg{};
			static auto next_refresh = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now();
			if ( now >= next_refresh )
			{
				next_refresh = now + std::chrono::milliseconds( 33 );
				game_cfg.dot = game::variables().get<bool>(
					CONVAR( "cl_crosshairdot"_id ) );
				game_cfg.t_style = game::variables().get<bool>(
					CONVAR( "cl_crosshair_t"_id ) );
				game_cfg.length = std::clamp( game::variables().get<float>(
					CONVAR( "cl_crosshairsize"_id ) ), 0.0f, 50.0f );
				game_cfg.thickness = std::clamp( game::variables().get<float>(
					CONVAR( "cl_crosshairthickness"_id ) ), 0.5f, 10.0f );
				game_cfg.gap = std::clamp( game::variables().get<float>(
					CONVAR( "cl_crosshairgap"_id ) ), -10.0f, 50.0f );
				game_cfg.outline = game::variables().get<bool>(
					CONVAR( "cl_crosshair_drawoutline"_id ) );
				game_cfg.outline_thickness = std::clamp( game::variables().get<float>(
					CONVAR( "cl_crosshair_outlinethickness"_id ) ), 0.5f, 3.0f );
				const auto use_alpha = game::variables().get<bool>(
					CONVAR( "cl_crosshairusealpha"_id ) );
				const auto alpha = static_cast<std::uint8_t>( use_alpha
					? std::clamp( game::variables().get<int>(
						CONVAR( "cl_crosshairalpha"_id ) ), 0, 255 ) : 255 );
				const auto color_mode = game::variables().get<int>(
					CONVAR( "cl_crosshaircolor"_id ) );
				static constexpr std::array<zdraw::rgba, 5> presets{
					zdraw::rgba{ 255, 0, 0, 255 }, zdraw::rgba{ 0, 255, 0, 255 },
					zdraw::rgba{ 255, 255, 0, 255 }, zdraw::rgba{ 0, 0, 255, 255 },
					zdraw::rgba{ 0, 255, 255, 255 } };
				if ( color_mode >= 0 && color_mode < static_cast<int>( presets.size() ) )
					game_cfg.color = presets[ static_cast<std::size_t>( color_mode ) ];
				else game_cfg.color = {
					static_cast<std::uint8_t>( std::clamp( game::variables().get<int>(
						CONVAR( "cl_crosshaircolor_r"_id ) ), 0, 255 ) ),
					static_cast<std::uint8_t>( std::clamp( game::variables().get<int>(
						CONVAR( "cl_crosshaircolor_g"_id ) ), 0, 255 ) ),
					static_cast<std::uint8_t>( std::clamp( game::variables().get<int>(
						CONVAR( "cl_crosshaircolor_b"_id ) ), 0, 255 ) ), alpha };
				game_cfg.color.a = alpha;
			}
			cfg.sync = false;
			cfg.lines = true;
			cfg.dot = game_cfg.dot;
			cfg.t_style = game_cfg.t_style;

			const auto hud_scale = static_cast<float>( sh ) / 480.0f;
			cfg.length = game_cfg.length * hud_scale;
			cfg.thickness = game_cfg.thickness * hud_scale;

			cfg.gap = ( game_cfg.gap + 5.0f ) * hud_scale;
			cfg.outline = game_cfg.outline;
			cfg.outline_thickness = game_cfg.outline_thickness * hud_scale;
			cfg.color = game_cfg.color;
			cfg.outline_color = { 0, 0, 0, game_cfg.color.a };
		}

		if ( cfg.sync )
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
		if ( cfg.penetration_enabled )
		{

			const auto& ctx = simulation::ballistics().ctx( );
			if ( ctx.valid && game::rules::is_firearm( ctx.weapon_type ) )
			{
				const auto eye_pos = game::camera().origin( );
				const auto view_angles = game::camera().angles( );
				foundation::vec3 forward{};
				view_angles.to_directions( &forward, nullptr, nullptr );

				const auto range = simulation::ballistics().pen( ).get_weapon_data( ).range;
				const auto first_hit = game::collision().trace_ray( eye_pos, eye_pos + forward * range );
				if ( first_hit.hit )
				{

					auto pen_damage{ 0.0f };
					const auto can_pen = simulation::ballistics().pen( ).can( eye_pos, forward, pen_damage );
					crosshair_color = ( can_pen && pen_damage >= cfg.penetration_min_damage )
						? cfg.penetration_color_yes
						: cfg.penetration_color_no;
				}
				else
				{

					crosshair_color = cfg.penetration_color_yes;
				}
			}
		}

		const auto center_x = std::floor( static_cast<float>( sw ) * 0.5f );
		const auto center_y = std::floor( static_cast<float>( sh ) * 0.5f );

		const auto bar = std::max( 1.0f, std::round( cfg.thickness ) );
		const auto center_lo_x = std::round( center_x - bar * 0.5f );
		const auto center_hi_x = center_lo_x + bar;
		const auto center_lo_y = std::round( center_y - bar * 0.5f );
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

}
