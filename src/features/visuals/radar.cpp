#include <stdafx.hpp>
#include <features/visuals/visuals.hpp>

namespace {
	[[nodiscard]] bool radar_active( )
	{
		return config::visual_settings.m_radar.active( );
	}

	struct rect_t
	{
		float x{};
		float y{};
		float w{};
		float h{};

		[[nodiscard]] bool valid( float display_w, float display_h ) const
		{
			return std::isfinite( x ) && std::isfinite( y ) && std::isfinite( w ) && std::isfinite( h ) &&
				w >= 64.0f && h >= 64.0f && w <= display_w * 2.5f && h <= display_h * 2.5f;
		}
	};

	struct panel_info
	{
		std::uintptr_t wrapper{};
		std::uintptr_t ui{};
		rect_t image{};
		rect_t viewport{};
	};

	struct overview_info
	{
		std::uintptr_t object{};
		panel_info panel{};
		foundation::vec2 map_origin{};
		float map_scale{};
		float map_size{};
		float canvas_size{};
		float viewport_size{};
		float world_to_pixels{};
		foundation::vec2 map_center{};
		float radar_angle{};
		float alternate_scale{};
		bool alternate_transform{};
		rect_t marker_space{};
		float layout_width{};
		float layout_height{};
	};

	struct panel_chain_node
	{
		std::uintptr_t ui{};
		float offset_x{};
		float offset_y{};
		float width{};
		float height{};
	};

	struct panel_chain
	{
		std::array<panel_chain_node, 32> nodes{};
		std::array<float, 32> accumulated_x{};
		std::array<float, 32> accumulated_y{};
		std::size_t count{};
		float scale_x{};
		float scale_y{};
	};

	[[nodiscard]] rect_t scale_layout_rect(
		const rect_t& rect, const overview_info& overview,
		const float display_width, const float display_height )
	{
		if ( overview.layout_width <= 0.0f || overview.layout_height <= 0.0f )
			return rect;
		const auto scale_x = display_width / overview.layout_width;
		const auto scale_y = display_height / overview.layout_height;
		return { rect.x * scale_x, rect.y * scale_y,
			rect.w * scale_x, rect.h * scale_y };
	}

	[[nodiscard]] bool readable_pointer( std::uintptr_t value )
	{
		return value >= 0x10000 && value <= 0x00007fffffffffffULL;
	}

	std::uintptr_t hud_radar( )
	{
		static std::uintptr_t hud_global{};
		static std::uintptr_t cached_radar{};
		static std::uintptr_t cached_vtable{};
		if ( cached_radar && app::context().process.load<std::uintptr_t>( cached_radar ) == cached_vtable )
		{
			return cached_radar;
		}

		if ( !hud_global )
		{
			const auto find_hud = app::context().process.scan_signature( app::context().modules.client,
				"40 53 48 83 EC 20 48 8B 05 ? ? ? ? 48 8B D9 48 85 C0 74 ? 48 89 5C 24 ? 48 8D 88 58 02 00 00" );
			if ( !find_hud )
			{
				return 0;
			}
			hud_global = app::context().process.decode_rip( find_hud + 6 );
		}

		const auto root = app::context().process.load<std::uintptr_t>( hud_global );
		if ( !readable_pointer( root ) )
		{
			return 0;
		}

		const auto table = root + 0x258;
		const auto capacity = app::context().process.load<std::uint32_t>( table + 0xc ) & 0x7fffffffu;
		const auto data = capacity ? app::context().process.load<std::uintptr_t>( table + 0x10 ) : 0;
		if ( !readable_pointer( data ) || capacity > 1024 )
		{
			return 0;
		}

		for ( std::uint32_t index = 0; index < capacity; ++index )
		{
			const auto entry = data + static_cast<std::uintptr_t>( index ) * 0x20;
			const auto name = app::context().process.load<std::uintptr_t>( entry + 0x10 );
			const auto value = app::context().process.load<std::uintptr_t>( entry + 0x18 );
			if ( readable_pointer( name ) && readable_pointer( value ) &&
				app::context().process.load_text( name, 32 ) == "CCSGO_HudRadar" )
			{
				cached_radar = value;
				cached_vtable = app::context().process.load<std::uintptr_t>( value );
				return value;
			}
		}

		return 0;
	}

	[[nodiscard]] bool read_panel_chain( std::uintptr_t ui,
		float display_w, float display_h, panel_chain& out )
	{
		out = {};
		if ( !readable_pointer( ui ) )
		{
			return false;
		}

		std::uintptr_t current = ui;
		for ( ; out.count < out.nodes.size( ) && readable_pointer( current ); )
		{

			constexpr std::uintptr_t fields_begin{ 0x11d };
			constexpr std::uintptr_t fields_end{ 0x1c8 };
			std::array<std::byte, fields_end - fields_begin> fields{};
			if ( !app::context().process.copy( current + fields_begin,
				fields.data( ), fields.size( ) ) )
			{
				return false;
			}
			const auto field = [ & ]<typename T>( const std::uintptr_t offset )
			{
				T value{};
				std::memcpy( &value, fields.data( ) + offset - fields_begin,
					sizeof( value ) );
				return value;
			};
			const auto ox = field.template operator()<float>( 0x1b0 );
			const auto oy = field.template operator()<float>( 0x1b4 );
			if ( !std::isfinite( ox ) || !std::isfinite( oy ) || std::abs( ox ) > 20000.0f || std::abs( oy ) > 20000.0f )
			{
				return false;
			}

			const auto visibility = field.template operator()<std::uint8_t>( 0x11d );
			if ( ( visibility & 0x08 ) == 0 )
			{
				return false;
			}
			out.nodes[ out.count++ ] = {
				current, ox, oy,
				field.template operator()<float>( 0x1c0 ),
				field.template operator()<float>( 0x1c4 ) };

			const auto parent = app::context().process.load<std::uintptr_t>( current + 0x18 );
			if ( !readable_pointer( parent ) || parent == current )
			{
				break;
			}
			current = parent;
		}

		if ( out.count == 0 ) return false;
		const auto& root = out.nodes[ out.count - 1 ];
		const auto root_width = root.width;
		const auto root_height = root.height;
		if ( !std::isfinite( root_width ) || !std::isfinite( root_height ) || root_width < 320.0f || root_height < 240.0f )
		{
			return false;
		}

		out.scale_x = display_w / root_width;
		out.scale_y = display_h / root_height;
		float x{};
		float y{};
		for ( auto index = out.count; index-- > 0; )
		{
			x += out.nodes[ index ].offset_x;
			y += out.nodes[ index ].offset_y;
			out.accumulated_x[ index ] = x;
			out.accumulated_y[ index ] = y;
		}
		return true;
	}

	[[nodiscard]] rect_t chain_rect( const panel_chain& chain,
		const std::size_t index )
	{
		if ( index >= chain.count ) return {};
		const auto& node = chain.nodes[ index ];
		return { chain.accumulated_x[ index ] * chain.scale_x,
			chain.accumulated_y[ index ] * chain.scale_y,
			node.width * chain.scale_x, node.height * chain.scale_y };
	}

	[[nodiscard]] rect_t panel_rect( std::uintptr_t ui, float display_w, float display_h )
	{
		panel_chain chain{};
		return read_panel_chain( ui, display_w, display_h, chain )
			? chain_rect( chain, 0 ) : rect_t{};
	}

	[[nodiscard]] panel_info inspect_panel( std::uintptr_t wrapper, float display_w, float display_h )
	{
		panel_info result{};
		result.wrapper = wrapper;
		result.ui = app::context().process.load<std::uintptr_t>( wrapper + 0x8 );
		panel_chain chain{};
		if ( !read_panel_chain( result.ui, display_w, display_h, chain ) )
			return {};
		result.image = chain_rect( chain, 0 );
		if ( !result.image.valid( display_w, display_h ) )
		{
			return {};
		}

		for ( std::size_t depth = 0; depth < std::min<std::size_t>( 24, chain.count ); ++depth )
		{
			const auto candidate = chain_rect( chain, depth );
			if ( candidate.valid( display_w, display_h ) )
			{
				const auto aspect = candidate.w / candidate.h;
				if ( aspect > 0.80f && aspect < 1.20f && candidate.w <= result.image.w + 2.0f && candidate.h <= result.image.h + 2.0f )
				{
					result.viewport = candidate;
				}
			}
		}

		if ( !result.viewport.valid( display_w, display_h ) )
		{
			result.viewport = result.image;
		}
		return result;
	}

	[[nodiscard]] std::uintptr_t image_panel_vtable( )
	{
		static const auto value = app::context().process.locate_vtable( app::context().modules.client, "CImagePanel@panorama" );
		return value;
	}

	[[nodiscard]] panel_info find_radar_panel( std::uintptr_t object, float expected_size, float display_w, float display_h )
	{
		panel_info best{};
		float best_size_error = std::numeric_limits<float>::max( );
		const auto image_vtable = image_panel_vtable( );
		if ( !image_vtable )
		{
			return best;
		}

		for ( std::uintptr_t offset = 0x1e0; offset <= 0x430; offset += sizeof( std::uintptr_t ) )
		{
			const auto wrapper = app::context().process.load<std::uintptr_t>( object + offset );
			if ( !readable_pointer( wrapper ) || app::context().process.load<std::uintptr_t>( wrapper ) != image_vtable )
			{
				continue;
			}

			auto panel = inspect_panel( wrapper, display_w, display_h );
			const auto raw_width = app::context().process.load<float>( panel.ui + 0x1c0 );
			const auto raw_height = app::context().process.load<float>( panel.ui + 0x1c4 );
			const auto size_error = std::abs( raw_width - expected_size ) + std::abs( raw_height - expected_size );
			if ( panel.viewport.valid( display_w, display_h ) && std::isfinite( size_error ) && size_error < best_size_error )
			{
				best = panel;
				best_size_error = size_error;
			}
		}
		return best;
	}

	[[nodiscard]] rect_t marker_space_rect( std::uintptr_t object, float display_w, float display_h )
	{

		const auto marker_panel = app::context().process.load<std::uintptr_t>( object + 0x230 );
		const auto marker_ui = readable_pointer( marker_panel )
			? app::context().process.load<std::uintptr_t>( marker_panel + 0x8 )
			: 0;
		const auto parent = readable_pointer( marker_ui )
			? app::context().process.load<std::uintptr_t>( marker_ui + 0x18 )
			: 0;
		return panel_rect( parent, display_w, display_h );
	}

	std::vector<overview_info> discover_overviews( float display_w, float display_h )
	{
		std::vector<overview_info> result{};
		const auto object = hud_radar( );
		if ( !object || !image_panel_vtable( ) )
		{
			return result;
		}

		overview_info overview{};
		overview.object = object;

		overview.map_origin = app::context().process.load<foundation::vec2>( object + 0x170 );
		const auto inverse_scale = app::context().process.load<float>( object + 0x190 );
		overview.map_scale = std::isfinite( inverse_scale ) && inverse_scale > 0.0001f
			? 1.0f / inverse_scale
			: 0.0f;
		overview.map_size = app::context().process.load<float>( object + 0x188 );
		overview.canvas_size = app::context().process.load<float>( object + 0x17c );
		overview.viewport_size = app::context().process.load<float>( object + 0x180 );
		overview.world_to_pixels = app::context().process.load<float>( object + 0x194 );
		overview.map_center = app::context().process.load<foundation::vec2>( object + 0x1b0 );
		overview.radar_angle = app::context().process.load<float>( object + 0x1bc );
		overview.alternate_transform = app::context().process.load<std::uint8_t>( object + 0x40 ) != 0;
		overview.alternate_scale = app::context().process.load<float>( object + 0x17f6c );
		if ( !std::isfinite( overview.map_origin.x ) || !std::isfinite( overview.map_origin.y ) ||
			!std::isfinite( overview.map_scale ) || overview.map_scale < 0.01f || overview.map_scale > 100.0f ||
			!std::isfinite( overview.map_size ) || overview.map_size < 128.0f || overview.map_size > 8192.0f ||
			!std::isfinite( overview.canvas_size ) || overview.canvas_size < 64.0f || overview.canvas_size > 4096.0f ||
			!std::isfinite( overview.viewport_size ) || overview.viewport_size < 64.0f || overview.viewport_size > 4096.0f ||
			!std::isfinite( overview.world_to_pixels ) || overview.world_to_pixels < 0.001f || overview.world_to_pixels > 10.0f ||
			!std::isfinite( overview.map_center.x ) || !std::isfinite( overview.map_center.y ) ||
			!std::isfinite( overview.radar_angle ) )
		{
			return result;
		}

		overview.panel = find_radar_panel( object, std::max( overview.viewport_size, 290.0f ), display_w, display_h );
		overview.marker_space = marker_space_rect( object, display_w, display_h );
		overview.layout_width = display_w;
		overview.layout_height = display_h;

		if ( overview.panel.viewport.valid( display_w, display_h ) )
		{
			result.push_back( overview );
		}

		return result;
	}

	std::atomic<std::shared_ptr<const std::vector<overview_info>>> g_overview_snapshot{
		std::make_shared<std::vector<overview_info>>( ) };
	std::atomic<bool> g_square_always{};
	std::atomic<bool> g_square_with_scoreboard{};

	void refresh_overviews( std::vector<overview_info>& overviews,
		float display_width, float display_height )
	{
		static auto next_probe = std::chrono::steady_clock::time_point{};
		const auto now = std::chrono::steady_clock::now( );
		if ( overviews.empty( ) && now >= next_probe )
		{
			overviews = discover_overviews( display_width, display_height );
			next_probe = now + std::chrono::seconds( 1 );
		}

		static auto next_layout_refresh = std::chrono::steady_clock::time_point{};
		const auto refresh_layout = now >= next_layout_refresh;
		if ( refresh_layout )
		{
			next_layout_refresh = now + std::chrono::milliseconds( 50 );
		}

		auto any_active = false;
		for ( auto& overview : overviews )
		{
			std::array<std::byte, 0x50> dynamic{};
			const auto dynamic_ok = app::context().process.copy(
				overview.object + 0x170, dynamic.data( ), dynamic.size( ) );
			const auto read_dynamic = [ & ]<typename T>( std::size_t offset ) -> T
				{
					T value{};
					std::memcpy( &value, dynamic.data( ) + offset, sizeof( T ) );
					return value;
				};

			overview.map_origin = dynamic_ok
				? read_dynamic.template operator()<foundation::vec2>( 0x00 )
				: app::context().process.load<foundation::vec2>( overview.object + 0x170 );
			const auto inverse_scale = dynamic_ok
				? read_dynamic.template operator()<float>( 0x20 )
				: app::context().process.load<float>( overview.object + 0x190 );
			overview.map_scale = std::isfinite( inverse_scale ) && inverse_scale > 0.0001f
				? 1.0f / inverse_scale : 0.0f;
			overview.map_size = dynamic_ok
				? read_dynamic.template operator()<float>( 0x18 )
				: app::context().process.load<float>( overview.object + 0x188 );
			overview.canvas_size = dynamic_ok
				? read_dynamic.template operator()<float>( 0x0c )
				: app::context().process.load<float>( overview.object + 0x17c );
			overview.viewport_size = dynamic_ok
				? read_dynamic.template operator()<float>( 0x10 )
				: app::context().process.load<float>( overview.object + 0x180 );
			overview.world_to_pixels = dynamic_ok
				? read_dynamic.template operator()<float>( 0x24 )
				: app::context().process.load<float>( overview.object + 0x194 );
			overview.map_center = dynamic_ok
				? read_dynamic.template operator()<foundation::vec2>( 0x40 )
				: app::context().process.load<foundation::vec2>( overview.object + 0x1b0 );
			overview.radar_angle = dynamic_ok
				? read_dynamic.template operator()<float>( 0x4c )
				: app::context().process.load<float>( overview.object + 0x1bc );
			overview.alternate_transform =
				app::context().process.load<std::uint8_t>( overview.object + 0x40 ) != 0;
			overview.alternate_scale =
				app::context().process.load<float>( overview.object + 0x17f6c );

			if ( refresh_layout )
			{
				const auto expected_panel_size = std::max( overview.viewport_size, 290.0f );
				const auto current_raw_width =
					app::context().process.load<float>( overview.panel.ui + 0x1c0 );
				if ( !std::isfinite( current_raw_width )
					|| std::abs( current_raw_width - expected_panel_size ) > 2.0f )
				{
					overview.panel = find_radar_panel(
						overview.object, expected_panel_size, display_width, display_height );
				}
				else
				{
					overview.panel = inspect_panel(
						overview.panel.wrapper, display_width, display_height );
				}
				overview.marker_space =
					marker_space_rect( overview.object, display_width, display_height );
				overview.layout_width = display_width;
				overview.layout_height = display_height;
			}
			const auto viewport = scale_layout_rect(
				overview.panel.viewport, overview, display_width, display_height );
			any_active |= viewport.valid( display_width, display_height )
				&& viewport.x + viewport.w >= 0.0f
				&& viewport.y + viewport.h >= 0.0f
				&& viewport.x <= display_width && viewport.y <= display_height;
		}

		if ( !any_active )
		{
			overviews.clear( );
		}
	}

	[[nodiscard]] bool inside_viewport( const foundation::vec2& point, const rect_t& viewport, bool square, float margin = 3.0f )
	{
		const auto center_x = viewport.x + viewport.w * 0.5f;
		const auto center_y = viewport.y + viewport.h * 0.5f;
		if ( square )
		{
			return point.x >= viewport.x + margin && point.x <= viewport.x + viewport.w - margin &&
				point.y >= viewport.y + margin && point.y <= viewport.y + viewport.h - margin;
		}

		const auto nx = ( point.x - center_x ) / std::max( viewport.w * 0.5f - margin, 1.0f );
		const auto ny = ( point.y - center_y ) / std::max( viewport.h * 0.5f - margin, 1.0f );
		return nx * nx + ny * ny <= 1.0f;
	}

}

namespace features::visuals {

	void radar_t::tick( )
	{
		if ( !radar_active( ) || !game::local_player().pawn( ) )
		{
			return;
		}

		RECT overlay_client{};
		const auto overlay_window = app::context().overlay.hwnd( );
		if ( !overlay_window || !::GetClientRect( overlay_window, &overlay_client ) )
		{
			return;
		}
		const auto display_width = static_cast<float>(
			overlay_client.right - overlay_client.left );
		const auto display_height = static_cast<float>(
			overlay_client.bottom - overlay_client.top );
		if ( display_width < 64.0f || display_height < 64.0f )
		{
			return;
		}

		static std::vector<overview_info> working{};
		refresh_overviews( working, display_width, display_height );
		g_square_always.store(
			game::variables().get<bool>( CONVAR( "cl_radar_square_always"_id ) ),
			std::memory_order_relaxed );
		g_square_with_scoreboard.store(
			game::variables().get<bool>( CONVAR( "cl_radar_square_with_scoreboard"_id ) ),
			std::memory_order_relaxed );
		auto snapshot = std::make_shared<const std::vector<overview_info>>( working );
		g_overview_snapshot.store( std::move( snapshot ), std::memory_order_release );
	}

	void radar_t::on_render( zdraw::draw_list& draw_list )
	{
		if ( !radar_active( ) || !draw_list.m_im_draw_list )
		{
			return;
		}
		if ( config::visual_settings.m_player.spectator_sync
			&& game::world( ).local_spectated( ) ) return;

		const auto [ display_width_i, display_height_i ] = zdraw::get_display_size( );
		const auto display_width = static_cast<float>( display_width_i );
		const auto display_height = static_cast<float>( display_height_i );
		if ( display_width < 64.0f || display_height < 64.0f || !game::local_player().pawn( ) )
		{
			return;
		}

		auto overviews = g_overview_snapshot.load( std::memory_order_acquire );
		const overview_info* active{};
		rect_t active_viewport{};
		for ( const auto& overview : *overviews )
		{
			const auto viewport = scale_layout_rect(
				overview.panel.viewport, overview, display_width, display_height );
			if ( !viewport.valid( display_width, display_height ) || viewport.x + viewport.w < 0.0f || viewport.y + viewport.h < 0.0f ||
				viewport.x > display_width || viewport.y > display_height )
			{
				continue;
			}

			if ( !active || viewport.w * viewport.h > active_viewport.w * active_viewport.h )
			{
				active = &overview;
				active_viewport = viewport;
			}
		}

		if ( !active )
		{
			return;
		}

		const auto viewport = active_viewport;
		const auto tab_mode = ( ::GetAsyncKeyState( VK_TAB ) & 0x8000 ) != 0;
		const auto square = tab_mode || g_square_always.load( std::memory_order_relaxed )
			|| ( g_square_with_scoreboard.load( std::memory_order_relaxed ) && tab_mode );

		const auto marker_space = scale_layout_rect(
			active->marker_space, *active, display_width, display_height );
		const auto marker_space_valid = marker_space.valid( display_width, display_height );
		const auto ui_scale_x = marker_space_valid
			? marker_space.w / 300.0f
			: scale_layout_rect( active->panel.image, *active,
				display_width, display_height ).w / 290.0f;
		const auto ui_scale_y = marker_space_valid
			? marker_space.h / 300.0f
			: scale_layout_rect( active->panel.image, *active,
				display_width, display_height ).h / 290.0f;
		const auto icon_scale = std::min( ui_scale_x, ui_scale_y );
		const auto marker_center_x = marker_space_valid
			? marker_space.x + marker_space.w * 0.5f
			: viewport.x + viewport.w * 0.5f;
		const auto marker_center_y = marker_space_valid
			? marker_space.y + marker_space.h * 0.5f
			: viewport.y + viewport.h * 0.5f;
		const auto canvas_ratio = active->alternate_transform && std::isfinite( active->alternate_scale ) && active->alternate_scale > 0.001f
			? active->alternate_scale
			: active->canvas_size > 1.0f
			? active->viewport_size / active->canvas_size
			: 1.0f;
		const auto world_to_radar = active->world_to_pixels * canvas_ratio;
		const auto rotation = foundation::to_radians( active->radar_angle );
		const auto cosine = std::cos( rotation );
		const auto sine = std::sin( rotation );
		const auto transform = [ & ]( const foundation::vec3& world )
			{

				const auto map_x = ( world.x - active->map_origin.x ) * world_to_radar;
				const auto map_y = ( active->map_origin.y - world.y ) * world_to_radar;
				const auto dx = map_x - active->map_center.x;
				const auto dy = map_y - active->map_center.y;
				const auto rotated_x = dx * cosine - dy * sine;
				const auto rotated_y = dx * sine + dy * cosine;
				foundation::vec2 point{ marker_center_x + rotated_x * ui_scale_x,
					marker_center_y + rotated_y * ui_scale_y };
				return point;
			};
		draw_list.m_im_draw_list->PushClipRect( { viewport.x, viewport.y }, { viewport.x + viewport.w, viewport.y + viewport.h }, true );
		const auto& radar_cfg = config::visual_settings.m_radar;
		const auto packed = []( const zdraw::rgba& color )
			{ return zdraw::draw_list::to_im_color( color ); };
		const auto enemy_marker_color = packed( radar_cfg.enemy_color );
		if ( radar_cfg.show_projectiles || radar_cfg.show_trajectories
			|| radar_cfg.show_grenade_zones )
		{
			const auto projectiles = game::world().projectiles();
			struct cached_path
			{
				foundation::vec3 initial_position{};
				foundation::vec3 initial_velocity{};
				std::uintptr_t weapon{};
				grenade_path path{};
			};
			static std::unordered_map<std::uintptr_t, cached_path> paths{};
			static grenade_trajectory_engine trajectory{};
			static thread_local std::vector<std::uintptr_t> live{};
			live.clear();
			live.reserve( projectiles->size() );
			static thread_local std::vector<ImVec2> trajectory_points{};
			static thread_local std::vector<ImVec2> zone_points{};
			static thread_local std::vector<ImVec2> zone_hull{};
			const auto build_hull = []( std::vector<ImVec2>& points,
				std::vector<ImVec2>& hull )
			{
				hull.clear();
				if ( points.size() < 3 ) return;
				std::ranges::sort( points, {}, []( const ImVec2& p )
					{ return std::pair{ p.x, p.y }; } );
				const auto cross = []( const ImVec2& a, const ImVec2& b, const ImVec2& c )
					{ return ( b.x - a.x ) * ( c.y - a.y )
						- ( b.y - a.y ) * ( c.x - a.x ); };
				for ( const auto& point : points )
				{
					while ( hull.size() >= 2 && cross( hull[ hull.size() - 2 ],
						hull.back(), point ) <= 0.0f ) hull.pop_back();
					hull.push_back( point );
				}
				const auto lower = hull.size();
				for ( auto it = points.rbegin() + 1; it != points.rend(); ++it )
				{
					while ( hull.size() > lower && cross( hull[ hull.size() - 2 ],
						hull.back(), *it ) <= 0.0f ) hull.pop_back();
					hull.push_back( *it );
				}
				if ( hull.size() > 1 ) hull.pop_back();
			};
			const auto kind_data = [ & ]( game::projectile_kind kind )
			{
				struct result { std::uintptr_t weapon{}; const char* label{}; ImU32 color{}; float radius{}; };
				switch ( kind )
				{
				case game::projectile_kind::he_grenade: return result{ "weapon_hegrenade"_id, "HE", packed( radar_cfg.he_color ), 0.0f };
				case game::projectile_kind::flashbang: return result{ "weapon_flashbang"_id, "FLASH", packed( radar_cfg.flash_color ), 0.0f };
				case game::projectile_kind::smoke_grenade: return result{ "weapon_smokegrenade"_id, "SMOKE", packed( radar_cfg.smoke_color ), 144.0f };
				case game::projectile_kind::molotov: return result{ "weapon_molotov"_id, "FIRE", packed( radar_cfg.molotov_color ), 0.0f };
				case game::projectile_kind::molotov_fire: return result{ "weapon_molotov"_id, "FIRE", packed( radar_cfg.molotov_color ), 0.0f };
				case game::projectile_kind::decoy: return result{ "weapon_decoy"_id, "DECOY", packed( radar_cfg.decoy_color ), 0.0f };
				default: return result{ 0, "GRENADE", IM_COL32( 230, 230, 235, 255 ), 100.0f };
				}
			};
			for ( const auto& projectile : *projectiles )
			{
				if ( !projectile.entity || !std::isfinite( projectile.origin.x )
					|| !std::isfinite( projectile.origin.y ) ) continue;
				live.push_back( projectile.entity );
				const auto data = kind_data( projectile.subtype );
				auto& cached = paths[ projectile.entity ];
				const auto finite = []( const foundation::vec3& value )
				{
					return std::isfinite( value.x ) && std::isfinite( value.y )
						&& std::isfinite( value.z );
				};
				const auto has_initial_velocity = finite( projectile.initial_velocity )
					&& projectile.initial_velocity.length_sqr() > 25.0f;
				const auto launch_position = finite( projectile.initial_position )
					&& projectile.initial_position.length_sqr() > 1.0f
					? projectile.initial_position : projectile.origin;
				const auto launch_velocity = has_initial_velocity
					? projectile.initial_velocity : projectile.velocity;
				const auto moving = projectile.launch_valid && projectile.in_flight;
				if ( radar_cfg.show_trajectories && moving && data.weapon
					&& ( cached.weapon != data.weapon || !cached.path.valid
						|| cached.initial_position.distance_sqr( launch_position ) > 1.0f
						|| cached.initial_velocity.distance_sqr( launch_velocity ) > 1.0f ) )
				{
					cached.initial_position = launch_position;
					cached.initial_velocity = launch_velocity;
					cached.weapon = data.weapon;

					cached.path = trajectory.predict( launch_position, launch_velocity,
						data.weapon, -1.0f );
				}
				if ( radar_cfg.show_trajectories && moving
					&& cached.path.valid && cached.path.points.size() > 1 )
				{
					trajectory_points.clear();
					trajectory_points.reserve( cached.path.points.size() );
					for ( const auto& world : cached.path.points )
					{
						const auto p = transform( world );
						trajectory_points.push_back( { p.x, p.y } );
					}
					draw_list.m_im_draw_list->AddPolyline( trajectory_points.data(),
						static_cast<int>( trajectory_points.size() ), data.color, ImDrawFlags_None,
						std::max( 0.5f, radar_cfg.trajectory_thickness * icon_scale ) );
					const auto endpoint = trajectory_points.back();
					draw_list.m_im_draw_list->AddCircleFilled( endpoint,
						std::max( 1.0f, radar_cfg.trajectory_endpoint_size * icon_scale ), data.color, 16 );
				}
				const auto display_origin = projectile.smoke_active
					&& projectile.smoke_detonation_pos.length_sqr() > 1.0f
					? projectile.smoke_detonation_pos : projectile.origin;
				const auto point = transform( display_origin );
				if ( !inside_viewport( point, viewport, square, 3.0f ) ) continue;

				if ( radar_cfg.show_grenade_zones && ( projectile.smoke_active
					|| projectile.subtype == game::projectile_kind::molotov_fire ) )
				{
					const auto fill_alpha = static_cast<ImU32>( std::lround(
						std::clamp( radar_cfg.zone_fill_alpha, 0.0f, 100.0f ) * 2.55f ) );
					const auto outline_alpha = static_cast<ImU32>( std::lround(
						std::clamp( radar_cfg.zone_outline_alpha, 0.0f, 100.0f ) * 2.55f ) );
					if ( projectile.subtype == game::projectile_kind::molotov_fire
						&& !projectile.fire_points.empty() )
					{
						zone_points.clear();
						zone_points.reserve( projectile.fire_points.size() * 12 );
						for ( const auto& fire : projectile.fire_points )
							for ( int sample = 0; sample < 12; ++sample )
							{
								const auto angle = static_cast<float>( sample )
									* ( 2.0f * std::numbers::pi_v<float> / 12.0f );
								const auto edge = transform( fire + foundation::vec3{
									std::cos( angle ) * 60.0f, std::sin( angle ) * 60.0f, 0.0f } );
								zone_points.push_back( { edge.x, edge.y } );
							}
						build_hull( zone_points, zone_hull );
						if ( zone_hull.size() >= 3 )
						{
							draw_list.m_im_draw_list->AddConvexPolyFilled( zone_hull.data(),
								static_cast<int>( zone_hull.size() ),
								( data.color & 0x00ffffffu ) | ( fill_alpha << 24 ) );
							draw_list.m_im_draw_list->AddPolyline( zone_hull.data(),
								static_cast<int>( zone_hull.size() ),
								( data.color & 0x00ffffffu ) | ( outline_alpha << 24 ),
								ImDrawFlags_Closed, std::max( 0.5f, radar_cfg.zone_outline_thickness * icon_scale ) );
						}
					}
					else
					{
						const auto edge = transform( display_origin
							+ foundation::vec3{ data.radius, 0.0f, 0.0f } );
						const auto screen_radius = std::clamp(
							std::hypot( edge.x - point.x, edge.y - point.y ), 3.0f, viewport.w );
						draw_list.m_im_draw_list->AddCircleFilled( { point.x, point.y }, screen_radius,
							( data.color & 0x00ffffffu ) | ( fill_alpha << 24 ), 32 );
						draw_list.m_im_draw_list->AddCircle( { point.x, point.y }, screen_radius,
							( data.color & 0x00ffffffu ) | ( outline_alpha << 24 ), 32,
							std::max( 0.5f, radar_cfg.zone_outline_thickness * icon_scale ) );
					}
				}
				if ( radar_cfg.show_projectiles )
				{
					draw_list.m_im_draw_list->AddCircleFilled( { point.x, point.y },
						std::clamp( 3.0f * icon_scale, 2.0f, 5.0f ), data.color );
					if ( auto* font = ImGui::GetFont() )
					{
						const auto size = std::max( 7.0f, ImGui::GetFontSize() * icon_scale * 0.62f );
						const ImVec2 label_pos{ point.x + 4.0f * icon_scale, point.y - size * 0.5f };
						if ( radar_cfg.text_outline )
						{
							const auto offset = std::max( 0.5f, radar_cfg.text_outline_thickness * icon_scale );
							const auto outline = packed( radar_cfg.text_outline_color );
							for ( const auto delta : { ImVec2{ -offset, 0 }, ImVec2{ offset, 0 },
								ImVec2{ 0, -offset }, ImVec2{ 0, offset } } )
								draw_list.m_im_draw_list->AddText( font, size, label_pos + delta, outline, data.label );
						}
						draw_list.m_im_draw_list->AddText( font, size, label_pos, data.color, data.label );
					}
				}
			}
			std::erase_if( paths, [ & ]( const auto& item )
				{ return std::ranges::find( live, item.first ) == live.end(); } );
		}
		const auto players = game::world().players( );
		for ( const auto& player : *players )
		{

			if ( player.health <= 0 || !game::local_player().is_enemy( player.team ) ||
				!player.pawn || !player.game_scene_node )
			{
				continue;
			}
			if ( !std::isfinite( player.origin.x ) || !std::isfinite( player.origin.y ) )
			{
				continue;
			}

			auto point = transform( player.origin );
			const auto marker_radius = std::clamp( 5.0f * icon_scale
				* std::clamp( radar_cfg.marker_scale, 0.5f, 2.0f ), 2.0f, 14.0f );
			const auto point_inside = inside_viewport( point, viewport, square, marker_radius * 2.0f + 2.0f );
			const auto center_x = viewport.x + viewport.w * 0.5f;
			const auto center_y = viewport.y + viewport.h * 0.5f;
			const auto edge_dx = point.x - center_x;
			const auto edge_dy = point.y - center_y;
			const auto edge_length = std::sqrt( edge_dx * edge_dx + edge_dy * edge_dy );
			if ( !point_inside && edge_length > 0.001f )
			{
				const auto edge_icon_size = marker_radius;
				const auto max_x = viewport.w * 0.5f - edge_icon_size - 3.0f;
				const auto max_y = viewport.h * 0.5f - edge_icon_size - 3.0f;
				float edge_scale{};
				if ( square )
				{
					edge_scale = std::min( max_x / std::max( std::abs( edge_dx ), 0.001f ),
						max_y / std::max( std::abs( edge_dy ), 0.001f ) );
				}
				else
				{

					const auto normalized_x = edge_dx / std::max( max_x, 1.0f );
					const auto normalized_y = edge_dy / std::max( max_y, 1.0f );
					edge_scale = 1.0f / std::sqrt( normalized_x * normalized_x + normalized_y * normalized_y );
				}

				const auto direction_x = edge_dx / edge_length;
				const auto direction_y = edge_dy / edge_length;
				point = { center_x + edge_dx * edge_scale, center_y + edge_dy * edge_scale };
				const auto base_x = point.x - direction_x * edge_icon_size * 1.7f;
				const auto base_y = point.y - direction_y * edge_icon_size * 1.7f;
				const auto perpendicular_x = -direction_y * edge_icon_size * 0.8f;
				const auto perpendicular_y = direction_x * edge_icon_size * 0.8f;
				const auto outline = IM_COL32( 10, 10, 13, 225 );
				const auto outline_back_x = point.x - direction_x * edge_icon_size * 2.0f;
				const auto outline_back_y = point.y - direction_y * edge_icon_size * 2.0f;
				const auto outline_perpendicular_x = -direction_y * edge_icon_size;
				const auto outline_perpendicular_y = direction_x * edge_icon_size;
				draw_list.m_im_draw_list->AddTriangleFilled(
					{ point.x, point.y },
					{ outline_back_x + outline_perpendicular_x, outline_back_y + outline_perpendicular_y },
					{ outline_back_x - outline_perpendicular_x, outline_back_y - outline_perpendicular_y },
					outline );
				draw_list.m_im_draw_list->AddTriangleFilled(
					{ point.x, point.y },
					{ base_x + perpendicular_x, base_y + perpendicular_y },
					{ base_x - perpendicular_x, base_y - perpendicular_y },
					enemy_marker_color );
				continue;
			}

			const auto eye_angle = foundation::to_radians( player.eye_angles.y );
			const auto direction_x = std::cos( eye_angle );
			const auto direction_y = -std::sin( eye_angle );
			const auto radar_direction_x = direction_x * cosine - direction_y * sine;
			const auto radar_direction_y = direction_x * sine + direction_y * cosine;
			const auto screen_direction_x_raw = radar_direction_x * ui_scale_x;
			const auto screen_direction_y_raw = radar_direction_y * ui_scale_y;
			const auto screen_direction_length = std::max( std::sqrt( screen_direction_x_raw * screen_direction_x_raw +
				screen_direction_y_raw * screen_direction_y_raw ), 0.001f );
			const auto screen_direction_x = screen_direction_x_raw / screen_direction_length;
			const auto screen_direction_y = screen_direction_y_raw / screen_direction_length;
			const auto perpendicular_x = -screen_direction_y;
			const auto perpendicular_y = screen_direction_x;

			const auto triangle_tip_x = point.x + screen_direction_x * marker_radius * 1.40f;
			const auto triangle_tip_y = point.y + screen_direction_y * marker_radius * 1.40f;
			const auto triangle_base_x = point.x + screen_direction_x * marker_radius * 0.55f;
			const auto triangle_base_y = point.y + screen_direction_y * marker_radius * 0.55f;
			const auto triangle_half_width = marker_radius * 0.65f;

			draw_list.m_im_draw_list->AddCircleFilled( { point.x, point.y }, marker_radius, enemy_marker_color );
			draw_list.m_im_draw_list->AddTriangleFilled(
				{ triangle_tip_x, triangle_tip_y },
				{ triangle_base_x + perpendicular_x * triangle_half_width, triangle_base_y + perpendicular_y * triangle_half_width },
				{ triangle_base_x - perpendicular_x * triangle_half_width, triangle_base_y - perpendicular_y * triangle_half_width },
				packed( radar_cfg.direction_color ) );

			const auto info_scale = std::clamp( radar_cfg.information_scale, 0.5f, 1.5f );
			const auto text_size = std::max( 7.0f,
				ImGui::GetFontSize() * icon_scale * info_scale );
			auto* font = ImGui::GetFont();
			const auto centered_text = [ & ]( const std::string_view value, float y, ImU32 color )
			{
				if ( value.empty() ) return;
				const auto width = font->CalcTextSizeA( text_size, FLT_MAX, 0.0f,
					value.data(), value.data() + value.size() ).x;
				const ImVec2 position{ point.x - width * 0.5f, y };
				if ( radar_cfg.text_outline )
				{
					const auto offset = std::max( 0.5f, radar_cfg.text_outline_thickness * icon_scale );
					const auto outline = packed( radar_cfg.text_outline_color );
					for ( const auto delta : { ImVec2{ -offset, 0 }, ImVec2{ offset, 0 },
						ImVec2{ 0, -offset }, ImVec2{ 0, offset } } )
						draw_list.m_im_draw_list->AddText( font, text_size, position + delta,
							outline, value.data(), value.data() + value.size() );
				}
				draw_list.m_im_draw_list->AddText( font, text_size, position, color,
					value.data(), value.data() + value.size() );
			};
			if ( radar_cfg.show_names )
				centered_text( player.display_name, point.y - marker_radius - text_size - 2.0f,
					packed( radar_cfg.name_color ) );
			auto info_y = point.y + marker_radius + 2.0f;
			if ( radar_cfg.show_weapon && !player.weapon.name.empty() )
			{
				auto name = std::string_view( player.weapon.name );
				if ( name.starts_with( "weapon_" ) ) name.remove_prefix( 7 );
				centered_text( name, info_y,
					packed( radar_cfg.weapon_color ) );
				info_y += text_size + 2.0f;
			}
			const auto bar_height = std::max( 1.0f, 1.5f * icon_scale );
			const auto bar_width = marker_radius * 2.8f;
			if ( radar_cfg.show_health )
			{
				const auto y = info_y;
				draw_list.m_im_draw_list->AddRectFilled( { point.x - bar_width * 0.5f, y },
					{ point.x + bar_width * 0.5f, y + bar_height }, IM_COL32( 15, 18, 24, 220 ) );
				draw_list.m_im_draw_list->AddRectFilled( { point.x - bar_width * 0.5f, y },
					{ point.x - bar_width * 0.5f + bar_width * std::clamp( player.health / 100.0f, 0.0f, 1.0f ), y + bar_height },
					packed( radar_cfg.health_color ) );
				info_y += bar_height + 2.0f;
			}
			if ( radar_cfg.show_armor )
			{
				const auto y = info_y;
				draw_list.m_im_draw_list->AddRectFilled( { point.x - bar_width * 0.5f, y },
					{ point.x + bar_width * 0.5f, y + bar_height }, IM_COL32( 15, 18, 24, 220 ) );
				draw_list.m_im_draw_list->AddRectFilled( { point.x - bar_width * 0.5f, y },
					{ point.x - bar_width * 0.5f + bar_width * std::clamp( player.armor / 100.0f, 0.0f, 1.0f ), y + bar_height },
					packed( radar_cfg.armor_color ) );
				info_y += bar_height + 2.0f;
			}
			if ( radar_cfg.show_health || radar_cfg.show_armor )
			{
				char status[ 32 ]{};
				if ( radar_cfg.show_health && radar_cfg.show_armor )
					std::snprintf( status, sizeof( status ), "%d HP  %d AR",
						std::clamp( player.health, 0, 100 ), std::clamp( player.armor, 0, 100 ) );
				else if ( radar_cfg.show_health )
					std::snprintf( status, sizeof( status ), "%d HP", std::clamp( player.health, 0, 100 ) );
				else std::snprintf( status, sizeof( status ), "%d AR", std::clamp( player.armor, 0, 100 ) );
				centered_text( status, info_y, packed( radar_cfg.status_color ) );
			}
		}
		draw_list.m_im_draw_list->PopClipRect( );
	}

}
