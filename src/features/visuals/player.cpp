#include <stdafx.hpp>
#include <render/chams/renderer.hpp>
#include <features/visuals/visuals.hpp>

namespace features::visuals {
	namespace {
		struct meter_geometry
		{
			float x{};
			float y{};
			float width{};
			float height{};
			float fill{};
			bool vertical{};
		};

		template <typename Style, typename Layout>
		[[nodiscard]] meter_geometry paint_meter_body( zdraw::draw_list& draw_list,
			const game::bounds_projector::data& bounds, float fraction,
			const Style& style, const Layout& layout, bool vertical )
		{
			const auto thickness = std::clamp( style.thickness, 1.0f, 12.0f ) * layout.scale;
			meter_geometry result{};
			result.vertical = vertical;
			result.width = vertical ? thickness : std::floor( bounds.width( ) * layout.scale );
			result.height = vertical ? std::floor( bounds.height( ) * layout.scale ) : thickness;

			const auto center_x = config::visual_profile::player::resolve_layout_axis(
				bounds.min.x, bounds.max.x, layout.x,
				config::visual_profile::player::layout_reference_width );
			const auto center_y = config::visual_profile::player::resolve_layout_axis(
				bounds.min.y, bounds.max.y, layout.y,
				config::visual_profile::player::layout_reference_height );
			result.x = std::floor( center_x - result.width * 0.5f );
			result.y = std::floor( center_y - result.height * 0.5f );
			result.fill = std::floor( ( vertical ? result.height : result.width )
				* std::clamp( fraction, 0.0f, 1.0f ) );

			if ( style.outline )
			{
				const auto outline = std::clamp( style.outline_thickness, 0.5f, 4.0f ) * layout.scale;
				draw_list.add_rect_filled( result.x - outline, result.y - outline,
					result.width + outline * 2.0f, result.height + outline * 2.0f,
					style.outline_color );
			}
			draw_list.add_rect_filled( result.x, result.y, result.width, result.height,
				style.background_color );

			if ( result.fill > 0.0f )
			{
				const auto fill_x = result.x;
				const auto fill_y = vertical ? result.y + result.height - result.fill : result.y;
				const auto fill_width = vertical ? result.width : result.fill;
				const auto fill_height = vertical ? result.fill : result.height;
				if ( style.gradient )
				{
					if ( vertical )
						draw_list.add_rect_filled_multi_color( fill_x, fill_y, fill_width, fill_height,
							style.full_color, style.full_color, style.low_color, style.low_color );
					else
						draw_list.add_rect_filled_multi_color( fill_x, fill_y, fill_width, fill_height,
							style.low_color, style.full_color, style.full_color, style.low_color );
				}
				else
				{
					draw_list.add_rect_filled( fill_x, fill_y, fill_width, fill_height,
						style.full_color );
				}
			}

			const auto segments = std::clamp( style.segments, 1, 10 );
			const auto gap = std::clamp( style.segment_gap, 0.0f, 4.0f ) * layout.scale;
			if ( segments > 1 && gap > 0.0f )
			{
				const auto separator = style.outline ? style.outline_color : style.background_color;
				const auto extent = vertical ? result.height : result.width;
				for ( int segment = 1; segment < segments; ++segment )
				{
					const auto offset = std::floor( extent * static_cast<float>( segment ) / segments );
					if ( vertical )
						draw_list.add_rect_filled( result.x, result.y + offset - gap * 0.5f,
							result.width, gap, separator );
					else
						draw_list.add_rect_filled( result.x + offset - gap * 0.5f, result.y,
							gap, result.height, separator );
				}
			}
			return result;
		}

		enum class status_icon : std::uint8_t
		{
			money, armor, kit, scoped, defusing, flashed, ping, distance, bomb
		};

		void paint_status_icon( zdraw::draw_list& draw_list, status_icon icon,
			float x, float y, float size, zdraw::rgba color )
		{
			const auto s = std::max( 7.0f, size );
			const auto cx = x + s * 0.5f;
			const auto cy = y + s * 0.5f;
			const auto line = std::max( 1.0f, s / 9.0f );
			const zdraw::rgba outline{ 0, 0, 0, color.a };
			const auto stroke = [&]( float x0, float y0, float x1, float y1 )
			{
				draw_list.add_line( x0, y0, x1, y1, outline, line + 2.0f );
				draw_list.add_line( x0, y0, x1, y1, color, line );
			};

			switch ( icon )
			{
			case status_icon::kit:
			case status_icon::defusing:

				stroke( x + s * 0.24f, y + s * 0.18f, x + s * 0.76f, y + s * 0.82f );
				stroke( x + s * 0.76f, y + s * 0.18f, x + s * 0.24f, y + s * 0.82f );
				draw_list.add_circle( x + s * 0.22f, y + s * 0.82f, s * 0.13f, outline, 12, line + 2.0f );
				draw_list.add_circle( x + s * 0.78f, y + s * 0.82f, s * 0.13f, outline, 12, line + 2.0f );
				draw_list.add_circle( x + s * 0.22f, y + s * 0.82f, s * 0.13f, color, 12, line );
				draw_list.add_circle( x + s * 0.78f, y + s * 0.82f, s * 0.13f, color, 12, line );
				break;
			case status_icon::scoped:
				draw_list.add_circle( cx, cy, s * 0.30f, outline, 16, line + 2.0f );
				draw_list.add_circle( cx, cy, s * 0.30f, color, 16, line );
				stroke( cx - s * 0.45f, cy, cx + s * 0.45f, cy );
				stroke( cx, cy - s * 0.45f, cx, cy + s * 0.45f );
				break;
			case status_icon::flashed:
				draw_list.add_circle_filled( cx, cy, s * 0.20f, color, 16 );
				for ( int ray = 0; ray < 8; ++ray )
				{
					const auto angle = static_cast<float>( ray ) * std::numbers::pi_v<float> / 4.0f;
					stroke( cx + std::cos( angle ) * s * 0.30f, cy + std::sin( angle ) * s * 0.30f,
						cx + std::cos( angle ) * s * 0.46f, cy + std::sin( angle ) * s * 0.46f );
				}
				break;
			case status_icon::bomb:
				draw_list.add_circle( cx, y + s * 0.60f, s * 0.28f, outline, 16, line + 2.0f );
				draw_list.add_circle( cx, y + s * 0.60f, s * 0.28f, color, 16, line );
				stroke( cx, y + s * 0.30f, x + s * 0.72f, y + s * 0.08f );
				break;
			case status_icon::armor:
				stroke( cx, y + s * 0.08f, x + s * 0.82f, y + s * 0.25f );
				stroke( x + s * 0.82f, y + s * 0.25f, x + s * 0.72f, y + s * 0.72f );
				stroke( x + s * 0.72f, y + s * 0.72f, cx, y + s * 0.92f );
				stroke( cx, y + s * 0.92f, x + s * 0.28f, y + s * 0.72f );
				stroke( x + s * 0.28f, y + s * 0.72f, x + s * 0.18f, y + s * 0.25f );
				stroke( x + s * 0.18f, y + s * 0.25f, cx, y + s * 0.08f );
				break;
			default:

				draw_list.add_circle( cx, cy, s * 0.35f, outline, 16, line + 2.0f );
				draw_list.add_circle( cx, cy, s * 0.35f, color, 16, line );
				stroke( cx, cy, cx + s * 0.22f, cy - s * 0.22f );
				break;
			}
		}
	}

	void player_t::render( zdraw::draw_list& draw_list,
		const std::shared_ptr<const game::player_pose_frame>& frame )
	{
		const auto& cfg = config::visual_settings.m_player;
		if ( !cfg.active( ) || !frame || !frame->world )
		{
			return;
		}
		if ( cfg.spectator_sync
			&& game::world( ).local_spectated( ) ) return;

		const auto current_time = game::local_player().game_time( );
		if ( current_time <= 0.0f )
		{
			return;
		}

		active_bomb_info bomb_info{};
		if ( cfg.m_info_flags.enabled &&
			cfg.m_info_flags.has( config::visual_profile::player::info_flags::flag::bomb_damage ) )
		{
			const auto sampled = features::visuals::bomb( ).player_damage_snapshot( );
			bomb_info.valid = sampled.valid;
			bomb_info.position = sampled.position;
			bomb_info.blow_time = sampled.blow_time;
			bomb_info.baked_site = sampled.baked_site;
		}

		for ( const auto& pose : frame->players )
		{
			if ( pose.source_index >= frame->world->size( ) ) continue;
			const auto& player = ( *frame->world )[ pose.source_index ];
			if ( !game::local_player().is_enemy( player.team ) )
			{
				continue;
			}
			if ( !player.legit_visible ) continue;

			if ( !pose.bones.is_valid( ) )
			{
				continue;
			}

			const auto opacity = player.legit_opacity
				* ( player.invulnerable ? 0.45f : 1.0f );
			zdraw::draw_list player_draw{ draw_list.m_im_draw_list, opacity };
			this->paint_threat_highlights(
				player_draw, pose.bones, player, current_time );
		}

		for ( const auto& pose : frame->players )
		{
			if ( pose.source_index >= frame->world->size( ) ) continue;
			const auto& player = ( *frame->world )[ pose.source_index ];
			if ( !game::local_player().is_enemy( player.team ) )
			{
				continue;
			}
			if ( !player.legit_visible ) continue;

			const auto& bones = pose.bones;
			if ( !bones.is_valid( ) )
			{
				continue;
			}

			const auto bounds = game::projection_bounds().get( bones );
			if ( !bounds.is_valid( ) )
			{
				continue;
			}
			const auto opacity = player.legit_opacity
				* ( player.invulnerable ? 0.45f : 1.0f );
			zdraw::draw_list player_draw{ draw_list.m_im_draw_list, opacity };
			zdraw::draw_list legit_bright_draw{
				draw_list.m_im_draw_list, player.legit_opacity };

			if ( cfg.m_box.enabled )
			{
				this->paint_frame( player_draw, bounds, cfg.m_box, player.is_visible );
			}

			if ( cfg.m_skeleton.enabled )
			{
				this->paint_skeleton( player_draw, bones, cfg.m_skeleton, player.is_visible );
			}

			if ( cfg.m_head_circle.enabled )
			{
				this->paint_head_marker( player_draw, bones, player, cfg.m_head_circle );
			}

			if ( cfg.m_view_line.enabled )
			{
				this->paint_view_direction( player_draw, bones, player, cfg.m_view_line );
			}

			if ( cfg.m_health_bar.enabled )
			{
				this->paint_health_meter( player_draw, bounds, player, cfg.m_health_bar );
			}

			if ( cfg.m_armor_bar.enabled && player.armor > 0 )
			{
				this->paint_armor_meter( player_draw, bounds, player, cfg.m_armor_bar );
			}

			if ( cfg.m_name.enabled && !player.display_name.empty( ) )
			{
				this->paint_identity( player_draw, bounds, player, cfg.m_name );
			}

			if ( cfg.m_weapon.enabled && !player.weapon.name.empty( ) )
			{
				this->paint_weapon_label( player_draw, bounds, player, cfg.m_weapon );
			}

			if ( cfg.m_info_flags.enabled || player.invulnerable )
			{
				this->paint_status_labels( player_draw, legit_bright_draw,
					bounds, player, cfg, bomb_info, current_time );
			}
		}

		if ( cfg.m_offscreen_arrows.enabled )
		{
			this->paint_offscreen_markers( draw_list, *frame->world, cfg.m_offscreen_arrows );
		}
	}

	void player_t::paint_offscreen_markers( zdraw::draw_list& draw_list, const std::vector<game::player_snapshot>& players, const config::visual_profile::player::offscreen_arrows& cfg )
	{
		const auto display = zdraw::get_display_size( );
		const auto center_x = static_cast<float>( display.first ) * 0.5f;
		const auto center_y = static_cast<float>( display.second ) * 0.5f;

		const auto size = std::max( 6.0f, cfg.size );

		const auto max_radius = std::max( 20.0f, std::min( center_x, center_y ) - size * 2.0f );
		const auto radius = std::clamp( cfg.radius, 20.0f, max_radius );

		const auto m = game::camera().matrix( );
		if ( m[ 3 ][ 3 ] == 0.0f )
		{
			return;
		}

		static const auto animation_start = std::chrono::steady_clock::now( );
		const auto seconds = std::chrono::duration<double>(
			std::chrono::steady_clock::now( ) - animation_start ).count( );
		const auto phase = 0.5f - 0.5f * static_cast<float>( std::cos(
			seconds * static_cast<double>( std::max( 0.05f, cfg.bloom_speed ) )
				* 2.0 * std::numbers::pi_v<double> ) );
		const auto smooth_phase = phase * phase * ( 3.0f - 2.0f * phase );
		const auto bloom_min = std::clamp( cfg.bloom_min_alpha, 0.0f, 1.0f );
		const auto bloom_max = std::max( bloom_min,
			std::clamp( cfg.bloom_max_alpha, 0.0f, 1.0f ) );
		const auto bloom_intensity = std::lerp(
			bloom_min, bloom_max, smooth_phase );
		const auto bloom_alpha = static_cast<std::uint8_t>( std::clamp(
			static_cast<float>( cfg.bloom_color.a ) * bloom_intensity, 0.0f, 255.0f ) );
		const zdraw::rgba arrow_color{ cfg.color.r, cfg.color.g, cfg.color.b, 255 };

		for ( const auto& player : players )
		{
			if ( !game::local_player().is_enemy( player.team ) || player.health <= 0
				|| !player.legit_visible )
			{
				continue;
			}

			const auto& target = player.collision_center;
			const auto clip_x = m[ 0 ][ 0 ] * target.x + m[ 0 ][ 1 ] * target.y + m[ 0 ][ 2 ] * target.z + m[ 0 ][ 3 ];
			const auto clip_y = m[ 1 ][ 0 ] * target.x + m[ 1 ][ 1 ] * target.y + m[ 1 ][ 2 ] * target.z + m[ 1 ][ 3 ];
			const auto clip_w = m[ 3 ][ 0 ] * target.x + m[ 3 ][ 1 ] * target.y + m[ 3 ][ 2 ] * target.z + m[ 3 ][ 3 ];
			if ( clip_w == 0.0f )
			{
				continue;
			}

			auto ndc_x = clip_x / clip_w;
			auto ndc_y = clip_y / clip_w;
			const bool behind = clip_w < 0.0f;

			if ( !behind && std::abs( ndc_x ) <= 1.0f && std::abs( ndc_y ) <= 1.0f )
			{
				continue;
			}

			if ( behind )
			{
				ndc_x = -ndc_x;
				ndc_y = -ndc_y;
			}

			auto dir_x = ndc_x;
			auto dir_y = -ndc_y;
			const auto length = std::sqrt( dir_x * dir_x + dir_y * dir_y );
			if ( !std::isfinite( length ) || length < 1e-4f )
			{
				continue;
			}
			dir_x /= length;
			dir_y /= length;

			const auto anchor_x = center_x + dir_x * radius;
			const auto anchor_y = center_y + dir_y * radius;
			const auto perp_x = -dir_y;
			const auto perp_y = dir_x;
			const auto point = [ & ]( float forward, float side ) -> ImVec2
			{
				return {
					anchor_x + dir_x * forward + perp_x * side,
					anchor_y + dir_y * forward + perp_y * side };
			};
			auto* canvas = draw_list.m_im_draw_list;
			if ( !canvas ) continue;
			const auto saved_flags = canvas->Flags;
			canvas->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

			const auto tip_forward = size * 0.78f;
			const auto base_forward = -size * 0.52f;
			const auto tip = point( tip_forward, 0.0f );
			const auto left_base = point( base_forward, -size * 0.54f );
			const auto right_base = point( base_forward, size * 0.54f );
			const auto notch = point( base_forward + ( tip_forward - base_forward ) / 4.0f, 0.0f );

			const std::array<ImVec2, 4> perimeter{ tip, right_base, notch, left_base };

			auto player_arrow = arrow_color;
			if ( player.invulnerable )
			{
				const auto muted = static_cast<std::uint8_t>(
					( static_cast<unsigned>( player_arrow.r ) + player_arrow.g
						+ player_arrow.b ) / 3u * 0.72f );
				player_arrow.r = player_arrow.g = player_arrow.b = muted;
			}

			player_arrow.a = static_cast<std::uint8_t>( std::clamp(
				255.0f * player.legit_opacity
					* ( player.invulnerable ? 0.45f : 1.0f ), 0.0f, 255.0f ) );
			const auto fill = zdraw::draw_list::to_im_color( player_arrow );

			canvas->AddConcavePolyFilled( perimeter.data( ),
				static_cast<int>( perimeter.size( ) ), fill );

			if ( cfg.bloom && bloom_alpha > 0 )
			{
				const auto immunity_scale = ( player.invulnerable ? 0.45f : 1.0f )
					* player.legit_opacity;
				const zdraw::rgba glow{ cfg.bloom_color.r, cfg.bloom_color.g,
					cfg.bloom_color.b, static_cast<std::uint8_t>( std::clamp(
						bloom_alpha * immunity_scale, 0.0f, 255.0f ) ) };
				const auto bloom_radius = std::clamp( cfg.bloom_radius, 1.0f, 16.0f );

				chams::g_renderer.add_2d_bloom_triangle( tip.x, tip.y,
					left_base.x, left_base.y, notch.x, notch.y, bloom_radius, glow );
				chams::g_renderer.add_2d_bloom_triangle( tip.x, tip.y,
					notch.x, notch.y, right_base.x, right_base.y, bloom_radius, glow );
			}
			canvas->Flags = saved_flags;
		}
	}

	void player_t::paint_threat_highlights( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, float current_time )
	{
		const auto& cfg = config::visual_settings.m_player.m_threat_module;
		if ( !cfg.enabled )
		{
			return;
		}

		const auto eye_pos = game::camera().origin( );

		auto& anim = this->m_animations[ player.controller ];
		if ( player.health < anim.last_health )
		{
			anim.last_damage_time = current_time;
		}
		anim.last_health = player.health;

		const auto damage_flash = std::clamp( 1.0f - ( current_time - anim.last_damage_time ) * 3.0f, 0.0f, 1.0f );

		auto draw_capsule = [ & ]( std::span<const int> bone_indices, const zdraw::rgba& base_color, bool use_color_alpha = false )
			{
				for ( const auto bone_index : bone_indices )
				{
					const game::hitbox_catalog::entry* target_hb = nullptr;
					for ( const auto& hb : player.hitboxes )
					{
						if ( hb.bone == bone_index && hb.radius > 0.0f )
						{
							target_hb = &hb;
							break;
						}
					}

					if ( !target_hb )
					{
						continue;
					}

					const auto& bone_data = bones.bones[ static_cast< std::size_t >( target_hb->bone ) ];

					const auto cap_a = bone_data.position + foundation::rotate( bone_data.rotation, target_hb->mins );
					const auto cap_b = bone_data.position + foundation::rotate( bone_data.rotation, target_hb->maxs );
					const auto center_world = ( cap_a + cap_b ) * 0.5f;

					const auto sa = game::camera().project( cap_a );
					const auto sb = game::camera().project( cap_b );

					if ( !game::camera().projection_valid( sa ) || !game::camera().projection_valid( sb ) )
					{
						continue;
					}

					const auto view_dir = ( center_world - eye_pos ).normalized( );
					auto perp = ( cap_b - cap_a ).cross( view_dir );
					const auto pl = perp.length( );

					if ( pl < 0.001f )
					{
						perp = foundation::vec3{ 0.0f, 0.0f, 1.0f }.cross( view_dir );
						const auto pl2 = perp.length( );
						perp = pl2 > 0.001f ? perp / pl2 : foundation::vec3{ 1.0f, 0.0f, 0.0f };
					}
					else
					{
						perp = perp / pl;
					}

					const auto perp_world = perp * target_hb->radius;
					const auto p_left = game::camera().project( center_world + perp_world );
					const auto p_right = game::camera().project( center_world - perp_world );

					if ( !game::camera().projection_valid( p_left ) || !game::camera().projection_valid( p_right ) )
					{
						continue;
					}

					const auto screen_radius = std::sqrt( ( p_left.x - p_right.x ) * ( p_left.x - p_right.x ) + ( p_left.y - p_right.y ) * ( p_left.y - p_right.y ) ) * 0.5f;
					if ( screen_radius <= 0.0f || screen_radius > 500.0f )
					{
						continue;
					}

					const auto flash_boost = use_color_alpha ? 0.0f : damage_flash * 60.0f;
					const auto final_alpha = static_cast< std::uint8_t >( std::min( static_cast< float >( base_color.a ) + flash_boost, 255.0f ) );

					const auto fill_color = zdraw::rgba( base_color.r, base_color.g, base_color.b, final_alpha );

					const auto outline_alpha = static_cast< std::uint8_t >( cfg.outline_alpha * ( static_cast< float >( base_color.a ) / 255.0f ) );
					const auto outline_color = zdraw::rgba( base_color.r, base_color.g, base_color.b, outline_alpha );

					const auto dx = sb.x - sa.x;
					const auto dy = sb.y - sa.y;
					const auto axis_len = std::sqrt( dx * dx + dy * dy );

					if ( axis_len < 1.0f )
					{

						draw_list.add_circle_filled( sa.x, sa.y, screen_radius, fill_color, 24 );

						if ( cfg.outline_thickness > 0.0f )
						{
							draw_list.add_circle( sa.x, sa.y, screen_radius, outline_color, 24, cfg.outline_thickness );
						}
						continue;
					}

					constexpr auto cap_segments = 12;
					constexpr auto pi = std::numbers::pi_v<float>;
					const auto phi = std::atan2( dy, dx );

					std::array<float, ( cap_segments + 1 ) * 4> pts{};
					int n = 0;

					for ( int s = 0; s <= cap_segments; ++s )
					{
						const auto a = phi + pi * 0.5f + pi * static_cast< float >( s ) / cap_segments;
						pts[ n++ ] = sa.x + std::cos( a ) * screen_radius;
						pts[ n++ ] = sa.y + std::sin( a ) * screen_radius;
					}

					for ( int s = 0; s <= cap_segments; ++s )
					{
						const auto a = phi - pi * 0.5f + pi * static_cast< float >( s ) / cap_segments;
						pts[ n++ ] = sb.x + std::cos( a ) * screen_radius;
						pts[ n++ ] = sb.y + std::sin( a ) * screen_radius;
					}

					const auto poly = std::span<const float>( pts.data( ), static_cast< std::size_t >( n ) );

					draw_list.add_convex_poly_filled( poly, fill_color );

					if ( cfg.outline_thickness > 0.0f )
					{
						draw_list.add_polyline( poly, outline_color, true, cfg.outline_thickness );
					}
				}
			};

		if ( cfg.head_hitbox )
		{
			draw_capsule( threat_head_bones, cfg.head_color, true );
		}

		if ( cfg.body_hitbox )
		{
			draw_capsule( threat_body_bones, cfg.body_color );
		}

		if ( cfg.limb_hitbox )
		{
			draw_capsule( threat_limb_bones, cfg.limb_color );
		}
	}

	void player_t::paint_frame( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const config::visual_profile::player::box& cfg, bool is_visible )
	{
		const auto& color = is_visible ? cfg.visible_color : cfg.occluded_color;

		const auto x = std::floorf( bounds.min.x );
		const auto y = std::floorf( bounds.min.y );
		const auto w = std::floorf( bounds.max.x - bounds.min.x );
		const auto h = std::floorf( bounds.max.y - bounds.min.y );

		if ( cfg.fill )
		{

			const auto& fill_color = is_visible ? cfg.fill_visible_color : cfg.fill_occluded_color;

			draw_list.add_rect_filled( x + 1, y + 1, w - 2, h - 2, fill_color );
		}

		if ( cfg.style == config::visual_profile::player::box::style_type::full )
		{
			if ( cfg.outline )
			{
				draw_list.add_rect( x - 1, y - 1, w + 2, h + 2, zdraw::rgba( 0, 0, 0, 180 ), 1.0f );
				draw_list.add_rect( x, y, w, h, zdraw::rgba( 0, 0, 0, 200 ), 2.0f );
			}

			draw_list.add_rect( x, y, w, h, color, 1.0f );
		}
		else
		{
			const auto corner = std::min( cfg.corner_length, std::min( w, h ) * 0.4f );

			if ( cfg.outline )
			{
				draw_list.add_rect_cornered( x - 1, y - 1, w + 2, h + 2, zdraw::rgba( 0, 0, 0, 180 ), corner + 1, 1.0f );
				draw_list.add_rect_cornered( x, y, w, h, zdraw::rgba( 0, 0, 0, 200 ), corner, 2.0f );
			}

			draw_list.add_rect_cornered( x, y, w, h, color, corner, 1.0f );
		}
	}

	void player_t::paint_skeleton( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const config::visual_profile::player::skeleton& cfg, bool is_visible )
	{
		const auto& color = is_visible ? cfg.visible_color : cfg.occluded_color;

		for ( const auto& [from, to] : skeleton_connections )
		{
			const auto from_screen = game::camera().project( bones.get_position( from ) );
			const auto to_screen = game::camera().project( bones.get_position( to ) );

			if ( !game::camera().projection_valid( from_screen ) || !game::camera().projection_valid( to_screen ) )
			{
				continue;
			}

			draw_list.add_line( from_screen.x, from_screen.y, to_screen.x, to_screen.y, color, cfg.thickness );
		}
	}

	void player_t::paint_head_marker( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, const config::visual_profile::player::head_circle& cfg )
	{
		const game::hitbox_catalog::entry* head_hb = nullptr;
		for ( const auto& hb : player.hitboxes )
		{
			if ( game::hitbox_data().hitgroup_from_hitbox( hb.index ) == 1 )
			{
				head_hb = &hb;
				break;
			}
		}

		if ( !head_hb )
		{
			return;
		}

		const auto& bone_data = bones.bones[ head_hb->bone ];
		const auto center_local = ( head_hb->mins + head_hb->maxs ) * 0.5f;
		const auto center_world = bone_data.position + foundation::rotate( bone_data.rotation, center_local );

		const auto screen_pos = game::camera().project( center_world );
		if ( !game::camera().projection_valid( screen_pos ) )
		{
			return;
		}

		const auto eye_pos = game::camera().origin( );
		const auto view_dir = ( center_world - eye_pos ).normalized( );
		auto perp = foundation::vec3{ 0.0f, 0.0f, 1.0f }.cross( view_dir );

		if ( perp.length_sqr( ) < 0.001f )
		{
			perp = foundation::vec3{ 1.0f, 0.0f, 0.0f };
		}
		else
		{
			perp.normalize( );
		}

		const auto edge_world = center_world + perp * head_hb->radius;
		const auto edge_screen = game::camera().project( edge_world );

		if ( !game::camera().projection_valid( edge_screen ) )
		{
			return;
		}

		const auto screen_radius = std::sqrt( ( edge_screen.x - screen_pos.x ) * ( edge_screen.x - screen_pos.x ) + ( edge_screen.y - screen_pos.y ) * ( edge_screen.y - screen_pos.y ) );

		if ( screen_radius > 0.0f && screen_radius < 500.0f )
		{
			draw_list.add_circle( screen_pos.x, screen_pos.y, screen_radius, cfg.color, 32, cfg.thickness );
		}
	}

	void player_t::paint_view_direction( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, const config::visual_profile::player::view_line& cfg )
	{
		const auto head_pos = bones.get_position( game::rules::joint_id::head );
		if ( head_pos.x == 0.0f && head_pos.y == 0.0f && head_pos.z == 0.0f )
		{
			return;
		}

		foundation::vec3 forward{}, right{}, up{};
		foundation::basis_from_angles( player.eye_angles, forward, right, up );

		const auto end_pos = head_pos + forward * cfg.length;

		const auto screen_start = game::camera().project( head_pos );
		const auto screen_end = game::camera().project( end_pos );

		if ( game::camera().projection_valid( screen_start ) && game::camera().projection_valid( screen_end ) )
		{
			draw_list.add_line( screen_start.x, screen_start.y, screen_end.x, screen_end.y, cfg.color, cfg.thickness );
		}
	}

	void player_t::paint_health_meter( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::health_bar& cfg )
	{
		auto& anim = this->m_animations[ player.controller ];
		const auto clamped_health = std::clamp( player.health, 0, 100 );
		const auto target_fraction = clamped_health / 100.0f;

		if ( !anim.initialized || ( target_fraction - anim.health.value( ) > 0.5f ) )
		{
			anim.health.snap( target_fraction );
			anim.initialized = true;
		}
		else
		{
			anim.health.set_target( target_fraction );
			anim.health.update( );
		}

		const auto vertical = cfg.position == config::visual_profile::player::health_bar::position_type::left ||
			cfg.position == config::visual_profile::player::health_bar::position_type::right;
		const auto& layout = config::visual_settings.m_player.m_layout.health;
		const auto meter = paint_meter_body( draw_list, bounds,
			anim.health.value( ), cfg, layout, vertical );

		if ( cfg.show_value && clamped_health < 100 )
		{
			const auto* base_font = app::context().overlay.fonts( ).esp_text_11;
			if ( !base_font ) return;
			auto scaled_font = *base_font;
			scaled_font.font_size *= layout.scale;
			const auto text = std::to_string( clamped_health );
			const auto [text_w, text_h] = zdraw::measure_text( text, &scaled_font );
			const auto text_x = std::floor( meter.x + meter.width * 0.5f - text_w * 0.5f );
			const auto text_y = vertical
				? std::floor( meter.y + meter.height - meter.fill - text_h - 2.0f )
				: std::floor( meter.y - text_h - 2.0f );

			draw_list.add_text( text_x, text_y, text, &scaled_font, cfg.text_color, zdraw::text_style::outlined );
		}
	}

	void player_t::paint_armor_meter( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::armor_bar& cfg )
	{
		auto& anim = this->m_animations[ player.controller ];
		const auto clamped_armor = std::clamp( player.armor, 0, 100 );
		const auto target_fraction = clamped_armor / 100.0f;

		if ( !anim.initialized || ( target_fraction - anim.armor.value( ) > 0.5f ) )
		{
			anim.armor.snap( target_fraction );
			anim.initialized = true;
		}
		else
		{
			anim.armor.set_target( target_fraction );
			anim.armor.update( );
		}

		const auto vertical = cfg.position == config::visual_profile::player::armor_bar::position_type::left ||
			cfg.position == config::visual_profile::player::armor_bar::position_type::right;
		const auto& layout = config::visual_settings.m_player.m_layout.armor;
		const auto meter = paint_meter_body( draw_list, bounds,
			anim.armor.value( ), cfg, layout, vertical );

		if ( cfg.show_value && clamped_armor > 0 )
		{
			const auto* base_font = app::context().overlay.fonts( ).esp_text_11;
			if ( !base_font ) return;
			auto scaled_font = *base_font;
			scaled_font.font_size *= layout.scale;
			const auto text = std::to_string( clamped_armor );
			const auto [text_w, text_h] = zdraw::measure_text( text, &scaled_font );
			const auto text_x = std::floor( meter.x + meter.width * 0.5f - text_w * 0.5f );
			const auto text_y = vertical
				? std::floor( meter.y + meter.height - meter.fill - text_h - 2.0f )
				: std::floor( meter.y - text_h - 2.0f );
			draw_list.add_text( text_x, text_y, text, &scaled_font, cfg.text_color,
				zdraw::text_style::outlined );
		}
	}

	void player_t::paint_identity( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::name& cfg )
	{
		const auto* base_font = app::context().overlay.fonts( ).esp_text_11;
		if ( !base_font || !base_font->im_font ) return;
		const auto& layout = config::visual_settings.m_player.m_layout.name;
		auto scaled_font = *base_font;
		scaled_font.font_size *= layout.scale;
		const auto [ text_w, text_h ] = zdraw::measure_text( player.display_name, &scaled_font );
		const auto anchor_x = config::visual_profile::player::resolve_layout_axis(
			bounds.min.x, bounds.max.x, layout.x, config::visual_profile::player::layout_reference_width );
		const auto anchor_y = config::visual_profile::player::resolve_layout_axis(
			bounds.min.y, bounds.max.y, layout.y, config::visual_profile::player::layout_reference_height );
		const auto text_x = std::floorf( anchor_x - text_w * 0.5f );
		const auto text_y = std::floorf( anchor_y - text_h * 0.5f );
		draw_list.add_text( text_x, text_y, player.display_name, &scaled_font, cfg.color, zdraw::text_style::outlined );
	}

	void player_t::paint_weapon_label( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::weapon& cfg )
	{
		const auto* text_font_base = app::context().overlay.fonts( ).esp_text_11;
		const auto* icon_font_base = app::context().overlay.fonts( ).weapons_esp_15;
		if ( !text_font_base || !icon_font_base ) return;
		const auto& layout = config::visual_settings.m_player.m_layout.weapon;
		auto text_font = *text_font_base;
		auto icon_font = *icon_font_base;
		text_font.font_size *= layout.scale;
		icon_font.font_size *= layout.scale;

		const auto show_icon = cfg.display == config::visual_profile::player::weapon::display_type::icon || cfg.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto show_text = cfg.display == config::visual_profile::player::weapon::display_type::text || cfg.display == config::visual_profile::player::weapon::display_type::text_and_icon;
		const auto icon = this->weapon_glyph( player.weapon.name );
		const auto [ icon_w, icon_h ] = show_icon ? zdraw::measure_text( icon, &icon_font ) : std::pair{ 0.0f, 0.0f };
		const auto ammo_valid = player.weapon.max_ammo > 0 && player.weapon.ammo >= 0;
		const auto exact_ammo = cfg.ammo.enabled && cfg.ammo.show_count && ammo_valid
			? std::to_string( std::clamp( player.weapon.ammo, 0, player.weapon.max_ammo ) )
			: std::string{};
		auto weapon_text = player.weapon.name;
		if ( show_text && !show_icon && !exact_ammo.empty( ) )
			weapon_text += "  " + exact_ammo;
		const auto [ text_w, text_h ] = show_text
			? zdraw::measure_text( weapon_text, &text_font ) : std::pair{ 0.0f, 0.0f };
		const auto [ ammo_w, ammo_h ] = show_icon && !exact_ammo.empty( )
			? zdraw::measure_text( exact_ammo, &text_font ) : std::pair{ 0.0f, 0.0f };
		const auto ammo_gap = ammo_w > 0.0f ? 3.0f * layout.scale : 0.0f;
		const auto gap = show_icon && show_text ? 2.0f * layout.scale : 0.0f;
		const auto icon_row_height = std::max( icon_h, ammo_h );
		const auto total_height = ( show_icon ? icon_row_height : 0.0f ) + text_h + gap;
		const auto anchor_x = config::visual_profile::player::resolve_layout_axis(
			bounds.min.x, bounds.max.x, layout.x, config::visual_profile::player::layout_reference_width );
		const auto anchor_y = config::visual_profile::player::resolve_layout_axis(
			bounds.min.y, bounds.max.y, layout.y, config::visual_profile::player::layout_reference_height );
		auto current_y = anchor_y - total_height * 0.5f;

		if ( show_icon )
		{
			const auto row_width = icon_w + ammo_gap + ammo_w;
			const auto icon_x = std::floorf( anchor_x - row_width * 0.5f );
			const auto icon_y = std::floorf( current_y + ( icon_row_height - icon_h ) * 0.5f );
			const zdraw::rgba pin_color{ 235, 65, 70, cfg.icon_color.a };
			if ( player.weapon.pin_pulled )
			{

				draw_list.add_text( icon_x, icon_y, icon, &icon_font,
					pin_color, zdraw::text_style::outlined );
			}
			else if ( cfg.ammo.enabled && ammo_valid )
			{
				const auto ammo = std::clamp( player.weapon.ammo, 0, player.weapon.max_ammo );
				const auto fraction = static_cast<float>( ammo ) / player.weapon.max_ammo;

				draw_list.add_text( icon_x, icon_y, icon, &icon_font,
					cfg.ammo.empty_color, zdraw::text_style::outlined );
				if ( fraction > 0.0f && draw_list.m_im_draw_list )
				{
					const auto split_x = icon_x + icon_w * ( 1.0f - fraction );
					draw_list.m_im_draw_list->PushClipRect(
						{ split_x, icon_y - 2.0f },
						{ icon_x + icon_w + 2.0f, icon_y + icon_h + 2.0f }, true );
					draw_list.add_text( icon_x, icon_y, icon, &icon_font,
						cfg.icon_color, zdraw::text_style::normal );
					draw_list.m_im_draw_list->PopClipRect( );
				}
			}
			else
			{
				draw_list.add_text( icon_x, icon_y, icon, &icon_font,
					cfg.icon_color, zdraw::text_style::outlined );
			}
			if ( !exact_ammo.empty( ) )
			{
				const auto ammo_x = std::floorf( icon_x + icon_w + ammo_gap );
				const auto ammo_y = std::floorf(
					current_y + ( icon_row_height - ammo_h ) * 0.5f );
				draw_list.add_text( ammo_x, ammo_y, exact_ammo, &text_font,
					player.weapon.pin_pulled ? pin_color : cfg.text_color,
					zdraw::text_style::outlined );
			}
			current_y += icon_row_height + gap;
		}
		if ( show_text )
		{
			draw_list.add_text( std::floorf( anchor_x - text_w * 0.5f ),
				std::floorf( current_y ), weapon_text, &text_font,
				cfg.text_color, zdraw::text_style::outlined );
		}
	}

	void player_t::paint_status_labels( zdraw::draw_list& draw_list,
		zdraw::draw_list& bright_draw_list,
		const game::bounds_projector::data& bounds,
		const game::player_snapshot& player,
		const config::visual_profile::player& cfg,
		const active_bomb_info& bomb, float current_time )
	{
		const auto* base_font = app::context().overlay.fonts( ).esp_text_11;
		const auto* icon_font = app::context().overlay.fonts( ).weapons_esp_15;
		if ( !base_font || !base_font->im_font ) return;
		const auto& layout = config::visual_settings.m_player.m_layout.flags;
		const auto x = std::floorf( config::visual_profile::player::resolve_layout_axis(
			bounds.min.x, bounds.max.x, layout.x, config::visual_profile::player::layout_reference_width ) );
		auto y = std::floorf( config::visual_profile::player::resolve_layout_axis(
			bounds.min.y, bounds.max.y, layout.y, config::visual_profile::player::layout_reference_height ) );

		using info_flags = config::visual_profile::player::info_flags;
		struct status_line
		{
			std::string text;
			const info_flags::style* style{};
			status_icon icon{};
		};
		std::array<status_line, 9> lines{};
		std::size_t line_count{};
		const auto append = [ & ]( bool visible, std::string text,
			const info_flags::style& style, status_icon icon )
		{
			if ( visible && line_count < lines.size( ) )
				lines[ line_count++ ] = { std::move( text ), &style, icon };
		};
		const auto& flags = cfg.m_info_flags;

		if ( flags.has( info_flags::flag::bomb_damage ) && bomb.valid && bomb.blow_time > current_time )
		{
			const auto damage = bomb.baked_site >= 0
				? game::blast_damage().predicted_damage(
					player.collision_center, bomb.baked_site, player.eye_angles, player.is_ducked )
				: -1;
			append( damage > 0, std::format( "-{} HP", damage ),
				flags.bomb_damage_style, status_icon::bomb );
		}

		using flag = info_flags::flag;
		if ( flags.has( flag::money ) )
			append( true, std::format( "${}", player.money ),
				flags.money_style, status_icon::money );
		append( flags.has( flag::armor ) && player.armor > 0,
			player.has_helmet ? "hk" : "k", flags.armor_style, status_icon::armor );
		append( flags.has( flag::scoped ) && player.is_scoped, "zoom",
			flags.scoped_style, status_icon::scoped );
		append( flags.has( flag::defusing ) && player.is_defusing, "defusing",
			flags.defusing_style, status_icon::defusing );
		if ( flags.has( flag::ping ) )
			append( true, std::format( "{}ms", player.ping ),
				flags.ping_style, status_icon::ping );
		if ( flags.has( flag::distance ) )
		{
			const auto distance = game::camera().origin( ).distance( player.origin ) * 0.01905f;
			append( true, std::format( "{:.0f}m", distance ),
				flags.distance_style, status_icon::distance );
		}

		append( flags.has( flag::kit ) && player.has_defuser, "kit",
			flags.kit_style, status_icon::kit );
		append( flags.has( flag::flashed ) && player.is_flashed, "flashed",
			flags.flashed_style, status_icon::flashed );

		for ( std::size_t line_index{}; line_index < line_count; ++line_index )
		{
			const auto& line = lines[ line_index ];
			auto line_font = *base_font;
			line_font.font_size *= layout.scale * line.style->scale;
			if ( line.icon == status_icon::kit )
			{
				if ( icon_font && icon_font->im_font )
				{
					auto kit_font = *icon_font;
					kit_font.font_size = 15.0f * layout.scale * line.style->scale;

					draw_list.add_text( x, y, "r", &kit_font, line.style->color,
						zdraw::text_style::outlined );
					y += zdraw::measure_text( "r", &kit_font ).second;
				}
				else
				{
					paint_status_icon( draw_list, status_icon::kit, x, y,
						line_font.font_size, line.style->color );
					y += line_font.font_size + 1.0f;
				}
			}
			else if ( line.icon == status_icon::flashed && icon_font && icon_font->im_font )
			{
				auto flash_font = *icon_font;
				flash_font.font_size = 15.0f * layout.scale * line.style->scale;

				draw_list.add_text( x, y, "i", &flash_font, line.style->color,
					zdraw::text_style::outlined );
				y += zdraw::measure_text( "i", &flash_font ).second;
			}
			else
			{
				draw_list.add_text( x, y, line.text, &line_font, line.style->color,
					zdraw::text_style::outlined );
				y += zdraw::measure_text( line.text, &line_font ).second;
			}
		}
		if ( player.invulnerable )
		{
			auto immune_font = *base_font;
			immune_font.font_size *= layout.scale;
			bright_draw_list.add_text( x, y, "IMMUNE", &immune_font,
				{ 255, 220, 110, 255 }, zdraw::text_style::outlined );
		}
	}

	std::string_view player_t::weapon_glyph( const std::string_view weapon_name )
	{
		using icon_entry = std::pair<std::string_view, std::string_view>;
		static constexpr auto icons = std::to_array<icon_entry>( {
			{ "knife_ct", "]" }, { "knife_t", "[" }, { "knife", "]" },
			{ "deagle", "A" }, { "elite", "B" }, { "fiveseven", "C" }, { "glock", "D" },
			{ "revolver", "J" }, { "hkp2000", "E" }, { "p250", "F" }, { "usp_silencer", "G" },
			{ "tec9", "H" }, { "cz75a", "I" }, { "mac10", "K" }, { "ump45", "L" },
			{ "bizon", "M" }, { "mp7", "N" }, { "mp9", "R" }, { "p90", "O" },
			{ "mp5sd", "N" }, { "galilar", "Q" }, { "famas", "R" }, { "m4a1_silencer", "T" },
			{ "m4a1", "S" }, { "aug", "U" }, { "sg556", "V" }, { "ak47", "W" },
			{ "g3sg1", "X" }, { "scar20", "Y" }, { "awp", "Z" }, { "ssg08", "a" },
			{ "xm1014", "b" }, { "sawedoff", "c" }, { "mag7", "d" }, { "nova", "e" },
			{ "negev", "f" }, { "m249", "g" }, { "taser", "h" }, { "flashbang", "i" },
			{ "hegrenade", "j" }, { "smokegrenade", "k" }, { "molotov", "l" },
			{ "decoy", "m" }, { "incgrenade", "n" }, { "c4", "o" }
		} );
		const auto match = std::ranges::find( icons, weapon_name,
			[]( const auto& entry ) { return entry.first; } );
		if ( match == icons.end( ) ) return "?";
		return match->second;
	}

}
