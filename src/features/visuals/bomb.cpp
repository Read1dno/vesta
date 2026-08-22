#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>
#include <app/workers.hpp>

namespace features::visuals {

	void bomb_t::tick( )
	{
		const auto& cfg = config::visual_settings.m_bomb;
		bomb_data planted{};
		active_bomb_data active{};
		const auto current_time = game::local_player().game_time( );
		const auto local_health = game::local_player().health( );
		const auto& player_cfg = config::visual_settings.m_player;
		const auto player_damage_requested = player_cfg.enabled
			&& player_cfg.m_info_flags.enabled
			&& player_cfg.m_info_flags.has(
				config::visual_profile::player::info_flags::flag::bomb_damage );
		const auto script_requested = game::world().script_demand(
			game::script_data_demand::bomb );
		const auto active_requested = script_requested
			|| ( cfg.enabled && cfg.show_active_bomb );
		const auto planted_requested = script_requested || player_damage_requested
			|| ( cfg.enabled && ( cfg.show_planted_bomb
				|| cfg.show_info_panel || cfg.show_safe_zone ) );

		if ( current_time > 0.0f && ( active_requested || planted_requested ) )
		{
			const auto raw = game::entity_index().all( );
			for ( const auto& entry : *raw )
			{
				if ( active_requested && !active.valid
					&& entry.schema_id == "C_C4"_id )
				{
					const auto node = app::context().process.load<std::uintptr_t>(
						entry.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
					if ( node )
					{
						active.position = app::context().process.load<foundation::vec3>(
							node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
						active.valid = active.position.length_sqr( ) > 1.0f;
					}
				}

				if ( planted_requested && !planted.planted
					&& entry.schema_id == "C_PlantedC4"_id
					&& app::context().process.load<bool>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_bBombTicking"_id ) ) )
				{
					const auto node = app::context().process.load<std::uintptr_t>(
						entry.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
					if ( !node )
						continue;
					planted.position = app::context().process.load<foundation::vec3>(
						node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
					if ( planted.position.length_sqr( ) < 1.0f )
						continue;
					planted.planted = true;
					planted.blow_time = app::context().process.load<float>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_flC4Blow"_id ) );
					planted.timer_length = app::context().process.load<float>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_flTimerLength"_id ) );
					planted.defuse_length = app::context().process.load<float>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_flDefuseLength"_id ) );
					planted.defuse_countdown = app::context().process.load<float>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_flDefuseCountDown"_id ) );
					planted.being_defused = app::context().process.load<bool>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_bBeingDefused"_id ) );
					planted.bomb_site = app::context().process.load<std::int32_t>( entry.ptr
						+ SCHEMA( "C_PlantedC4", "m_nBombSite"_id ) );
				}
			}
		}

		if ( planted.planted && game::blast_damage( ).valid( ) )
		{
			planted.baked_site = game::blast_damage( ).site_for_position( planted.position );
		}

		if ( planted.planted && local_health > 0 && planted.baked_site >= 0 )
		{
			const auto local = game::local_player( ).view_pawn( );
			const auto scene_node = local ? app::context().process.load<std::uintptr_t>(
				local + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) ) : 0;
			if ( scene_node )
			{
				const auto origin = app::context().process.load<foundation::vec3>(
					scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
				const auto collision = local
					+ SCHEMA( "C_BaseModelEntity", "m_Collision"_id );
				const auto mins = app::context().process.load<foundation::vec3>(
					collision + SCHEMA( "CCollisionProperty", "m_vecMins"_id ) );
				const auto maxs = app::context().process.load<foundation::vec3>(
					collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_id ) );
				const auto movement = app::context().process.load<std::uintptr_t>( local
					+ SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_id ) );
				const auto ducked = movement && app::context().process.load<bool>( movement
					+ SCHEMA( "CCSPlayer_MovementServices", "m_bDucked"_id ) );
				planted.predicted_damage = game::blast_damage( ).predicted_damage(
					origin + ( mins + maxs ) * 0.5f, planted.baked_site,
					game::camera( ).angles( ), ducked );
			}
		}

		std::unique_lock lock( this->m_state_mutex );
		this->m_sampled_planted = planted;
		this->m_sampled_active = active;
		this->m_sampled_game_time = current_time;
		this->m_sampled_health = local_health;
		this->m_sampled_at = std::chrono::steady_clock::now( );
	}

	void bomb_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::visual_settings.m_bomb;
		if ( !cfg.enabled )
		{
			return;
		}

		bomb_data active_planted{};
		active_bomb_data active_bomb{};
		auto current_time = 0.0f;
		auto local_health = 0;
		auto sampled_at = std::chrono::steady_clock::time_point{};
		{
			std::shared_lock lock( this->m_state_mutex );
			active_planted = this->m_sampled_planted;
			active_bomb = this->m_sampled_active;
			current_time = this->m_sampled_game_time;
			local_health = this->m_sampled_health;
			sampled_at = this->m_sampled_at;
		}
		if ( current_time <= 0.0f ) return;
		current_time += std::chrono::duration<float>(
			std::chrono::steady_clock::now( ) - sampled_at ).count( );

		if ( cfg.show_active_bomb && active_bomb.valid )
		{
			const auto screen = game::camera().project( active_bomb.position );
			if ( game::camera().projection_valid( screen ) )
			{
				if ( cfg.display_mode == config::visual_profile::bomb::text_only )
				{
					auto* font = app::context().overlay.fonts( ).esp_text_11;
					if ( font && font->im_font )
					{
						constexpr std::string_view label{ "C4" };
						const auto [ width, height ] = zdraw::measure_text( label, font );
						draw_list.add_text( std::floorf( screen.x - width * 0.5f ),
							std::floorf( screen.y - height * 0.5f ), label, font,
							cfg.active_bomb_color, zdraw::text_style::outlined );
					}
				}
				else if ( auto* icon_base = app::context().overlay.fonts( ).weapons_15;
					icon_base && icon_base->im_font )
				{
					auto icon_font = *icon_base;
					icon_font.font_size = 18.0f;
					constexpr std::string_view icon{ "o" };
					const auto [ icon_w, icon_h ] = zdraw::measure_text( icon, &icon_font );
					draw_list.add_text( std::floorf( screen.x - icon_w * 0.5f ),
						std::floorf( screen.y - icon_h * 0.5f ), icon, &icon_font,
						cfg.active_bomb_color, zdraw::text_style::outlined );
				}
			}
		}

		if ( cfg.show_planted_bomb )
		{
			if ( active_planted.planted )
			{
				this->draw_planted_bomb( draw_list, active_planted, cfg, current_time );

				if ( cfg.show_safe_zone )
				{
					this->draw_safe_zone( draw_list, active_planted, cfg, local_health );
				}
			}
			else
			{
				this->m_zone_site = -1;
				this->m_zone_segments.clear( );
				this->m_zone_polylines.clear( );
				this->m_zone_surfaces.clear( );
				this->m_zone_damage.clear( );
			}
		}
	}

	bomb_t::hud_snapshot bomb_t::info_snapshot( ) const
	{
		bomb_data data{};
		active_bomb_data active{};
		auto current_time = 0.0f;
		auto local_health = 0;
		auto sampled_at = std::chrono::steady_clock::time_point{};
		{
			std::shared_lock lock( this->m_state_mutex );
			data = this->m_sampled_planted;
			active = this->m_sampled_active;
			current_time = this->m_sampled_game_time;
			local_health = this->m_sampled_health;
			sampled_at = this->m_sampled_at;
		}

		hud_snapshot result{};
		result.active = active.valid;
		result.active_position = active.position;
		if ( !data.planted || current_time <= 0.0f )
			return result;
		current_time += std::chrono::duration<float>(
			std::chrono::steady_clock::now( ) - sampled_at ).count( );
		result.planted = true;
		result.time_remaining = std::max( 0.0f, data.blow_time - current_time );
		result.timer_length = std::max( 1.0f, data.timer_length );
		result.being_defused = data.being_defused;
		result.defuse_remaining = data.being_defused
			? std::max( 0.0f, data.defuse_countdown - current_time ) : 0.0f;
		result.defuse_length = std::max( 0.0f, data.defuse_length );
		result.defuse_success = data.being_defused
			&& data.defuse_countdown < data.blow_time;
		result.bomb_site = data.bomb_site;
		result.predicted_damage = data.predicted_damage;
		result.local_health = local_health;
		result.position = data.position;
		return result;
	}

	bomb_t::damage_snapshot bomb_t::player_damage_snapshot( ) const
	{
		std::shared_lock lock( this->m_state_mutex );
		return {
			this->m_sampled_planted.planted
				&& this->m_sampled_planted.baked_site >= 0,
			this->m_sampled_planted.position,
			this->m_sampled_planted.blow_time,
			this->m_sampled_planted.baked_site,
		};
	}

	void bomb_t::draw_planted_bomb( zdraw::draw_list& draw_list, const bomb_data& data, const config::visual_profile::bomb& cfg, float current_time ) const
	{
		const auto screen = game::camera().project( data.position );
		if ( !game::camera().projection_valid( screen ) )
		{
			return;
		}

		if ( cfg.display_mode == config::visual_profile::bomb::text_only )
		{
			auto* font = app::context().overlay.fonts( ).esp_text_11;
			if ( !font || !font->im_font )
				return;

			const auto marker_color = data.being_defused
				? cfg.bomb_color_ct : cfg.bomb_color_t;
			const auto site_name = data.bomb_site == 1 ? "B" : "A";
			const auto label = std::format( "bomb[{}]", site_name );
			const auto [ label_width, label_height ] = zdraw::measure_text( label, font );
			auto y = std::floorf( screen.y - ( cfg.show_timer ? 7.0f : label_height * 0.5f ) );
			draw_list.add_text( std::floorf( screen.x - label_width * 0.5f ), y,
				label, font, marker_color, zdraw::text_style::outlined );

			if ( cfg.show_timer )
			{
				y += std::max( 9.0f, label_height - 2.0f );
				const auto remaining = std::max( 0.0f, data.blow_time - current_time );
				const auto timer_text = std::format( "{:.1f}s", remaining );
				const auto [ timer_width, timer_height ] = zdraw::measure_text( timer_text, font );
				draw_list.add_text( std::floorf( screen.x - timer_width * 0.5f ), y,
					timer_text, font, cfg.timer_text_color, zdraw::text_style::outlined );

				if ( data.being_defused )
				{
					const auto defuse_remaining = std::max( 0.0f,
						data.defuse_countdown - current_time );
					const auto defuse_success = data.defuse_countdown < data.blow_time;
					const auto defuse_text = std::format( "defuse {:.1f}s", defuse_remaining );
					const auto [ defuse_width, defuse_height ] = zdraw::measure_text( defuse_text, font );
					const auto defuse_color = defuse_success ? cfg.bomb_color_ct
						: zdraw::rgba{ 255, 92, 62, 255 };
					draw_list.add_text( std::floorf( screen.x - defuse_width * 0.5f ),
						y + timer_height, defuse_text, font, defuse_color,
						zdraw::text_style::outlined );
				}
			}
			return;
		}

		{
			auto* draw = draw_list.m_im_draw_list;
			auto* icon_base = app::context().overlay.fonts( ).weapons_15;
			auto* timer_base = app::context().overlay.fonts( ).notosans_medium_12;
			if ( !draw || !icon_base || !icon_base->im_font
				|| !timer_base || !timer_base->im_font )
				return;

			const auto marker_color = data.being_defused
				? cfg.bomb_color_ct : cfg.bomb_color_t;
			const auto center = ImVec2{ std::floorf( screen.x ) + 0.5f,
				std::floorf( screen.y ) + 0.5f };
			constexpr auto radius = 14.0f;
			draw->AddCircleFilled( center, radius + 2.0f,
				IM_COL32( 0, 0, 0, 105 ), 32 );
			draw->AddCircleFilled( center, radius,
				zdraw::draw_list::to_im_color( cfg.panel_background ), 32 );
			draw->AddCircle( center, radius,
				IM_COL32( marker_color.r, marker_color.g, marker_color.b,
					static_cast<int>( marker_color.a * 0.34f ) ), 32, 1.0f );

			const auto remaining = std::max( 0.0f, data.blow_time - current_time );
			const auto defuse_remaining = data.being_defused
				? std::max( 0.0f, data.defuse_countdown - current_time ) : 0.0f;
			const auto defuse_success = data.being_defused
				&& data.defuse_countdown < data.blow_time;
			if ( cfg.show_timer )
			{
				const auto fraction = std::clamp( remaining
					/ std::max( 1.0f, data.timer_length ), 0.0f, 1.0f );
				const auto danger = zdraw::rgba{ 238, 68, 68,
					cfg.timer_text_color.a };
				const auto ring_color = this->lerp_color( danger,
					cfg.timer_text_color, fraction );
				constexpr auto ring_radius = radius + 2.5f;
				draw->AddCircle( center, ring_radius,
					IM_COL32( 0, 0, 0, 155 ), 40, 2.4f );
				if ( fraction > 0.002f )
				{
					constexpr auto start = -std::numbers::pi_v<float> * 0.5f;
					draw->PathArcTo( center, ring_radius, start,
						start + 2.0f * std::numbers::pi_v<float> * fraction, 40 );
					draw->PathStroke(
						zdraw::draw_list::to_im_color( ring_color ), 0, 2.4f );
				}

				if ( data.being_defused )
				{
					const auto defuse_fraction = std::clamp( defuse_remaining
						/ std::max( 0.1f, data.defuse_length ), 0.0f, 1.0f );
					const auto defuse_color = defuse_success ? cfg.bomb_color_ct
						: zdraw::rgba{ 255, 92, 62, 255 };
					if ( defuse_fraction > 0.002f )
					{
						constexpr auto start = -std::numbers::pi_v<float> * 0.5f;
						draw->PathArcTo( center, ring_radius, start,
							start + 2.0f * std::numbers::pi_v<float>
								* defuse_fraction, 40 );
						draw->PathStroke(
							zdraw::draw_list::to_im_color( defuse_color ), 0, 3.0f );
					}
				}
			}

			auto icon_font = *icon_base;
			icon_font.font_size = 18.0f;
			const std::string_view icon = data.being_defused ? "r" : "o";
			const auto [ icon_w, icon_h ] = zdraw::measure_text( icon, &icon_font );
			draw_list.add_text( std::floorf( center.x - icon_w * 0.5f ),
				std::floorf( center.y - icon_h * 0.5f ), icon, &icon_font,
				marker_color, zdraw::text_style::outlined );

			if ( cfg.show_timer )
			{
				auto timer_font = *timer_base;
				timer_font.font_size = 12.0f;
				const auto bomb_time = std::format( "{:.1f}s", remaining );
				const auto text_x = std::floorf( center.x + radius + 6.0f );
				const auto bomb_y = std::floorf( center.y
					- ( data.being_defused ? 12.0f : 6.0f ) );
				draw_list.add_text( text_x, bomb_y, bomb_time, &timer_font,
					cfg.timer_text_color, zdraw::text_style::outlined );
				if ( data.being_defused )
				{
					const auto defuse_time = std::format( "D {:.1f}s", defuse_remaining );
					const auto defuse_color = defuse_success ? cfg.bomb_color_ct
						: zdraw::rgba{ 255, 92, 62, 255 };
					draw_list.add_text( text_x, std::floorf( center.y + 1.0f ),
						defuse_time, &timer_font, defuse_color,
						zdraw::text_style::outlined );
				}
			}
		}
	}

	void bomb_t::draw_safe_zone( zdraw::draw_list& draw_list, const bomb_data& data,
		const config::visual_profile::bomb& cfg, int local_health )
	{
		if ( !game::blast_damage().valid( ) )
		{
			return;
		}

		const auto local = game::local_player().view_pawn( );
		if ( !local )
		{
			return;
		}

		if ( local_health <= 0 )
		{
			return;
		}

		const auto site = game::blast_damage().site_for_position( data.position );
		if ( site < 0 )
		{
			return;
		}
		const auto site_info = game::blast_damage().site( site );
		const foundation::vec3 zone_center{
			( site_info.mins.x + site_info.maxs.x ) * 0.5f,
			( site_info.mins.y + site_info.maxs.y ) * 0.5f,
			data.position.z };

		const auto threshold = static_cast<float>( local_health );

		const auto bands = std::clamp( cfg.safe_zone_bands, 1, 8 );

		struct async_zone_state
		{
			std::future<std::unique_ptr<bomb_t>> job{};
			std::string job_map{};
			std::string applied_map{};
		};
		static async_zone_state async_zone{};
		const auto map_snapshot = app::workers::current_map( );
		static const std::string empty_map{};
		const auto& current_map = map_snapshot ? *map_snapshot : empty_map;

		const auto request_matches = [ & ]( const bomb_t& zone )
			{
				return async_zone.applied_map == current_map
					&& zone.m_zone_site == site
					&& zone.m_zone_threshold == threshold
					&& !zone.m_zone_surfaces.empty( )
					&& zone.m_zone_bands == bands
					&& std::abs( zone.m_zone_step - cfg.safe_zone_band_step ) <= 0.5f
					&& std::abs( zone.m_zone_center.z - zone_center.z ) <= 0.5f;
			};

		if ( async_zone.job.valid( )
			&& async_zone.job.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready )
		{
			auto ready = async_zone.job.get( );
			if ( ready && async_zone.job_map == current_map )
			{
				async_zone.applied_map = current_map;
				if ( request_matches( *ready ) )
				{
					this->m_zone_segments = std::move( ready->m_zone_segments );
					this->m_zone_polylines = std::move( ready->m_zone_polylines );
					this->m_zone_surfaces = std::move( ready->m_zone_surfaces );
					this->m_zone_damage = std::move( ready->m_zone_damage );
					this->m_zone_center = ready->m_zone_center;
					this->m_zone_threshold = ready->m_zone_threshold;
					this->m_zone_site = ready->m_zone_site;
					this->m_zone_grid_min_x = ready->m_zone_grid_min_x;
					this->m_zone_grid_min_y = ready->m_zone_grid_min_y;
					this->m_zone_grid_width = ready->m_zone_grid_width;
					this->m_zone_grid_height = ready->m_zone_grid_height;
					this->m_zone_bands = ready->m_zone_bands;
					this->m_zone_step = ready->m_zone_step;
				}
			}
		}

		if ( !request_matches( *this ) )
		{
			if ( !async_zone.job.valid( ) )
			{
				async_zone.job_map = current_map;
				async_zone.job = std::async( std::launch::async,
					[ zone_center, site, threshold, bands, step = cfg.safe_zone_band_step ]
					{
						auto built = std::make_unique<bomb_t>( );
						built->rebuild_safe_zone( zone_center,
							site, threshold, bands, step );
						return built;
					} );
			}

			const auto compatible_published_zone = async_zone.applied_map == current_map
				&& this->m_zone_site == site
				&& !this->m_zone_polylines.empty( )
				&& std::abs( this->m_zone_center.z - zone_center.z ) <= 0.5f;
			if ( !compatible_published_zone ) return;
		}

		const auto local_origin = game::camera().origin( );
		const auto [ display_width, display_height ] = zdraw::get_display_size( );
		const auto on_screen = [ & ]( const foundation::vec2& point )
			{
				constexpr auto margin = 48.0f;
				return game::camera().projection_valid( point ) && point.x >= -margin && point.y >= -margin &&
					point.x <= static_cast<float>( display_width ) + margin &&
					point.y <= static_cast<float>( display_height ) + margin;
			};

		const auto draw_radius = std::clamp( cfg.safe_zone_draw_radius, 200.0f, 2500.0f );
		const auto draw_radius_sqr = draw_radius * draw_radius;

		for ( std::size_t line_index = 0; line_index < this->m_zone_polylines.size( ); ++line_index )
		{
			const auto& line = this->m_zone_polylines[ line_index ];
			const auto render_bands = std::max( 1, this->m_zone_bands );
			const auto progress = render_bands > 1
				? static_cast< float >( line.band ) / static_cast< float >( render_bands - 1 )
				: 1.0f;
			const auto alpha_scale = 0.12f + progress * 0.88f;
			const auto alpha = static_cast< std::uint8_t >( static_cast< float >( cfg.safe_zone_color.a ) * alpha_scale );
			const auto color = zdraw::rgba( cfg.safe_zone_color.r, cfg.safe_zone_color.g, cfg.safe_zone_color.b, alpha );
			const auto main_boundary = line.band == render_bands - 1;
			const auto draw_chunk = [ & ]( bool closed )
				{
					if ( this->m_zone_screen_points.size( ) < 4 ) return;
					if ( main_boundary )
					{
						const auto outer = zdraw::rgba( color.r, color.g, color.b,
							static_cast<std::uint8_t>( static_cast<float>( color.a ) * 0.10f ) );
						const auto inner = zdraw::rgba( color.r, color.g, color.b,
							static_cast<std::uint8_t>( static_cast<float>( color.a ) * 0.24f ) );
						draw_list.add_polyline( this->m_zone_screen_points, outer, closed, 10.0f );
						draw_list.add_polyline( this->m_zone_screen_points, inner, closed, 6.0f );
						draw_list.add_polyline( this->m_zone_screen_points, color, closed, 3.4f );
					}
					else
					{
						draw_list.add_polyline( this->m_zone_screen_points, color, closed, 1.4f );
					}
				};

			this->m_zone_screen_points.clear( );
			auto all_visible = true;
			for ( std::size_t point_index = 0; point_index < line.points.size( ); ++point_index )
			{
				const auto& world = line.points[ point_index ];
				const auto dx = local_origin.x - world.x;
				const auto dy = local_origin.y - world.y;
				const auto within_range = dx * dx + dy * dy <= draw_radius_sqr;
				const auto screen = within_range ? game::camera().project( world ) : foundation::vec2{};
				if ( within_range && on_screen( screen ) )
				{
					this->m_zone_screen_points.push_back( screen.x );
					this->m_zone_screen_points.push_back( screen.y );
					continue;
				}

				all_visible = false;
				draw_chunk( false );
				this->m_zone_screen_points.clear( );
			}

			draw_chunk( line.closed && all_visible );
		}
	}

	void bomb_t::rebuild_safe_zone( const foundation::vec3& center,
		std::int32_t site, float threshold, int bands, float band_step )
	{
		this->m_zone_segments.clear( );
		this->m_zone_polylines.clear( );
		this->m_zone_threshold = threshold;
		this->m_zone_bands = bands;
		this->m_zone_step = band_step;

#if 0

		const auto grid = game::blast_damage().bounds( );
		if ( !grid.valid )
		{
			this->m_zone_site = -1;
			return;
		}
		const auto grid_width = static_cast< int >( grid.max_x - grid.min_x + 1 );
		const auto grid_height = static_cast< int >( grid.max_y - grid.min_y + 1 );

		const auto refresh_samples = this->m_zone_surfaces.empty( )
			|| this->m_zone_site != site
			|| this->m_zone_grid_min_x != grid.min_x
			|| this->m_zone_grid_min_y != grid.min_y
			|| this->m_zone_grid_width != grid_width
			|| this->m_zone_grid_height != grid_height;

		if ( refresh_samples )
		{
			if ( !game::blast_damage().sample_grid_surfaces(
				grid.min_x, grid.min_y, grid_width, grid_height,
				site, reference_position, this->m_zone_surfaces ) )
			{
				this->m_zone_surfaces.clear( );
				this->m_zone_site = -1;
				return;
			}

			this->m_zone_center = center;
			this->m_zone_site = site;
			this->m_zone_grid_min_x = grid.min_x;
			this->m_zone_grid_min_y = grid.min_y;
			this->m_zone_grid_width = grid_width;
			this->m_zone_grid_height = grid_height;
		}

		static constexpr std::int8_t k_cases[ 16 ][ 4 ]{
			{ -1, -1, -1, -1 }, { 3, 0, -1, -1 }, { 0, 1, -1, -1 }, { 3, 1, -1, -1 },
			{ 1, 2, -1, -1 },   { 3, 2, 0, 1 },   { 0, 2, -1, -1 }, { 3, 2, -1, -1 },
			{ 2, 3, -1, -1 },   { 0, 2, -1, -1 }, { 0, 3, 1, 2 },   { 1, 2, -1, -1 },
			{ 1, 3, -1, -1 },   { 0, 1, -1, -1 }, { 3, 0, -1, -1 }, { -1, -1, -1, -1 },
		};

		static constexpr int k_edge_corners[ 4 ][ 2 ]{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
		const auto surface_node_count = static_cast<std::uint32_t>(
			static_cast<std::size_t>( grid_width ) * grid_height * 2 );
		const auto edge_node = [ grid_width, surface_node_count ](
			std::size_t surface, int edge, int ix, int iy ) -> std::uint32_t
			{
				int x = ix;
				int y = iy;
				int vertical = 0;
				if ( edge == 1 )
				{
					++x;
					vertical = 1;
				}
				else if ( edge == 2 )
				{
					++y;
				}
				else if ( edge == 3 )
				{
					vertical = 1;
				}
				return static_cast<std::uint32_t>( surface ) * surface_node_count
					+ static_cast<std::uint32_t>( ( y * grid_width + x ) * 2 + vertical );
			};

		for ( int band = 0; band < bands; ++band )
		{

			const auto iso = threshold - static_cast< float >( bands - 1 - band ) * band_step;
			if ( iso <= 0.0f )
			{
				continue;
			}

			for ( std::size_t surface = 0; surface < this->m_zone_surfaces.size( ); ++surface )
			{
				const auto& samples = this->m_zone_surfaces[ surface ];
				if ( !game::blast_damage().evaluate_grid_worst_case( samples, this->m_zone_damage ) )
					continue;
				for ( int iy = 0; iy + 1 < grid_height; ++iy )
				{
					for ( int ix = 0; ix + 1 < grid_width; ++ix )
					{
						const float v[ 4 ]{
						this->m_zone_damage[ static_cast< std::size_t >( iy ) * grid_width + ix ],
						this->m_zone_damage[ static_cast< std::size_t >( iy ) * grid_width + ix + 1 ],
						this->m_zone_damage[ static_cast< std::size_t >( iy + 1 ) * grid_width + ix + 1 ],
						this->m_zone_damage[ static_cast< std::size_t >( iy + 1 ) * grid_width + ix ] };

					if ( std::isnan( v[ 0 ] ) || std::isnan( v[ 1 ] ) || std::isnan( v[ 2 ] ) || std::isnan( v[ 3 ] ) )
					{
						continue;
					}

					const auto mask = ( v[ 0 ] >= iso ? 1 : 0 ) | ( v[ 1 ] >= iso ? 2 : 0 ) | ( v[ 2 ] >= iso ? 4 : 0 ) | ( v[ 3 ] >= iso ? 8 : 0 );
					const auto* edges = k_cases[ mask ];

					if ( edges[ 0 ] < 0 )
					{
						continue;
					}

					const float cz[ 4 ]{
						samples[ static_cast< std::size_t >( iy ) * grid_width + ix ].z,
						samples[ static_cast< std::size_t >( iy ) * grid_width + ix + 1 ].z,
						samples[ static_cast< std::size_t >( iy + 1 ) * grid_width + ix + 1 ].z,
						samples[ static_cast< std::size_t >( iy + 1 ) * grid_width + ix ].z };

					const float corner_x[ 4 ]{
						( static_cast< float >( grid.min_x + ix ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_x + ix + 1 ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_x + ix + 1 ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_x + ix ) ) * 10.0f + 5.0f };
					const float corner_y[ 4 ]{
						( static_cast< float >( grid.min_y + iy ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_y + iy ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_y + iy + 1 ) ) * 10.0f + 5.0f,
						( static_cast< float >( grid.min_y + iy + 1 ) ) * 10.0f + 5.0f };

					const auto edge_point = [ & ]( int edge ) -> foundation::vec3
						{
							const auto a = k_edge_corners[ edge ][ 0 ];
							const auto b = k_edge_corners[ edge ][ 1 ];

							auto t = ( iso - v[ a ] ) / ( v[ b ] - v[ a ] );
							t = std::clamp( t, 0.0f, 1.0f );

							return {
								corner_x[ a ] + ( corner_x[ b ] - corner_x[ a ] ) * t,
								corner_y[ a ] + ( corner_y[ b ] - corner_y[ a ] ) * t,
								cz[ a ] + ( cz[ b ] - cz[ a ] ) * t + 3.0f };
						};

					for ( int e = 0; e + 1 < 4 && edges[ e ] >= 0; e += 2 )
					{
						const auto edge_a = edges[ e ];
						const auto edge_b = edges[ e + 1 ];
						const auto edge_is_continuous = [ & ]( int edge )
							{
								const auto a = k_edge_corners[ edge ][ 0 ];
								const auto b = k_edge_corners[ edge ][ 1 ];
								return std::abs( cz[ a ] - cz[ b ] ) <= 96.0f;
							};

						if ( !edge_is_continuous( edge_a ) || !edge_is_continuous( edge_b ) ) continue;
						this->m_zone_segments.push_back( {
							edge_point( edge_a ), edge_point( edge_b ),
							edge_node( surface, edge_a, ix, iy ),
							edge_node( surface, edge_b, ix, iy ),
							static_cast< std::uint8_t >( band ) } );
					}
				}
			}
		}
		}
#endif

		const auto minimum_iso = std::max( 0.0f,
			threshold - static_cast<float>( bands - 1 ) * band_step );
		if ( !game::blast_damage().sample_grid_surfaces(
			site, minimum_iso, threshold, this->m_zone_surfaces ) )
		{
			this->m_zone_surfaces.clear( );
			this->m_zone_site = -1;
			return;
		}

		this->m_zone_center = center;
		this->m_zone_site = site;
		const auto grid = game::blast_damage().bounds( );
		this->m_zone_grid_min_x = grid.min_x;
		this->m_zone_grid_min_y = grid.min_y;
		this->m_zone_grid_width = grid.valid ? grid.max_x - grid.min_x + 1 : 0;
		this->m_zone_grid_height = grid.valid ? grid.max_y - grid.min_y + 1 : 0;

		static constexpr std::int8_t k_cases[ 16 ][ 4 ]{
			{ -1, -1, -1, -1 }, { 3, 0, -1, -1 }, { 0, 1, -1, -1 }, { 3, 1, -1, -1 },
			{ 1, 2, -1, -1 },   { 3, 2, 0, 1 },   { 0, 2, -1, -1 }, { 3, 2, -1, -1 },
			{ 2, 3, -1, -1 },   { 0, 2, -1, -1 }, { 0, 3, 1, 2 },   { 1, 2, -1, -1 },
			{ 1, 3, -1, -1 },   { 0, 1, -1, -1 }, { 3, 0, -1, -1 }, { -1, -1, -1, -1 },
		};
		static constexpr int k_edge_corners[ 4 ][ 2 ]{
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };

		std::unordered_map<std::uint64_t, std::uint32_t> edge_nodes{};
		edge_nodes.reserve( this->m_zone_surfaces.size( ) * 2 );
		const auto edge_node = [ & ]( const game::blast_model::grid_quad& quad,
			const int edge ) -> std::uint32_t
			{
				const auto a = quad.point_indices[ k_edge_corners[ edge ][ 0 ] ];
				const auto b = quad.point_indices[ k_edge_corners[ edge ][ 1 ] ];
				const auto low = std::min( a, b );
				const auto high = std::max( a, b );
				const auto key = ( static_cast<std::uint64_t>( low ) << 32 ) | high;
				const auto [ it, inserted ] = edge_nodes.try_emplace(
					key, static_cast<std::uint32_t>( edge_nodes.size( ) ) );
				return it->second;
			};

		for ( int band = 0; band < bands; ++band )
		{
			const auto iso = threshold
				- static_cast<float>( bands - 1 - band ) * band_step;
			if ( iso <= 0.0f ) continue;

			for ( const auto& quad : this->m_zone_surfaces )
			{
				const auto& v = quad.damage;
				const auto mask = ( v[ 0 ] >= iso ? 1 : 0 )
					| ( v[ 1 ] >= iso ? 2 : 0 )
					| ( v[ 2 ] >= iso ? 4 : 0 )
					| ( v[ 3 ] >= iso ? 8 : 0 );
				const auto* edges = k_cases[ mask ];
				if ( edges[ 0 ] < 0 ) continue;

				const float corner_x[ 4 ]{
					static_cast<float>( quad.cell_x ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_x + 1 ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_x + 1 ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_x ) * 10.0f + 5.0f };
				const float corner_y[ 4 ]{
					static_cast<float>( quad.cell_y ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_y ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_y + 1 ) * 10.0f + 5.0f,
					static_cast<float>( quad.cell_y + 1 ) * 10.0f + 5.0f };

				const auto edge_point = [ & ]( const int edge )
					{
						const auto a = k_edge_corners[ edge ][ 0 ];
						const auto b = k_edge_corners[ edge ][ 1 ];
						const auto denominator = v[ b ] - v[ a ];
						auto t = std::abs( denominator ) > 0.001f
							? ( iso - v[ a ] ) / denominator : 0.5f;
						t = std::clamp( t, 0.0f, 1.0f );
						return foundation::vec3{
							corner_x[ a ] + ( corner_x[ b ] - corner_x[ a ] ) * t,
							corner_y[ a ] + ( corner_y[ b ] - corner_y[ a ] ) * t,
							quad.samples[ a ].z
								+ ( quad.samples[ b ].z - quad.samples[ a ].z ) * t + 3.0f };
					};

				for ( int e = 0; e + 1 < 4 && edges[ e ] >= 0; e += 2 )
				{
					const auto edge_a = edges[ e ];
					const auto edge_b = edges[ e + 1 ];
					this->m_zone_segments.push_back( {
						edge_point( edge_a ), edge_point( edge_b ),
						edge_node( quad, edge_a ), edge_node( quad, edge_b ),
						static_cast<std::uint8_t>( band ) } );
				}
			}
		}

		const auto node_count = edge_nodes.size( );
		std::vector<std::array<std::int32_t, 2>> node_segments(
			node_count, std::array<std::int32_t, 2>{ -1, -1 } );
		std::vector<std::uint8_t> node_degrees( node_count );
		std::vector<bool> visited( this->m_zone_segments.size( ) );

		for ( int band = 0; band < bands; ++band )
		{
			std::ranges::fill( node_segments, std::array<std::int32_t, 2>{ -1, -1 } );
			std::ranges::fill( node_degrees, 0 );

			for ( std::size_t i = 0; i < this->m_zone_segments.size( ); ++i )
			{
				const auto& segment = this->m_zone_segments[ i ];
				if ( segment.band != band )
				{
					continue;
				}

				for ( const auto node : { segment.node_a, segment.node_b } )
				{
					auto& degree = node_degrees[ node ];
					if ( degree < 2 )
					{
						node_segments[ node ][ degree++ ] = static_cast< std::int32_t >( i );
					}
				}
			}

			const auto emit_line = [ & ]( std::size_t seed, std::uint32_t start_node )
				{
					zone_polyline line{};
					line.band = static_cast< std::uint8_t >( band );
					line.points.reserve( 64 );

					auto segment_index = seed;
					auto node = start_node;
					const auto& first = this->m_zone_segments[ segment_index ];
					line.points.push_back( node == first.node_a ? first.a : first.b );

					while ( !visited[ segment_index ] )
					{
						const auto& segment = this->m_zone_segments[ segment_index ];
						const auto other_node = node == segment.node_a ? segment.node_b : segment.node_a;
						const auto& other_point = node == segment.node_a ? segment.b : segment.a;
						visited[ segment_index ] = true;

						if ( other_node == start_node )
						{
							line.closed = true;
							break;
						}

						line.points.push_back( other_point );
						auto next = -1;
						for ( std::uint8_t i = 0; i < node_degrees[ other_node ]; ++i )
						{
							const auto candidate = node_segments[ other_node ][ i ];
							if ( candidate >= 0 && !visited[ static_cast< std::size_t >( candidate ) ] )
							{
								next = candidate;
								break;
							}
						}

						if ( next < 0 )
						{
							break;
						}
						node = other_node;
						segment_index = static_cast< std::size_t >( next );
					}

					if ( line.points.size( ) >= 2 )
					{
						this->m_zone_polylines.push_back( std::move( line ) );
					}
				};

			for ( std::size_t i = 0; i < this->m_zone_segments.size( ); ++i )
			{
				const auto& segment = this->m_zone_segments[ i ];
				if ( segment.band != band || visited[ i ] )
				{
					continue;
				}
				if ( node_degrees[ segment.node_a ] == 1 )
				{
					emit_line( i, segment.node_a );
				}
				else if ( node_degrees[ segment.node_b ] == 1 )
				{
					emit_line( i, segment.node_b );
				}
			}

			for ( std::size_t i = 0; i < this->m_zone_segments.size( ); ++i )
			{
				const auto& segment = this->m_zone_segments[ i ];
				if ( segment.band == band && !visited[ i ] )
				{
					emit_line( i, segment.node_a );
				}
			}
		}
	}

	zdraw::rgba bomb_t::lerp_color( const zdraw::rgba& a, const zdraw::rgba& b, float t ) const
	{
		return zdraw::rgba
		(
			static_cast< std::uint8_t >( a.r + ( b.r - a.r ) * t ),
			static_cast< std::uint8_t >( a.g + ( b.g - a.g ) * t ),
			static_cast< std::uint8_t >( a.b + ( b.b - a.b ) * t ),
			static_cast< std::uint8_t >( a.a + ( b.a - a.a ) * t )
		);
	}

}
