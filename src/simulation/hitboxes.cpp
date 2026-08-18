#include <stdafx.hpp>

namespace game {

	namespace {

		constexpr auto k_hitbox_stride{ 0x70 };

		struct field_layout
		{
			int mins;
			int maxs;
			int radius;
			int name;
		};

		constexpr field_layout k_layouts[]
		{
			{ 0x20, 0x2C, 0x50, 0x38 },
			{ 0x18, 0x24, 0x30, 0x00 },
		};

		struct recipe
		{
			int handle_off;
			int mid_off;
			bool mid_deref;
			int set_off;
		};

		constexpr recipe k_known[]
		{
			{ 0xC0, 0x78, true,  0x168 },
			{ 0xC0, 0x78, true,  0x150 },
			{ 0xC0, 0x78, true,  0x140 },
			{ 0xD0, 0x78, true,  0x140 },
			{ 0xD0, 0x78, true,  0x150 },
			{ 0xD0, 0x78, true,  0x168 },
		};

		bool plausible_pointer( std::uintptr_t p )
		{
			return p > 0x10000 && p < 0x7FFFFFFFFFFF;
		}

		bool plausible_vec( const foundation::vec3& v )
		{
			return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z )
				&& std::fabsf( v.x ) < 200.0f && std::fabsf( v.y ) < 200.0f && std::fabsf( v.z ) < 200.0f;
		}

		bool plausible_entry( const std::byte* buf, int index, const field_layout& fl )
		{
			const auto off = index * k_hitbox_stride;
			foundation::vec3 mins{};
			foundation::vec3 maxs{};
			float radius{};
			std::memcpy( &mins, buf + off + fl.mins, sizeof( mins ) );
			std::memcpy( &maxs, buf + off + fl.maxs, sizeof( maxs ) );
			std::memcpy( &radius, buf + off + fl.radius, sizeof( radius ) );

			if ( !std::isfinite( radius ) || radius < 0.5f || radius > 40.0f )
			{
				return false;
			}

			return plausible_vec( mins ) && plausible_vec( maxs );
		}

		int count_valid_entries( const std::byte* buf, int count, const field_layout& fl )
		{
			int valid = 0;
			for ( int i = 0; i < count; ++i )
			{
				if ( plausible_entry( buf, i, fl ) )
				{
					++valid;
				}
			}
			return valid;
		}

		struct resolved_set
		{
			std::vector<std::byte> buffer{};
			int count{};
			int layout{ -1 };
			std::uintptr_t model_handle{};
		};

		bool walk_recipe( std::uintptr_t model_state, const recipe& r, bool strict, resolved_set& out )
		{
			const auto model_handle = app::context().process.load<std::uintptr_t>( model_state + r.handle_off );
			if ( !plausible_pointer( model_handle ) )
			{
				return false;
			}

			const auto cmodel = app::context().process.load<std::uintptr_t>( model_handle );
			if ( !plausible_pointer( cmodel ) )
			{
				return false;
			}

			auto base = cmodel;

			if ( r.mid_off >= 0 )
			{
				base = app::context().process.load<std::uintptr_t>( cmodel + r.mid_off );
				if ( !plausible_pointer( base ) )
				{
					return false;
				}

				if ( r.mid_deref )
				{
					base = app::context().process.load<std::uintptr_t>( base );
					if ( !plausible_pointer( base ) )
					{
						return false;
					}
				}
			}

			const auto hitbox_set = app::context().process.load<std::uintptr_t>( base + r.set_off );
			if ( !plausible_pointer( hitbox_set ) )
			{
				return false;
			}

			const auto count = app::context().process.load<int>( hitbox_set + 0x28 );
			const auto array_ptr = app::context().process.load<std::uintptr_t>( hitbox_set + 0x30 );

			const int min_count = strict ? 12 : 4;
			if ( !plausible_pointer( array_ptr ) || count < min_count || count > 40 )
			{
				return false;
			}

			std::vector<std::byte> buf( static_cast< std::size_t >( count ) * k_hitbox_stride );
			if ( !app::context().process.copy( array_ptr, buf.data( ), buf.size( ) ) )
			{
				return false;
			}

			for ( int li = 0; li < static_cast< int >( std::size( k_layouts ) ); ++li )
			{
				const auto valid = count_valid_entries( buf.data( ), count, k_layouts[ li ] );
				const auto need = strict ? std::max( 8, count * 3 / 4 ) : std::max( 3, count / 2 );

				if ( valid >= need )
				{
					out.buffer = std::move( buf );
					out.count = count;
					out.layout = li;
					out.model_handle = model_handle;
					return true;
				}
			}

			return false;
		}

		bool deep_scan( std::uintptr_t model_state, recipe& out_recipe, resolved_set& out )
		{
			constexpr int handle_offs[] = { 0xC0, 0xD0, 0xA0, 0xA8, 0xB0, 0xB8, 0xC8, 0xD8, 0xE0 };
			constexpr int mid_offs[] = { -1, 0x68, 0x70, 0x78, 0x80, 0x88 };

			for ( const auto h : handle_offs )
			{
				for ( const auto m : mid_offs )
				{
					for ( int d = 0; d < ( m < 0 ? 1 : 2 ); ++d )
					{
						for ( int s = 0x00; s <= 0x1F8; s += 8 )
						{
							const recipe r{ h, m, d != 0, s };

							if ( walk_recipe( model_state, r, true, out ) )
							{
								out_recipe = r;
								return true;
							}
						}
					}
				}
			}

			return false;
		}
	}

	hitbox_catalog::set hitbox_catalog::query( std::uintptr_t game_scene_node,
		bool use_geometry_cache ) const
	{
		set result{};
		if ( !game_scene_node )
		{
			return result;
		}

#ifdef _DEBUG
		const bool do_debug = ( GetAsyncKeyState( VK_F8 ) & 0x8000 ) != 0;
#else
		constexpr bool do_debug = false;
#endif
		thread_local bool printed_this_press = false;
		const bool dbg = do_debug && !printed_this_press;

		const auto model_state_offset = SCHEMA( "CSkeletonInstance", "m_modelState"_id );
		const auto model_state = game_scene_node + ( model_state_offset ? model_state_offset : 0x160 );

		thread_local recipe s_cached{};
		thread_local bool s_has_cached = false;
		struct node_cache_entry
		{
			int handle_off{};
			std::uintptr_t model_handle{};
			set geometry{};
		};
		thread_local std::unordered_map<std::uintptr_t, node_cache_entry> s_node_cache{};

		if ( use_geometry_cache && !do_debug )
		{
			if ( const auto it = s_node_cache.find( game_scene_node ); it != s_node_cache.end( ) )
			{
				const auto current_handle = app::context().process.load<std::uintptr_t>(
					model_state + static_cast<std::uintptr_t>( it->second.handle_off ) );
				if ( current_handle == it->second.model_handle )
				{
					return it->second.geometry;
				}
				s_node_cache.erase( it );
			}
			if ( s_node_cache.size( ) > 256 )
			{
				s_node_cache.clear( );
			}
		}

		resolved_set rs{};
		bool resolved = false;

		if ( s_has_cached )
		{
			resolved = walk_recipe( model_state, s_cached, false, rs );
			if ( !resolved )
			{
				s_has_cached = false;
			}
		}

		if ( !resolved )
		{
			for ( const auto& r : k_known )
			{
				if ( walk_recipe( model_state, r, false, rs ) )
				{
					s_cached = r;
					s_has_cached = true;
					resolved = true;
					break;
				}
			}
		}

		if ( !resolved && dbg )
		{
			app::context().diagnostics.info( "\n=== HITBOX DEEP SCAN ===" );
			app::context().diagnostics.info( "model_state=0x{:X} (offset 0x{:X}), scanning...", model_state, model_state_offset );

			recipe found{};
			if ( deep_scan( model_state, found, rs ) )
			{
				s_cached = found;
				s_has_cached = true;
				resolved = true;

				app::context().diagnostics.info( ">>> FOUND: model_state+0x{:X} -> deref -> {}{} -> set+0x{:X} (layout {})",
					found.handle_off,
					found.mid_off < 0 ? std::string( "cmodel" ) : std::format( "cmodel+0x{:X}", found.mid_off ),
					found.mid_off >= 0 && found.mid_deref ? " -> deref" : "",
					found.set_off, rs.layout );
			}
			else
			{
				app::context().diagnostics.warning( "deep scan found nothing -- chain is deeper than 3 hops or fields changed" );
			}
		}

		if ( resolved )
		{
			const auto& fl = k_layouts[ rs.layout ];

			for ( int i = 0; i < rs.count && result.count < static_cast< int >( result.entries.size( ) ); ++i )
			{
				if ( !plausible_entry( rs.buffer.data( ), i, fl ) )
				{
					continue;
				}

				const auto off = i * k_hitbox_stride;

				auto& hb = result.entries[ result.count++ ];
				hb.index = i;
				hb.bone = i < static_cast< int >( std::size( k_bone_map ) ) ? k_bone_map[ i ] : i;
				std::uintptr_t name_ptr{};
				std::memcpy( &name_ptr, rs.buffer.data( ) + off + fl.name,
					sizeof( name_ptr ) );
				if ( plausible_pointer( name_ptr ) )
					static_cast<void>( app::context().process.copy(
						name_ptr, hb.name.data( ), hb.name.size( ) - 1 ) );
				const std::string_view name{ hb.name.data( ) };
				if ( name == "head" ) hb.bone = game::rules::joint_id::head;
				else if ( name == "neck_0" || name == "neck" )
					hb.bone = game::rules::joint_id::neck;
				std::memcpy( &hb.mins, rs.buffer.data( ) + off + fl.mins,
					sizeof( hb.mins ) );
				std::memcpy( &hb.maxs, rs.buffer.data( ) + off + fl.maxs,
					sizeof( hb.maxs ) );
				std::memcpy( &hb.radius, rs.buffer.data( ) + off + fl.radius,
					sizeof( hb.radius ) );
			}

			if ( dbg )
			{
				app::context().diagnostics.info( "\n=== HITBOX OK ===" );
				app::context().diagnostics.info( "recipe: handle+0x{:X} mid={} deref={} set+0x{:X} layout={} count={} parsed={}",
					s_cached.handle_off, s_cached.mid_off, s_cached.mid_deref, s_cached.set_off, rs.layout, rs.count, result.count );

				for ( int i = 0; i < result.count && i < 8; ++i )
				{
					const auto& hb = result.entries[ i ];

					char name_buf[ 16 ]{};
					std::uintptr_t name_ptr{};
					std::memcpy( &name_ptr, rs.buffer.data( )
						+ hb.index * k_hitbox_stride + k_layouts[ rs.layout ].name,
						sizeof( name_ptr ) );
					if ( plausible_pointer( name_ptr ) )
					{
						if ( !app::context().process.copy( name_ptr, name_buf, sizeof( name_buf ) - 1 ) )
						{
							name_buf[ 0 ] = '?';
						}
					}

					app::context().diagnostics.info( "  hb[{}] bone={} name=\"{}\" r={:.2f} mins=({:.1f},{:.1f},{:.1f}) maxs=({:.1f},{:.1f},{:.1f})",
						hb.index, hb.bone, name_buf, hb.radius, hb.mins.x, hb.mins.y, hb.mins.z, hb.maxs.x, hb.maxs.y, hb.maxs.z );
				}
			}

			if ( use_geometry_cache && result.count > 0
				&& plausible_pointer( rs.model_handle ) )
			{
				s_node_cache[ game_scene_node ] = node_cache_entry{
					.handle_off = s_cached.handle_off,
					.model_handle = rs.model_handle,
					.geometry = result,
				};
			}
		}

		if ( dbg )
		{
			app::context().diagnostics.info( "===============================\n" );
			printed_this_press = true;
		}
		else if ( !do_debug )
		{
			printed_this_press = false;
		}

		return result;
	}

	int hitbox_catalog::hitgroup_from_hitbox( int hitbox ) const
	{

		switch ( hitbox )
		{
		case 0:
		case 1:  return 1;
		case 2:
		case 3:  return 3;
		case 4:
		case 5:
		case 6:  return 2;
		case 7:
		case 9:
		case 11: return 6;
		case 8:
		case 10:
		case 12: return 7;
		case 13:
		case 15:
		case 16: return 4;
		case 14:
		case 17:
		case 18: return 5;
		default: return 2;
		}
	}

	void hitbox_catalog::remember( const set& snapshot )
	{
		if ( snapshot.count <= 0 )
		{
			return;
		}

		std::unique_lock lock( this->m_snapshot_mutex );
		this->m_snapshot = snapshot;
	}

	hitbox_catalog::set hitbox_catalog::snapshot( ) const
	{
		std::shared_lock lock( this->m_snapshot_mutex );
		return this->m_snapshot;
	}

}
