#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <app/workers.hpp>
#include <render/menu/localization.hpp>
#include <resources/nades/nade_data.hpp>

#include <string>
#include <string_view>

namespace features::misc {

	namespace {

		namespace nd = resources::nades;

		[[nodiscard]] std::string_view normalize_map( std::string_view raw )
		{
			if ( const auto slash = raw.find_last_of( "/\\" ); slash != std::string_view::npos )
			{
				raw.remove_prefix( slash + 1 );
			}

			if ( const auto dot = raw.find( '.' ); dot != std::string_view::npos )
			{
				raw = raw.substr( 0, dot );
			}

			return raw;
		}

		[[nodiscard]] const nd::map_entries* find_map( std::string_view name )
		{
			for ( const auto& candidate : nd::k_maps )
			{
				if ( name == candidate.map )
				{
					return &candidate;
				}
			}

			return nullptr;
		}

		[[nodiscard]] float angle_error( const foundation::vec3& view_angles, float pitch, float yaw )
		{
			const auto dx = pitch - view_angles.x;
			const auto dy = foundation::wrap_yaw( yaw - view_angles.y );
			return std::sqrtf( dx * dx + dy * dy );
		}

		[[nodiscard]] const char* button_name( const float strength )
		{
			if ( strength <= 0.25f ) return render::localization::tr( "RMB" );
			if ( strength < 0.75f ) return render::localization::tr( "LMB+RMB" );
			return render::localization::tr( "LMB" );
		}

		[[nodiscard]] std::string translate_action( std::string_view action )
		{
			if ( action.empty( ) )
			{
				return {};
			}

			const auto whole = render::localization::tr( action );
			if ( whole != action )
			{
				return std::string{ whole };
			}

			std::string out{};
			std::size_t start{ 0 };
			while ( start <= action.size( ) )
			{
				const auto plus = action.find( '+', start );
				const auto token = action.substr( start,
					plus == std::string_view::npos ? std::string_view::npos : plus - start );

				if ( !out.empty( ) )
				{
					out += '+';
				}
				out += render::localization::tr( token );

				if ( plus == std::string_view::npos )
				{
					break;
				}
				start = plus + 1;
			}

			return out;
		}

		[[nodiscard]] foundation::vec3 local_origin( std::uintptr_t pawn )
		{
			const auto node = app::context().process.load<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
			return node
				? app::context().process.load<foundation::vec3>( node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) )
				: foundation::vec3{};
		}

		[[nodiscard]] bool game_has_input_focus( )
		{
			const auto foreground = ::GetForegroundWindow( );
			const auto root = foreground ? ::GetAncestor( foreground, GA_ROOT ) : nullptr;
			DWORD process_id{};
			if ( root )
				::GetWindowThreadProcessId( root, &process_id );
			return process_id != 0
				&& process_id == app::context().process.process_id( );
		}

		[[nodiscard]] bool gameplay_allows_throw( const std::uintptr_t pawn )
		{
			if ( !pawn || app::context().process.load<bool>( pawn
				+ SCHEMA( "C_CSPlayerPawn", "m_bIsBuyMenuOpen"_id ) ) )
			{
				return false;
			}

			static std::uintptr_t game_rules{};
			static auto next_scan = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now( );
			if ( now >= next_scan )
			{
				next_scan = now + std::chrono::milliseconds( 500 );
				game_rules = 0;
				const auto entities = game::entity_index( ).all( );
				for ( const auto& entity : *entities )
				{
					if ( entity.schema_id != "C_CSGameRulesProxy"_id ) continue;
					game_rules = app::context().process.load<std::uintptr_t>( entity.ptr
						+ SCHEMA( "C_CSGameRulesProxy", "m_pGameRules"_id ) );
					if ( game_rules ) break;
				}
			}

			if ( !game_rules ) return true;
			return !app::context().process.load<bool>( game_rules
				+ SCHEMA( "C_CSGameRules", "m_bFreezePeriod"_id ) )
				&& !app::context().process.load<bool>( game_rules
					+ SCHEMA( "C_CSGameRules", "m_bTeamIntroPeriod"_id ) )
				&& !app::context().process.load<bool>( game_rules
					+ SCHEMA( "C_GameRules", "m_bGamePaused"_id ) );
		}

	}

	std::uint8_t nade_helper_t::resolve_kind( std::uintptr_t weapon_vdata )
	{
		constexpr auto unknown = static_cast< std::uint8_t >( 0xff );
		if ( !weapon_vdata )
		{
			return unknown;
		}

		const auto name_ptr = app::context().process.load<std::uintptr_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
		if ( !name_ptr )
		{
			return unknown;
		}

		char name[ 64 ]{};
		if ( !app::context().process.copy( name_ptr, name, sizeof( name ) - 1 ) )
		{
			return unknown;
		}

		switch ( identity::of( name ) )
		{
		case "weapon_smokegrenade"_id: return static_cast< std::uint8_t >( nd::kind::smoke );
		case "weapon_flashbang"_id:    return static_cast< std::uint8_t >( nd::kind::flash );
		case "weapon_molotov"_id:
		case "weapon_incgrenade"_id:   return static_cast< std::uint8_t >( nd::kind::molotov );
		case "weapon_hegrenade"_id:    return static_cast< std::uint8_t >( nd::kind::he );
		case "weapon_decoy"_id:        return static_cast< std::uint8_t >( nd::kind::decoy );
		default:                         return unknown;
		}
	}

	std::string nade_helper_t::throw_instruction( const lineup_view& lineup )
	{
		std::string keys{};

		const auto append = [ &keys ]( const char* name )
		{
			if ( !keys.empty( ) )
			{
				keys += '+';
			}

			keys += render::localization::tr( name );
		};

		if ( lineup.actions & nd::action_crouch )
		{
			append( "Crouch" );
		}

		if ( lineup.actions & nd::action_walk )
		{
			append( "Walk" );
		}

		if ( lineup.actions & nd::action_run )
		{
			append( "Forward" );
		}

		if ( lineup.actions & nd::action_jump )
		{
			append( "Jump" );
		}

		const std::string button{ button_name( lineup.throw_strength ) };

		if ( keys.empty( ) )
		{
			return std::vformat( render::localization::tr( "Hold {}, release" ), std::make_format_args( button ) );
		}

		return std::vformat( render::localization::tr( "Hold {}, {}, release" ), std::make_format_args( button, keys ) );
	}

	bool nade_helper_t::collect( const foundation::vec3& player_pos, std::vector<lineup_view>& out ) const
	{
		out.clear( );

		const auto kind = resolve_kind( game::local_player().weapon_vdata( ) );
		if ( kind == 0xff )
		{
			return false;
		}

		const auto map_name = app::workers::current_map( );
		const auto map = map_name
			? find_map( normalize_map( *map_name ) )
			: nullptr;
		if ( !map )
		{
			return false;
		}

		const auto& cfg = config::general_settings.m_nade_helper;
		const auto max_distance_sqr = cfg.draw_distance * cfg.draw_distance;

		for ( std::uint32_t i = 0; i < map->count; ++i )
		{
			const auto& entry = map->data[ i ];
			if ( static_cast< std::uint8_t >( entry.type ) != kind )
			{
				continue;
			}

			const foundation::vec3 position{ entry.x, entry.y, entry.z };
			const auto distance_sqr = position.distance_sqr( player_pos );
			if ( distance_sqr > max_distance_sqr )
			{
				continue;
			}

			out.push_back( lineup_view{
				.name = entry.name,
				.action = entry.action,
				.position = position,
				.pitch = entry.pitch,
				.yaw = entry.yaw,
				.actions = entry.actions,
				.run_ticks = entry.run_ticks,
				.after_jump_ticks = entry.after_jump_ticks,
				.throw_strength = entry.throw_strength,
				.manual = entry.manual,
				.distance = std::sqrtf( distance_sqr ),
			} );
		}

		std::sort( out.begin( ), out.end( ), [ ]( const lineup_view& a, const lineup_view& b )
			{
				return a.distance > b.distance;
			} );

		return !out.empty( );
	}

	int nade_helper_t::select_armed( const std::vector<lineup_view>& lineups, const foundation::vec3& view_angles ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;

		auto best_index{ -1 };
		auto best_error = std::numeric_limits<float>::max( );

		for ( std::size_t i = 0; i < lineups.size( ); ++i )
		{
			const auto& lineup = lineups[ i ];
			if ( lineup.distance > cfg.stand_radius )
			{
				continue;
			}

			const auto error = angle_error( view_angles, lineup.pitch, lineup.yaw );
			if ( error < best_error )
			{
				best_error = error;
				best_index = static_cast< int >( i );
			}
		}

		return best_index;
	}

	bool nade_helper_t::execution_position_ready( const lineup_view& lineup,
		const foundation::vec3& player_pos ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		const auto dx = lineup.position.x - player_pos.x;
		const auto dy = lineup.position.y - player_pos.y;
		return dx * dx + dy * dy <= cfg.release_radius * cfg.release_radius
			&& std::abs( lineup.position.z - player_pos.z ) <= cfg.height_tolerance;
	}

	void nade_helper_t::reset_lock( )
	{
		this->m_lock_started = {};
		this->m_lock_name = nullptr;
		this->m_lock_position = {};
		this->m_lock_pitch = 0.0f;
		this->m_lock_yaw = 0.0f;
	}

	void nade_helper_t::on_render( zdraw::draw_list& draw_list )
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		if ( !cfg.enabled || !game::local_player().alive( ) )
		{
			return;
		}

		const auto pawn = game::local_player().pawn( );
		if ( !pawn )
		{
			return;
		}

		const auto player_pos = local_origin( pawn );
		if ( !this->collect( player_pos, this->m_render_scratch ) )
		{
			return;
		}

		const auto eye_pos = game::camera().origin( );
		auto view_angles = game::camera().angles( );
		auto unused_origin = foundation::vec3{};
		static_cast<void>( game::camera().sample( unused_origin, view_angles ) );
		const auto armed_index = this->select_armed( this->m_render_scratch, view_angles );

		const auto occupied = armed_index >= 0;

		for ( std::size_t i = 0; i < this->m_render_scratch.size( ); ++i )
		{
			const auto& lineup = this->m_render_scratch[ i ];
			const auto in_zone = lineup.distance <= cfg.stand_radius;

			if ( occupied && !in_zone )
			{
				continue;
			}

			if ( in_zone )
			{
				const auto selected = static_cast< int >( i ) == armed_index;
				const auto converged = this->execution_position_ready( lineup, player_pos )
					&& angle_error( view_angles, lineup.pitch, lineup.yaw ) <= cfg.aim_threshold;

				this->draw_stand_marker( draw_list, lineup, true );
				this->draw_aim_guidance( draw_list, lineup, eye_pos, selected, converged );
				continue;
			}

			if ( lineup.distance <= cfg.stand_distance )
			{
				this->draw_stand_marker( draw_list, lineup, false );
			}

			const auto screen = game::camera().project( lineup.position + foundation::vec3{ 0.0f, 0.0f, 16.0f } );
			if ( game::camera().projection_valid( screen ) )
			{
				this->draw_plaque( draw_list, lineup, screen );
			}
		}
	}

	void nade_helper_t::draw_text_plaque( zdraw::draw_list& draw_list, float center_x, float top_y,
		std::string_view title, std::string_view subtitle, const zdraw::rgba& accent, float alpha ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		const auto& fonts = app::context().overlay.fonts( );
		const auto* title_font = fonts.menu_semibold_13;
		const auto* sub_font = fonts.menu_regular_12;

		if ( !title_font || alpha <= 0.01f )
		{
			return;
		}

		const auto scale_alpha = [ alpha ]( const zdraw::rgba& color )
			{
				return zdraw::rgba{ color.r, color.g, color.b,
					static_cast< std::uint8_t >( std::clamp( static_cast< float >( color.a ) * alpha, 0.0f, 255.0f ) ) };
			};

		const auto [ title_w, title_h ] = zdraw::measure_text( title, title_font );
		auto sub_w{ 0.0f };
		auto sub_h{ 0.0f };
		if ( !subtitle.empty( ) && sub_font )
		{
			const auto measured = zdraw::measure_text( subtitle, sub_font );
			sub_w = measured.first;
			sub_h = measured.second;
		}

		constexpr auto pad_x{ 8.0f };
		constexpr auto pad_y{ 5.0f };
		constexpr auto gap{ 1.0f };

		const auto content_w = std::max( title_w, sub_w );
		const auto content_h = title_h + ( sub_h > 0.0f ? gap + sub_h : 0.0f );
		const auto box_w = content_w + pad_x * 2.0f;
		const auto box_h = content_h + pad_y * 2.0f;
		const auto x = center_x - box_w * 0.5f;
		const auto y = top_y;

		if ( auto* im = draw_list.m_im_draw_list )
		{
			im->AddRectFilled( { x, y }, { x + box_w, y + box_h },
				zdraw::draw_list::to_im_color( scale_alpha( cfg.plaque_background ) ), 4.0f );

			im->AddLine( { x, y + 3.0f }, { x, y + box_h - 3.0f },
				zdraw::draw_list::to_im_color( scale_alpha( accent ) ), 2.0f );
		}

		draw_list.add_text( x + pad_x, y + pad_y, title, title_font, scale_alpha( cfg.plaque_text ) );

		if ( !subtitle.empty( ) && sub_font )
		{
			const zdraw::rgba dimmed{ cfg.plaque_text.r, cfg.plaque_text.g, cfg.plaque_text.b,
				static_cast< std::uint8_t >( cfg.plaque_text.a * 0.62f ) };
			draw_list.add_text( x + pad_x, y + pad_y + title_h + gap, subtitle, sub_font, scale_alpha( dimmed ) );
		}
	}

	void nade_helper_t::draw_plaque( zdraw::draw_list& draw_list, const lineup_view& lineup, const foundation::vec2& screen ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;

		std::string subtitle{};
		if ( cfg.show_action )
		{
			subtitle = translate_action( lineup.action );
		}

		if ( cfg.show_distance )
		{
			if ( !subtitle.empty( ) )
			{
				subtitle += "  ";
			}

			subtitle += std::format( "{:.0f}{}", lineup.distance / 52.0f, render::localization::tr( "m" ) );
		}

		const auto fade_start = cfg.draw_distance * 0.66f;
		const auto fade = lineup.distance <= fade_start
			? 1.0f
			: std::clamp( 1.0f - ( lineup.distance - fade_start ) / std::max( cfg.draw_distance - fade_start, 1.0f ), 0.0f, 1.0f );

		const auto [ _, title_h ] = zdraw::measure_text( lineup.name, app::context().overlay.fonts( ).menu_semibold_13 );
		this->draw_text_plaque( draw_list, screen.x, screen.y - ( title_h + 26.0f ),
			lineup.name, subtitle, cfg.plaque_accent, fade );
	}

	void nade_helper_t::draw_stand_marker( zdraw::draw_list& draw_list, const lineup_view& lineup, bool standing ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		const auto color = standing ? cfg.stand_marker_active : cfg.stand_marker;

		constexpr auto segments{ 28 };
		std::vector<float> points{};
		points.reserve( segments * 2 );

		for ( int i = 0; i < segments; ++i )
		{
			const auto theta = ( static_cast< float >( i ) / static_cast< float >( segments ) ) * 2.0f * std::numbers::pi_v<float>;
			const foundation::vec3 world{
				lineup.position.x + std::cosf( theta ) * cfg.stand_radius,
				lineup.position.y + std::sinf( theta ) * cfg.stand_radius,
				lineup.position.z + 1.0f,
			};

			const auto screen = game::camera().project( world );
			if ( !game::camera().projection_valid( screen ) )
			{
				return;
			}

			points.push_back( screen.x );
			points.push_back( screen.y );
		}

		draw_list.add_polyline( points, color, true, standing ? 2.0f : 1.4f );

		const auto center = game::camera().project( lineup.position + foundation::vec3{ 0.0f, 0.0f, 1.0f } );
		if ( game::camera().projection_valid( center ) )
		{
			draw_list.add_circle_filled( center.x, center.y, standing ? 2.5f : 1.8f, color, 10 );
		}
	}

	void nade_helper_t::draw_aim_guidance( zdraw::draw_list& draw_list, const lineup_view& lineup,
		const foundation::vec3& eye_pos, bool selected, bool converged ) const
	{
		const auto& cfg = config::general_settings.m_nade_helper;

		foundation::vec3 forward{};
		const foundation::vec3 angles{ lineup.pitch, lineup.yaw, 0.0f };
		angles.to_directions( &forward, nullptr, nullptr );

		const auto screen = game::camera().project( eye_pos + forward * k_aim_marker_distance );
		if ( !game::camera().projection_valid( screen ) )
		{
			return;
		}

		if ( !selected )
		{
			draw_list.add_circle( screen.x, screen.y, 4.0f, cfg.aim_marker, 16, 1.0f );
			return;
		}

		const auto color = converged ? cfg.stand_marker_active : cfg.aim_marker;
		const auto radius = converged ? 7.0f : 5.5f;
		const auto thickness = converged ? 2.0f : 1.4f;

		draw_list.add_circle( screen.x, screen.y, radius, color, 20, thickness );
		draw_list.add_line( screen.x - radius - 4.0f, screen.y, screen.x - radius - 1.0f, screen.y, color, thickness );
		draw_list.add_line( screen.x + radius + 1.0f, screen.y, screen.x + radius + 4.0f, screen.y, color, thickness );
		draw_list.add_line( screen.x, screen.y - radius - 4.0f, screen.x, screen.y - radius - 1.0f, color, thickness );
		draw_list.add_line( screen.x, screen.y + radius + 1.0f, screen.x, screen.y + radius + 4.0f, color, thickness );

		const auto instruction = lineup.manual
			? std::string{ render::localization::tr( "Manual lineup" ) }
			: throw_instruction( lineup );

		if ( !converged )
		{

			const auto [ screen_w, screen_h ] = zdraw::get_display_size( );
			const auto center_x = static_cast< float >( screen_w ) * 0.5f;
			const auto center_y = static_cast< float >( screen_h ) * 0.5f;

			const zdraw::rgba trail{ cfg.aim_marker.r, cfg.aim_marker.g, cfg.aim_marker.b,
				static_cast< std::uint8_t >( cfg.aim_marker.a * 0.55f ) };
			draw_list.add_line( center_x, center_y, screen.x, screen.y, trail, 1.4f );

			this->draw_text_plaque( draw_list, screen.x, screen.y + radius + 10.0f,
				lineup.name, instruction, cfg.plaque_accent, 1.0f );
			return;
		}

		const auto* font = app::context().overlay.fonts( ).menu_regular_12;
		if ( !font )
		{
			return;
		}

		const auto name_size = zdraw::measure_text( lineup.name, font );
		draw_list.add_text( screen.x - name_size.first * 0.5f, screen.y + radius + 8.0f,
			lineup.name, font, cfg.plaque_text, zdraw::text_style::outlined );

		const auto hint_size = zdraw::measure_text( instruction, font );
		const zdraw::rgba hint_color{ cfg.stand_marker_active.r, cfg.stand_marker_active.g,
			cfg.stand_marker_active.b, cfg.plaque_text.a };
		draw_list.add_text( screen.x - hint_size.first * 0.5f, screen.y + radius + 8.0f + name_size.second + 2.0f,
			instruction, font, hint_color, zdraw::text_style::outlined );
	}

	void nade_helper_t::tick( )
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		if ( !cfg.enabled )
		{
			const auto owns_input = this->m_throw_phase != throw_phase::idle
				|| !this->m_gated_keys.empty( ) || this->m_forward.pressed
				|| this->m_walk.pressed || this->m_duck.pressed
				|| this->m_jump.pressed || this->m_attack.pressed
				|| this->m_attack2.pressed;
			if ( owns_input )
				this->cancel_throw( false );
			else
			{
				this->m_activation_latched = false;
				this->reset_lock( );
			}
			return;
		}
		const auto now = std::chrono::steady_clock::now( );
		const auto activation_held = cfg.aim_key > 0
			&& ( ::GetAsyncKeyState( cfg.aim_key ) & 0x8000 ) != 0;
		if ( !activation_held )
		{

			const auto owns_input = this->m_throw_phase != throw_phase::idle
				|| !this->m_gated_keys.empty( ) || this->m_forward.pressed
				|| this->m_walk.pressed || this->m_duck.pressed
				|| this->m_jump.pressed || this->m_attack.pressed
				|| this->m_attack2.pressed;
			if ( owns_input )
				this->cancel_throw( false );
			this->m_activation_latched = false;
			this->m_aim_error = {};
			this->m_last_aim_update = {};
			this->reset_lock( );
			return;
		}

		const auto pawn = game::local_player().pawn( );
		const auto controller = game::local_player().controller( );
		const auto weapon = game::local_player().weapon( );
		const auto weapon_vdata = game::local_player().weapon_vdata( );
		const auto weapon_type = weapon_vdata
			? app::context().process.load<std::uint32_t>( weapon_vdata
				+ SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_id ) )
			: std::uint32_t{};
		const auto usable = !app::context().menu.is_open( )
			&& game_has_input_focus( ) && controller && pawn
			&& game::local_player().alive( ) && weapon
			&& weapon_type == game::rules::equipment_class::throwable
			&& gameplay_allows_throw( pawn );
		if ( !usable || ( this->m_throw_phase != throw_phase::idle
			&& ( pawn != this->m_active_pawn || weapon != this->m_active_weapon ) ) )
		{
			this->cancel_throw( activation_held );
			this->m_aim_error = {};
			return;
		}

		const auto raw_tick = app::context().process.load<std::int32_t>( controller
			+ SCHEMA( "CBasePlayerController", "m_nTickBase"_id ) );
		if ( raw_tick <= 0 )
		{
			this->cancel_throw( activation_held );
			return;
		}
		const auto tick = static_cast<std::uint32_t>( raw_tick );
		if ( this->m_throw_phase != throw_phase::idle )
		{
			if ( cfg.aim_assist )
			{
				foundation::vec3 unused_origin{}, live_view{};
				if ( !game::camera().sample( unused_origin, live_view ) )
				{
					this->cancel_throw( activation_held );
					return;
				}
				auto error = 0.0f;
				this->aim_at( this->m_active_lineup, live_view, error );
			}
			this->drive_throw( pawn, weapon, tick, now );
			return;
		}

		if ( !activation_held || this->m_activation_latched )
		{
			this->m_aim_error = {};
			this->m_last_aim_update = {};
			return;
		}
		const auto player_pos = local_origin( pawn );
		if ( !this->collect( player_pos, this->m_tick_scratch ) )
		{
			this->reset_lock( );
			return;
		}

		foundation::vec3 unused_origin{}, view_angles{};
		if ( !game::camera().sample( unused_origin, view_angles ) )
		{
			this->reset_lock( );
			return;
		}
		const auto index = this->select_armed( this->m_tick_scratch, view_angles );
		if ( index < 0 )
		{
			this->m_aim_error = {};
			this->m_last_aim_update = {};
			this->reset_lock( );
			return;
		}

		const auto& lineup = this->m_tick_scratch[static_cast<std::size_t>( index )];
		auto error = angle_error( view_angles, lineup.pitch, lineup.yaw );
		if ( cfg.aim_assist )
			this->aim_at( lineup, view_angles, error );

		foundation::vec3 velocity{};
		if ( !app::context().process.copy( pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&velocity, sizeof( velocity ) ) )
		{
			this->reset_lock( );
			return;
		}
		const auto stationary = std::isfinite( velocity.x ) && std::isfinite( velocity.y )
			&& std::isfinite( velocity.z ) && velocity.length_2d( ) <= 12.0f
			&& std::abs( velocity.z ) <= 12.0f;
		const auto position_ready = this->execution_position_ready( lineup, player_pos );
		const auto lock_matches = this->m_lock_name == lineup.name
			&& this->m_lock_position.distance_sqr( lineup.position ) <= 0.01f
			&& std::abs( this->m_lock_pitch - lineup.pitch ) <= 0.001f
			&& std::abs( foundation::wrap_yaw( this->m_lock_yaw - lineup.yaw ) ) <= 0.001f;
		if ( position_ready && stationary && error <= cfg.aim_threshold )
		{
			if ( !lock_matches )
			{
				this->m_lock_name = lineup.name;
				this->m_lock_position = lineup.position;
				this->m_lock_pitch = lineup.pitch;
				this->m_lock_yaw = lineup.yaw;
				this->m_lock_started = now;
			}
		}
		else
		{
			this->reset_lock( );
		}

		const auto settled = this->m_lock_started != std::chrono::steady_clock::time_point{}
			&& now - this->m_lock_started
				>= std::chrono::milliseconds( std::clamp( cfg.lock_time_ms, 0, 250 ) );
		if ( cfg.auto_release && !lineup.manual && settled )
		{
			if ( !this->begin_throw( lineup, pawn, weapon, tick, now ) )
				this->m_activation_latched = true;
		}
	}

	bool nade_helper_t::set_control( owned_control& control, const bool pressed )
	{
		if ( control.pressed == pressed )
			return true;
		if ( !control.binding )
			return false;

		using platform::windows::pointer_action;
		auto success = false;
		switch ( control.binding.device )
		{
		case game::input_device::keyboard:
			success = app::context().input.key( control.binding.virtual_key, pressed );
			break;
		case game::input_device::mouse_primary:
			success = app::context().input.pointer( 0, 0,
				pressed ? pointer_action::primary_down : pointer_action::primary_up );
			break;
		case game::input_device::mouse_secondary:
			success = app::context().input.pointer( 0, 0,
				pressed ? pointer_action::secondary_down : pointer_action::secondary_up );
			break;
		case game::input_device::mouse_middle:
			success = app::context().input.pointer( 0, 0,
				pressed ? pointer_action::middle_down : pointer_action::middle_up );
			break;
		case game::input_device::mouse_auxiliary1:
			success = app::context().input.pointer( 0, 0,
				pressed ? pointer_action::auxiliary1_down : pointer_action::auxiliary1_up );
			break;
		case game::input_device::mouse_auxiliary2:
			success = app::context().input.pointer( 0, 0,
				pressed ? pointer_action::auxiliary2_down : pointer_action::auxiliary2_up );
			break;
		default:
			break;
		}
		if ( success )
			control.pressed = pressed;
		return success;
	}

	void nade_helper_t::release_movement( const bool include_jump )
	{
		if ( include_jump ) (void)this->set_control( this->m_jump, false );
		(void)this->set_control( this->m_forward, false );
		(void)this->set_control( this->m_walk, false );
		(void)this->set_control( this->m_duck, false );
	}

	void nade_helper_t::release_attacks( )
	{
		using platform::windows::pointer_action;
		const auto primary_mouse = this->m_attack.pressed
			&& this->m_attack.binding.device == game::input_device::mouse_primary;
		const auto secondary_mouse = this->m_attack2.pressed
			&& this->m_attack2.binding.device == game::input_device::mouse_secondary;
		if ( primary_mouse && secondary_mouse
			&& app::context().input.pointer( 0, 0,
				pointer_action::primary_up | pointer_action::secondary_up ) )
		{
			this->m_attack.pressed = false;
			this->m_attack2.pressed = false;
		}
		(void)this->set_control( this->m_attack, false );
		(void)this->set_control( this->m_attack2, false );
	}

	void nade_helper_t::cancel_throw( const bool latch )
	{
		this->release_attacks( );
		this->release_movement( );
		app::context().input.set_movement_gate( {}, false );
		this->m_gated_keys.clear( );
		this->m_forward = {};
		this->m_walk = {};
		this->m_duck = {};
		this->m_jump = {};
		this->m_attack = {};
		this->m_attack2 = {};
		this->m_active_lineup = {};
		this->m_active_pawn = 0;
		this->m_active_weapon = 0;
		this->m_throw_phase = throw_phase::idle;
		this->m_phase_tick = 0;
		this->m_run_start_tick = 0;
		this->m_jump_tick = 0;
		this->m_aim_error = {};
		this->m_last_aim_update = {};
		this->reset_lock( );
		this->m_activation_latched = latch;
	}

	bool nade_helper_t::begin_throw( const lineup_view& lineup,
		const std::uintptr_t pawn, const std::uintptr_t weapon,
		const std::uint32_t tick, const std::chrono::steady_clock::time_point now )
	{
		this->cancel_throw( false );
		this->m_active_lineup = lineup;
		this->m_active_pawn = pawn;
		this->m_active_weapon = weapon;
		this->m_phase_tick = tick;
		this->m_phase_started = now;

		auto& bindings = game::input_bindings( );
		if ( lineup.actions & nd::action_run )
			this->m_forward.binding = bindings.resolve( game::input_action::forward );
		if ( lineup.actions & nd::action_walk )
			this->m_walk.binding = bindings.resolve( game::input_action::walk );
		if ( lineup.actions & nd::action_crouch )
			this->m_duck.binding = bindings.resolve( game::input_action::duck );
		if ( lineup.actions & nd::action_jump )
			this->m_jump.binding = bindings.resolve( game::input_action::jump );

		if ( lineup.throw_strength >= 0.75f )
			this->m_attack.binding = bindings.resolve( game::input_action::attack );
		else if ( lineup.throw_strength <= 0.25f )
			this->m_attack2.binding = bindings.resolve( game::input_action::attack2 );
		else
		{
			this->m_attack.binding = bindings.resolve( game::input_action::attack );
			this->m_attack2.binding = bindings.resolve( game::input_action::attack2 );
		}

		constexpr std::array movement_actions{
			game::input_action::forward, game::input_action::back,
			game::input_action::left, game::input_action::right,
			game::input_action::walk, game::input_action::duck,
			game::input_action::jump,
		};
		for ( const auto action : movement_actions )
		{
			const auto binding = bindings.resolve( action );
			if ( binding.device != game::input_device::keyboard || !binding.virtual_key
				|| std::ranges::find( this->m_gated_keys, binding.virtual_key )
					!= this->m_gated_keys.end( ) )
			{
				continue;
			}
			this->m_gated_keys.push_back( binding.virtual_key );
		}
		if ( !this->m_gated_keys.empty( ) )
			app::context().input.set_movement_gate( this->m_gated_keys, true );

		const auto missing = ( ( lineup.actions & nd::action_run ) && !this->m_forward.binding )
			|| ( ( lineup.actions & nd::action_walk ) && !this->m_walk.binding )
			|| ( ( lineup.actions & nd::action_crouch ) && !this->m_duck.binding )
			|| ( ( lineup.actions & nd::action_jump ) && !this->m_jump.binding )
			|| ( lineup.throw_strength >= 0.75f && !this->m_attack.binding )
			|| ( lineup.throw_strength <= 0.25f && !this->m_attack2.binding )
			|| ( lineup.throw_strength > 0.25f && lineup.throw_strength < 0.75f
				&& ( !this->m_attack.binding || !this->m_attack2.binding ) );
		if ( missing )
		{
			this->cancel_throw( true );
			return false;
		}

		if ( lineup.actions & nd::action_crouch )
		{
			if ( !this->set_control( this->m_duck, true ) )
			{
				this->cancel_throw( true );
				return false;
			}
			this->m_throw_phase = throw_phase::crouching;
			return true;
		}
		if ( !this->prime_throw( tick, now ) )
		{
			this->cancel_throw( true );
			return false;
		}
		return true;
	}

	bool nade_helper_t::prime_throw( const std::uint32_t tick,
		const std::chrono::steady_clock::time_point now )
	{
		const auto strength = this->m_active_lineup.throw_strength;
		if ( strength >= 0.75f )
		{
			if ( !this->set_control( this->m_attack, true ) )
				return false;
		}
		else if ( strength <= 0.25f )
		{
			if ( !this->set_control( this->m_attack2, true ) )
				return false;
		}
		else if ( this->m_attack.binding.device == game::input_device::mouse_primary
			&& this->m_attack2.binding.device == game::input_device::mouse_secondary )
		{
			using platform::windows::pointer_action;
			if ( !app::context().input.pointer( 0, 0,
				pointer_action::primary_down | pointer_action::secondary_down ) )
			{
				return false;
			}
			this->m_attack.pressed = true;
			this->m_attack2.pressed = true;
		}
		else if ( !this->set_control( this->m_attack, true )
			|| !this->set_control( this->m_attack2, true ) )
		{
			return false;
		}

		this->m_throw_phase = throw_phase::priming;
		this->m_phase_tick = tick;
		this->m_phase_started = now;
		return true;
	}

	void nade_helper_t::finish_throw( const std::uint32_t tick )
	{
		const auto release_after = ( this->m_active_lineup.actions
			& nd::action_release_movement_after_throw ) != 0;
		if ( release_after )
		{
			this->release_attacks( );
			this->release_movement( false );
		}
		else
		{
			this->release_movement( false );
			this->release_attacks( );
		}

		this->m_activation_latched = true;
		this->m_phase_tick = tick;
		if ( this->m_active_lineup.actions & nd::action_jump )
			this->m_throw_phase = throw_phase::complete;
		else
			this->cancel_throw( true );
	}

	void nade_helper_t::drive_throw( const std::uintptr_t pawn,
		const std::uintptr_t weapon, const std::uint32_t tick,
		const std::chrono::steady_clock::time_point now )
	{
		switch ( this->m_throw_phase )
		{
		case throw_phase::crouching:
		{
			const auto movement = app::context().process.load<std::uintptr_t>(
				pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_id ) );
			const auto duck_amount = movement ? app::context().process.load<float>(
				movement + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_id ) ) : 0.0f;
			const auto ducked = movement && ( duck_amount >= 0.94f
				|| app::context().process.load<bool>( movement
					+ SCHEMA( "CCSPlayer_MovementServices", "m_bDucked"_id ) ) );
			if ( ducked )
			{
				if ( !this->prime_throw( tick, now ) )
					this->cancel_throw( true );
			}
			else if ( now - this->m_phase_started > std::chrono::milliseconds( 900 ) )
				this->cancel_throw( true );
			return;
		}
		case throw_phase::priming:
		{
			const auto pin_pulled = app::context().process.load<bool>(
				weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
			const auto strength = app::context().process.load<float>(
				weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_id ) );
			const auto ready = pin_pulled && std::isfinite( strength )
				&& std::abs( strength - this->m_active_lineup.throw_strength ) <= 0.08f
				&& tick != this->m_phase_tick;
			if ( !ready )
			{
				if ( now - this->m_phase_started > std::chrono::milliseconds( 3000 ) )
					this->cancel_throw( true );
				return;
			}

			if ( this->m_active_lineup.actions & nd::action_run )
			{
				if ( !this->m_forward.pressed )
				{
					if ( ( this->m_active_lineup.actions & nd::action_walk )
						&& !this->set_control( this->m_walk, true ) )
					{
						this->cancel_throw( true );
						return;
					}
					if ( !this->set_control( this->m_forward, true ) )
					{
						this->cancel_throw( true );
						return;
					}
					this->m_run_start_tick = tick;
				}
				this->m_throw_phase = throw_phase::running;
				if ( tick - this->m_run_start_tick < this->m_active_lineup.run_ticks )
					return;
			}
			else if ( !( this->m_active_lineup.actions & nd::action_jump ) )
			{
				this->finish_throw( tick );
				return;
			}
			[[fallthrough]];
		}
		case throw_phase::running:
			if ( tick - this->m_run_start_tick < this->m_active_lineup.run_ticks )
				return;
			if ( this->m_active_lineup.actions & nd::action_jump )
			{
				if ( !this->set_control( this->m_jump, true ) )
				{
					this->cancel_throw( true );
					return;
				}
				this->m_jump_tick = tick;
				this->m_phase_started = now;
				this->m_throw_phase = throw_phase::jumping;

			}
			else
				this->finish_throw( tick );
			return;
		case throw_phase::jumping:
		{
			const auto elapsed_ticks = tick - this->m_jump_tick;
			const auto after_jump = this->m_active_lineup.after_jump_ticks;
			if ( elapsed_ticks < after_jump )
				return;

			if ( after_jump == 0 )
			{

				if ( now > this->m_phase_started )
					this->finish_throw( tick );
				return;
			}

			const auto flags = app::context().process.load<std::uint32_t>( pawn
				+ SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
			const auto velocity = app::context().process.load<foundation::vec3>( pawn
				+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
			const auto airborne = ( flags & 1u ) == 0u
				|| ( std::isfinite( velocity.z ) && std::abs( velocity.z ) > 12.0f );
			if ( airborne )
			{
				this->finish_throw( tick );
				return;
			}

			if ( now - this->m_phase_started > std::chrono::milliseconds( 250 ) )
				this->cancel_throw( true );
			return;
		}
		case throw_phase::complete:

			if ( tick != this->m_phase_tick )
				this->cancel_throw( true );
			return;
		default:
			return;
		}
	}

	void nade_helper_t::aim_at( const lineup_view& lineup, const foundation::vec3& view_angles, float& out_error )
	{
		const auto& cfg = config::general_settings.m_nade_helper;
		const auto now = std::chrono::steady_clock::now( );

		constexpr auto m_yaw{ 0.022f };
		const auto sensitivity = game::variables().get<float>( CONVAR( "sensitivity"_id ) );
		const auto fov_adjust = app::context().process.load<float>( game::local_player().pawn( ) + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id ) );
		const auto deg_per_pixel = sensitivity * m_yaw * fov_adjust;

		if ( deg_per_pixel <= 0.0f )
		{
			return;
		}

		auto delta_x = lineup.pitch - view_angles.x;
		auto delta_y = foundation::wrap_yaw( lineup.yaw - view_angles.y );

		out_error = std::sqrtf( delta_x * delta_x + delta_y * delta_y );

		if ( cfg.aim_smoothing > 1 )
		{

			const auto dt = this->m_last_aim_update == std::chrono::steady_clock::time_point{}
				? game::rules::simulation_step
				: std::clamp( std::chrono::duration<float>(
					now - this->m_last_aim_update ).count( ), 0.0005f, 0.05f );
			const auto response_seconds = ( 12.0f
				+ static_cast<float>( cfg.aim_smoothing - 1 ) * 8.0f ) * 0.001f;
			const auto amount = 1.0f - std::exp( -dt / response_seconds );
			delta_x *= amount;
			delta_y *= amount;
		}
		this->m_last_aim_update = now;

		this->m_aim_error.x += -delta_y / deg_per_pixel;
		this->m_aim_error.y += delta_x / deg_per_pixel;

		const auto dx = static_cast< int >( this->m_aim_error.x );
		const auto dy = static_cast< int >( this->m_aim_error.y );

		this->m_aim_error.x -= static_cast< float >( dx );
		this->m_aim_error.y -= static_cast< float >( dy );

		if ( dx != 0 || dy != 0 )
		{
			app::context().input.pointer( dx, dy, platform::windows::pointer_action::relative_move );
		}
	}

}
