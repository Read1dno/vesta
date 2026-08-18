#include <stdafx.hpp>

#include <simulation/collision_segments.hpp>

namespace game {

	namespace detail {

		static constexpr std::size_t k_inner_node_size{ 32 };
		static constexpr std::size_t k_outer_node_size{ 48 };

		struct packed_mesh_node
		{
			float min[ 3 ];
			std::uint32_t packed0;
			float max[ 3 ];
			std::uint32_t packed1;

			[[nodiscard]] std::uint32_t type( ) const { return packed0 >> 30; }
			[[nodiscard]] std::uint32_t payload( ) const { return packed0 & 0x3FFFFFFFu; }
		};

		struct half_edge_record
		{
			std::uint8_t next;
			std::uint8_t twin;
			std::uint8_t vert;
			std::uint8_t face;
		};

		struct quaternion { float x, y, z, w; };
		struct rotation_basis { float m[ 3 ][ 3 ]; };

		static rotation_basis make_rotation_basis( const quaternion& q )
		{
			const auto xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const auto xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const auto wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			rotation_basis m{};
			m.m[ 0 ][ 0 ] = 1 - 2 * ( yy + zz );
			m.m[ 0 ][ 1 ] = 2 * ( xy + wz );
			m.m[ 0 ][ 2 ] = 2 * ( xz - wy );
			m.m[ 1 ][ 0 ] = 2 * ( xy - wz );
			m.m[ 1 ][ 1 ] = 1 - 2 * ( xx + zz );
			m.m[ 1 ][ 2 ] = 2 * ( yz + wx );
			m.m[ 2 ][ 0 ] = 2 * ( xz + wy );
			m.m[ 2 ][ 1 ] = 2 * ( yz - wx );
			m.m[ 2 ][ 2 ] = 1 - 2 * ( xx + yy );
			return m;
		}

		static foundation::vec3 rotate_vector( const rotation_basis& m, const foundation::vec3& v )
		{
			return
			{
				m.m[ 0 ][ 0 ] * v.x + m.m[ 1 ][ 0 ] * v.y + m.m[ 2 ][ 0 ] * v.z,
				m.m[ 0 ][ 1 ] * v.x + m.m[ 1 ][ 1 ] * v.y + m.m[ 2 ][ 1 ] * v.z,
				m.m[ 0 ][ 2 ] * v.x + m.m[ 1 ][ 2 ] * v.y + m.m[ 2 ][ 2 ] * v.z
			};
		}

		static foundation::vec3 place_vertex( const rotation_basis& rot, const float scale[ 3 ], const float pos[ 3 ], const foundation::vec3& local )
		{
			const auto scaled = foundation::vec3{ local.x * scale[ 0 ], local.y * scale[ 1 ], local.z * scale[ 2 ] };
			const auto rotated = rotate_vector( rot, scaled );
			return { rotated.x + pos[ 0 ], rotated.y + pos[ 1 ], rotated.z + pos[ 2 ] };
		}

		struct triangle_span
		{
			std::uint32_t first{};
			std::uint32_t count{};
		};

		struct mesh_leaf_map
		{
			std::vector<triangle_span> spans{};
			std::uint32_t first_triangle{ UINT32_MAX };
			std::uint32_t past_last_triangle{};
		};

		template <typename T>
		[[nodiscard]] std::optional<std::vector<T>> read_remote_array(
			std::uintptr_t address, std::size_t count )
		{
			if ( !address || count == 0 )
				return std::nullopt;
			std::vector<T> values( count );
			if ( !app::context().process.copy(
				address, values.data( ), values.size( ) * sizeof( T ) ) )
			{
				return std::nullopt;
			}
			return values;
		}

		template <typename T, std::size_t Size>
		[[nodiscard]] T blob_field( const std::array<std::uint8_t, Size>& bytes,
			std::size_t offset )
		{
			T value{};
			if ( offset + sizeof( T ) <= bytes.size( ) )
				std::memcpy( &value, bytes.data( ) + offset, sizeof( T ) );
			return value;
		}

		[[nodiscard]] std::optional<mesh_leaf_map> discover_mesh_leaves(
			std::span<const packed_mesh_node> nodes )
		{
			mesh_leaf_map result{};
			std::vector<std::uint32_t> deferred{};
			deferred.reserve( 128 );
			auto cursor = 0u;

			const auto resume = [ & ]( ) -> bool
			{
				if ( deferred.empty( ) )
					return false;
				cursor = deferred.back( );
				deferred.pop_back( );
				return true;
			};

			for ( ;; )
			{
				if ( cursor >= nodes.size( ) )
				{
					if ( !resume( ) ) break;
					continue;
				}

				const auto& node = nodes[ cursor ];
				const auto stride = node.payload( );
				if ( node.type( ) == 3 )
				{
					if ( stride > 0 && stride < 0x1000000 )
					{
						result.spans.push_back( { node.packed1, stride } );
						result.first_triangle = std::min( result.first_triangle, node.packed1 );
						result.past_last_triangle = std::max(
							result.past_last_triangle, node.packed1 + stride );
					}
					if ( !resume( ) ) break;
					continue;
				}

				if ( stride == 0 )
				{
					if ( !resume( ) ) break;
					continue;
				}
				if ( cursor + stride < nodes.size( ) )
					deferred.push_back( cursor + stride );
				++cursor;
			}

			if ( result.spans.empty( )
				|| result.past_last_triangle <= result.first_triangle )
			{
				return std::nullopt;
			}
			return result;
		}

		static bool decode_mesh_shape( std::uintptr_t bvh_ptr, std::uintptr_t vert_ptr,
			std::uintptr_t tri_ptr, std::uint32_t node_count, const rotation_basis& rotation,
			const float scale[ 3 ], const float position[ 3 ], std::uintptr_t material_ptr,
			std::int32_t material_count,
			const std::vector<collision_world::global_surface_entry>& surface_table,
			const collision_world::surface_info& fallback_surface,
			std::vector<collision_world::triangle>& output, std::uint64_t solid_id )
		{
			if ( !bvh_ptr || !vert_ptr || !tri_ptr || node_count == 0
				|| node_count > 0x1000000 )
			{
				return false;
			}
			static_assert( sizeof( packed_mesh_node ) == k_inner_node_size );
			const auto nodes = read_remote_array<packed_mesh_node>( bvh_ptr, node_count );
			if ( !nodes ) return false;
			const auto leaves = discover_mesh_leaves(
				std::span<const packed_mesh_node>{ *nodes } );
			if ( !leaves ) return false;

			const auto triangle_count = leaves->past_last_triangle - leaves->first_triangle;
			if ( triangle_count > 0x1000000 ) return false;
			const auto indices = read_remote_array<std::int32_t>(
				tri_ptr + static_cast<std::uintptr_t>( leaves->first_triangle ) * 12,
				static_cast<std::size_t>( triangle_count ) * 3 );
			if ( !indices ) return false;
			const auto highest_vertex = *std::ranges::max_element( *indices );
			if ( highest_vertex <= 0 || highest_vertex > 0x1000000 ) return false;

			const auto vertex_count = static_cast<std::size_t>( highest_vertex ) + 1;
			const auto vertices = read_remote_array<float>( vert_ptr, vertex_count * 3 );
			if ( !vertices ) return false;

			std::vector<std::uint8_t> materials{};
			if ( material_ptr > 0x10000 && material_count > 0 )
			{
				if ( const auto remote = read_remote_array<std::uint8_t>(
					material_ptr + leaves->first_triangle, triangle_count ) )
				{
					materials = std::move( *remote );
				}
			}

			const auto vertex_at = [ & ]( std::int32_t index )
			{
				const auto offset = static_cast<std::size_t>( index ) * 3;
				return place_vertex( rotation, scale, position,
					{ ( *vertices )[ offset ], ( *vertices )[ offset + 1 ],
						( *vertices )[ offset + 2 ] } );
			};
			const auto initial_size = output.size( );
			for ( const auto span : leaves->spans )
			{
				for ( auto ordinal = 0u; ordinal < span.count; ++ordinal )
				{
					const auto local = span.first - leaves->first_triangle + ordinal;
					if ( local >= triangle_count ) continue;
					const auto base = static_cast<std::size_t>( local ) * 3;
					const std::array vertex_indices{
						( *indices )[ base ], ( *indices )[ base + 1 ], ( *indices )[ base + 2 ] };
					if ( std::ranges::any_of( vertex_indices, [ & ]( auto index )
						{ return index < 0 || static_cast<std::size_t>( index ) >= vertex_count; } ) )
					{
						continue;
					}

					auto material = fallback_surface;
					if ( local < materials.size( ) && materials[ local ] < surface_table.size( ) )
					{
						const auto global_index = materials[ local ];
						const auto& source = surface_table[ global_index ];
						material = { source.penetration_mod, source.surface_type, global_index };
					}
					output.push_back( { vertex_at( vertex_indices[ 0 ] ),
						vertex_at( vertex_indices[ 1 ] ), vertex_at( vertex_indices[ 2 ] ),
						material, solid_id } );
				}
			}
			return output.size( ) != initial_size;
		}

		static bool decode_convex_shape( std::uintptr_t hull_data, float uniform_scale,
			const collision_world::surface_info& surface,
			std::vector<collision_world::triangle>& out, std::uint64_t solid_id )
		{
			std::array<std::uint8_t, 0x100> header{};
			if ( !hull_data || !app::context().process.copy(
				hull_data, header.data( ), header.size( ) ) )
			{
				return false;
			}
			const auto vertex_count = blob_field<std::int32_t>( header, 0x88 );
			const auto vertex_address = blob_field<std::uintptr_t>( header, 0x90 );
			const auto edge_count = blob_field<std::int32_t>( header, 0xa0 );
			const auto edge_address = blob_field<std::uintptr_t>( header, 0xa8 );
			const auto face_count = blob_field<std::int32_t>( header, 0xb8 );
			const auto face_address = blob_field<std::uintptr_t>( header, 0xc0 );
			const auto plausible_count = []( auto count )
				{ return count > 0 && count <= 0xffff; };
			if ( !plausible_count( vertex_count ) || !plausible_count( edge_count )
				|| !plausible_count( face_count ) )
			{
				return false;
			}

			const auto vertices = read_remote_array<float>(
				vertex_address, static_cast<std::size_t>( vertex_count ) * 3 );
			const auto edges = read_remote_array<half_edge_record>( edge_address, edge_count );
			const auto faces = read_remote_array<std::uint8_t>( face_address, face_count );
			if ( !vertices || !edges || !faces ) return false;

			const auto face_loop = [ & ]( std::uint8_t first )
			{
				std::vector<std::uint8_t> loop{};
				loop.reserve( 8 );
				auto cursor = first;
				for ( auto steps = 0; steps < 64; ++steps )
				{
					if ( cursor >= edges->size( ) ) break;
					loop.push_back( ( *edges )[ cursor ].vert );
					cursor = ( *edges )[ cursor ].next;
					if ( cursor == first ) break;
				}
				return loop;
			};
			const auto vertex_at = [ & ]( std::uint8_t index )
			{
				const auto base = static_cast<std::size_t>( index ) * 3;
				return foundation::vec3{ ( *vertices )[ base ] * uniform_scale,
					( *vertices )[ base + 1 ] * uniform_scale,
					( *vertices )[ base + 2 ] * uniform_scale };
			};

			const auto initial_size = out.size( );
			for ( const auto first_edge : *faces )
			{
				const auto loop = face_loop( first_edge );
				if ( loop.size( ) < 3 || std::ranges::any_of( loop,
					[ & ]( auto index ) { return index >= vertex_count; } ) )
				{
					continue;
				}
				const auto anchor = vertex_at( loop.front( ) );
				for ( std::size_t index = 1; index + 1 < loop.size( ); ++index )
				{
					out.push_back( { anchor, vertex_at( loop[ index ] ),
						vertex_at( loop[ index + 1 ] ), surface, solid_id } );
				}
			}
			return out.size( ) != initial_size;
		}

		static void decode_convex_body( std::uintptr_t shape_body,
			std::vector<collision_world::triangle>& output )
		{
			const auto data = app::context().process.load<std::uintptr_t>( shape_body + 0xb8 );
			if ( data <= 0x10000 || data >= 0x7fffffffffff ) return;
			const auto raw_scale = app::context().process.load<float>( shape_body + 0xb0 );
			const auto scale = raw_scale > 0.0f && std::isfinite( raw_scale )
				? raw_scale : 1.0f;
			collision_world::surface_info material{};
			material.penetration = app::context().process.load<float>( shape_body + 0x28 );
			decode_convex_shape( data, scale, material, output, shape_body );
		}

		static void decode_mesh_body( std::uintptr_t shape_body,
			const std::vector<collision_world::global_surface_entry>& surface_table,
			std::vector<collision_world::triangle>& output )
		{
			const auto mesh_data = app::context().process.load<std::uintptr_t>( shape_body + 0xc0 );
			if ( !mesh_data || app::context().process.load<float>( shape_body + 0x2c ) < 0.0f )
				return;

			std::array<std::uint8_t, 0xa0> descriptor{};
			if ( !app::context().process.copy( mesh_data, descriptor.data( ), descriptor.size( ) ) )
				return;
			std::array<float, 3> scale{};
			std::array<float, 3> position{};
			quaternion orientation{};
			if ( !app::context().process.copy( shape_body + 0xb0, scale.data( ), sizeof( scale ) )
				|| !app::context().process.copy( shape_body + 0x100, position.data( ), sizeof( position ) )
				|| !app::context().process.copy( shape_body + 0x130, &orientation, sizeof( orientation ) ) )
			{
				return;
			}
			if ( std::ranges::any_of( scale, []( float value ) { return !std::isfinite( value ); } )
				|| std::ranges::all_of( scale, []( float value ) { return value == 0.0f; } ) )
			{
				return;
			}
			const auto quaternion_length = orientation.x * orientation.x
				+ orientation.y * orientation.y + orientation.z * orientation.z
				+ orientation.w * orientation.w;
			if ( quaternion_length < 0.5f || quaternion_length > 1.5f )
				orientation = { 0.0f, 0.0f, 0.0f, 1.0f };

			const std::array count_candidates{
				blob_field<std::int32_t>( descriptor, 0x28 ),
				blob_field<std::int32_t>( descriptor, 0x30 ),
				blob_field<std::int32_t>( descriptor, 0x48 ),
				blob_field<std::int32_t>( descriptor, 0x58 ) };
			const auto count = std::ranges::find_if( count_candidates,
				[]( auto value ) { return value > 0 && value < 0x1000000; } );
			if ( count == count_candidates.end( ) ) return;

			collision_world::surface_info fallback{};
			fallback.penetration = app::context().process.load<float>( shape_body + 0x28 );
			decode_mesh_shape(
				blob_field<std::uintptr_t>( descriptor, 0x20 ),
				blob_field<std::uintptr_t>( descriptor, 0x38 ),
				blob_field<std::uintptr_t>( descriptor, 0x50 ),
				static_cast<std::uint32_t>( *count ), make_rotation_basis( orientation ),
				scale.data( ), position.data( ),
				blob_field<std::uintptr_t>( descriptor, 0x98 ),
				blob_field<std::int32_t>( descriptor, 0x90 ), surface_table,
				fallback, output, shape_body );
		}

		static void append_shape_triangles( std::uintptr_t shape_body,
			std::uintptr_t hull_vtable, std::uintptr_t mesh_vtable,
			const std::vector<collision_world::global_surface_entry>& surface_table,
			std::vector<collision_world::triangle>& output )
		{
			const auto type = app::context().process.load<std::uintptr_t>( shape_body );
			if ( type == hull_vtable )
				decode_convex_body( shape_body, output );
			else if ( type == mesh_vtable )
				decode_mesh_body( shape_body, surface_table, output );
		}

	}

	std::uintptr_t collision_world::surface_manager( ) const
	{

		if ( this->m_surface_manager || this->m_surface_manager_attempts >= k_surface_manager_attempts )
		{
			return this->m_surface_manager;
		}

		++this->m_surface_manager_attempts;

		const auto plausible = [ ]( std::uintptr_t mgr )
			{
				if ( mgr < 0x10000 || mgr > 0x7fffffffffffull )
				{
					return false;
				}

				const auto count = app::context().process.load<std::int32_t>( mgr + 0x20 );
				if ( count <= 0 || count > 4096 )
				{
					return false;
				}

				const auto array_base = app::context().process.load<std::uintptr_t>( mgr + 0x28 );
				if ( array_base < 0x10000 || array_base > 0x7fffffffffffull )
				{
					return false;
				}

				global_surface_entry first{};
				if ( !app::context().process.copy( array_base, &first, sizeof( first ) ) )
				{
					return false;
				}

				return std::isfinite( first.penetration_mod ) && first.penetration_mod > 0.0f && first.penetration_mod <= 16.0f;
			};

		if ( const auto load_site = app::context().process.scan_signature( app::context().modules.client, "48 8B 1D ? ? ? ? 33 FF 48 85 DB 74 ? 8B 43 34 89 7B 20 A9 00 00 00 C0" ) )
		{
			const auto mgr = app::context().process.load<std::uintptr_t>( app::context().process.decode_rip( load_site ) );
			if ( plausible( mgr ) )
			{
				this->m_surface_manager = mgr;
				app::context().diagnostics.info( "[bvh] surface manager {:#x} via client.dll load site {:#x}", mgr, load_site );
				return mgr;
			}
		}

		for ( const auto module : { app::context().modules.client, app::context().process.module_base( "server.dll" ) } )
		{
			if ( !module )
			{
				continue;
			}

			const auto getter = app::context().process.scan_signature( module, "48 63 41 ? 48 8B 0D ? ? ? ? 48 C1 E0 05 48 03 41 28" );
			if ( !getter )
			{
				continue;
			}

			const auto mgr = app::context().process.load<std::uintptr_t>( app::context().process.decode_rip( getter + 4 ) );
			if ( plausible( mgr ) )
			{
				this->m_surface_manager = mgr;
				app::context().diagnostics.info( "[bvh] surface manager {:#x} via GetSurfaceDataFromHandle {:#x}", mgr, getter );
				return mgr;
			}
		}

		app::context().diagnostics.warning( "[bvh] surface manager not found -- geometry will trace with neutral penetration" );
		return 0;
	}

	std::vector<collision_world::global_surface_entry> collision_world::read_surface_table( ) const
	{

		std::vector<global_surface_entry> table{};

		const auto manager = this->surface_manager( );
		if ( !manager )
		{
			return table;
		}

		const auto array_base = app::context().process.load<std::uintptr_t>( manager + 0x28 );
		const auto surface_count = app::context().process.load<std::int32_t>( manager + 0x20 );

		if ( !array_base || surface_count <= 0 || surface_count > 4096 )
		{
			app::context().diagnostics.warning( "[bvh] surface table unreadable (manager={:#x}, array={:#x}, count={})", manager, array_base, surface_count );
			return table;
		}

		table.resize( surface_count );
		if ( !app::context().process.copy( array_base, table.data( ), static_cast< std::size_t >( surface_count ) * sizeof( global_surface_entry ) ) )
		{
			table.clear( );
		}

		return table;
	}

	void collision_world::parse( )
	{

		const auto anchor = app::context().process.scan_signature( app::context().modules.client, "E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CF" );
		if ( !anchor )
		{
			app::context().diagnostics.warning( "[bvh] world anchor signature not found" );
			return;
		}

		std::uintptr_t vphys2_world_global{ 0 };
		{
			constexpr auto k_window{ 0x40 };
			std::uint8_t window[ k_window ]{};
			if ( !app::context().process.copy(
				anchor - k_window, window, sizeof( window ) ) )
			{
				app::context().diagnostics.warning( "[collision] world anchor window unreadable" );
				return;
			}

			for ( int i = k_window - 7; i >= 0; --i )
			{
				if ( window[ i ] == 0x48 && window[ i + 1 ] == 0x8B && window[ i + 2 ] == 0x0D )
				{
					vphys2_world_global = app::context().process.decode_rip( anchor - k_window + i );
					break;
				}
			}
		}

		if ( !vphys2_world_global )
		{
			app::context().diagnostics.warning( "[bvh] world-global mov not found near anchor {:#x}", anchor );
			return;
		}

		const auto world_holder = app::context().process.load<std::uintptr_t>( vphys2_world_global );
		if ( !world_holder )
		{
			app::context().diagnostics.warning( "[bvh] world holder empty (global at {:#x})", vphys2_world_global );
			return;
		}

		const auto vphys2_world = app::context().process.load<std::uintptr_t>( world_holder );
		if ( !vphys2_world )
		{
			app::context().diagnostics.warning( "[bvh] world pointer empty (holder at {:#x})", world_holder );
			return;
		}

		const auto global_table = read_surface_table( );

		const auto inner_world = app::context().process.load<std::uintptr_t>( vphys2_world + 0x30 );
		if ( !inner_world )
		{
			app::context().diagnostics.warning( "[bvh] inner world null (world={:#x})", vphys2_world );
			return;
		}

		const auto body_array = app::context().process.load<std::uintptr_t>( inner_world + 0x110 );
		if ( !body_array )
		{
			app::context().diagnostics.warning( "[bvh] body array null (inner={:#x})", inner_world );
			return;
		}

		const auto body_count = app::context().process.load<std::int32_t>( body_array + 0x268 );
		if ( body_count <= 0 || body_count > 1'000'000 )
		{
			app::context().diagnostics.warning( "[bvh] implausible body count {} (body_array={:#x})", body_count, body_array );
			return;
		}

		const auto hull_vtable = app::context().process.locate_vtable( app::context().modules.physics, "CRnHullShape" );
		const auto mesh_vtable = app::context().process.locate_vtable( app::context().modules.physics, "CRnMeshShape" );

		if ( !hull_vtable || !mesh_vtable )
		{
			app::context().diagnostics.warning( "[bvh] shape vtables not found (hull={:#x}, mesh={:#x})", hull_vtable, mesh_vtable );
			return;
		}

		std::vector<triangle> fresh;
		fresh.reserve( 262144 );

		std::int32_t bodies_with_nodes{ 0 };
		std::int32_t bodies_static{ 0 };
		std::unordered_map<std::uint32_t, std::int32_t> flag_histogram{};

		for ( std::int32_t body_idx = 0; body_idx < body_count; ++body_idx )
		{
			const auto body = body_array + static_cast< std::uintptr_t >( body_idx ) * 88;
			const auto bvh_root = app::context().process.load<std::int32_t>( body );
			const auto bvh_nodes_ptr = app::context().process.load<std::uintptr_t>( body + 0x18 );

			if ( !bvh_nodes_ptr )
			{
				continue;
			}

			++bodies_with_nodes;

			const auto simulation_type = app::context().process.load<std::uint32_t>( body + 0x40 );
			++flag_histogram[ simulation_type ];

			++bodies_static;

			if ( bvh_root >= 0 )
			{
				const auto count_a = static_cast< std::uint32_t >( bvh_root + 1 );
				const auto count_b = static_cast< std::uint32_t >( app::context().process.load<std::int32_t>( body + 0x08 ) );
				const auto count_c = static_cast< std::uint32_t >( app::context().process.load<std::int32_t>( body + 0x10 ) );
				const auto outer_node_count = std::max( { count_a, count_b, count_c } );

				if ( outer_node_count > 0x100000 )
				{
					continue;
				}

				std::vector<std::uint8_t> outer_buf( outer_node_count * detail::k_outer_node_size );
				if ( !app::context().process.copy(
					bvh_nodes_ptr, outer_buf.data( ), outer_buf.size( ) ) )
				{
					continue;
				}

				std::vector<std::uintptr_t> leaves;
				leaves.reserve( 256 );

				std::vector<std::int32_t> outer_stack;
				outer_stack.reserve( 128 );
				outer_stack.push_back( bvh_root );

				while ( !outer_stack.empty( ) )
				{
					const auto idx = outer_stack.back( );
					outer_stack.pop_back( );

					if ( idx < 0 || static_cast< std::uint32_t >( idx ) >= outer_node_count )
					{
						continue;
					}

					const auto node = outer_buf.data( ) + static_cast< std::uintptr_t >( idx ) * detail::k_outer_node_size;
					std::int32_t left{};
					std::memcpy( &left, node + 12, sizeof( left ) );

					if ( left == -1 )
					{
						std::uintptr_t shape_ptr{};
						std::memcpy( &shape_ptr, node + 0x28, sizeof( shape_ptr ) );
						if ( shape_ptr )
						{
							leaves.push_back( shape_ptr );
						}
					}
					else
					{
						std::int32_t right{};
						std::memcpy( &right, node + 28, sizeof( right ) );
						if ( left >= 0 )
						{
							outer_stack.push_back( left );
						}

						if ( right >= 0 )
						{
							outer_stack.push_back( right );
						}
					}
				}

				std::unordered_set<std::uintptr_t> seen;

				for ( const auto shape : leaves )
				{
					if ( seen.count( shape ) )
					{
						continue;
					}

					seen.insert( shape );
					detail::append_shape_triangles( shape, hull_vtable, mesh_vtable, global_table, fresh );
				}
			}
			else
			{
				const auto shape = app::context().process.load<std::uintptr_t>( body + 0x28 );
				if ( shape )
				{
					detail::append_shape_triangles( shape, hull_vtable, mesh_vtable, global_table, fresh );
				}
			}
		}

		app::context().diagnostics.info( "[bvh] bodies={} with_nodes={} static={} triangles={}", body_count, bodies_with_nodes, bodies_static, fresh.size( ) );

		if ( fresh.empty( ) && bodies_with_nodes > 0 )
		{
			std::string hist{};
			for ( const auto& [flag, count] : flag_histogram )
			{
				hist += std::format( "{:#x}:{} ", flag, count );
			}
			app::context().diagnostics.warning( "[bvh] no triangles extracted; body flag histogram: {}", hist );
		}

		{
			std::unique_lock lock( this->m_mutex );
			this->m_triangles = std::move( fresh );
			this->m_world_triangle_count = this->m_triangles.size( );
			this->m_entity_triangle_count = 0;
			this->m_entity_collision.reset( );
			this->m_entity_state_hash = 0;
			this->rebuild_accel( );
			++this->m_world_render_revision;
			++this->m_entity_render_revision;
			this->m_geometry_revision.fetch_add( 1, std::memory_order_release );
		}
	}

	void collision_world::clear( )
	{
		std::unique_lock lock( this->m_mutex );
		this->m_triangles.clear( );
		this->m_world_triangle_count = 0;
		this->m_map_name.clear( );
		this->m_entity_triangle_count = 0;
		this->m_nodes.clear( );
		this->m_indices.clear( );
		this->m_tri_bounds.clear( );
		this->m_centroids.clear( );
		this->m_world_render.reset( );
		this->m_entity_render.reset( );
		this->m_entity_collision.reset( );
		this->m_entity_state_hash = 0;
		++this->m_world_render_revision;
		++this->m_entity_render_revision;
		this->m_geometry_revision.fetch_add( 1, std::memory_order_release );
	}

	std::optional<collision_world::ray_query> collision_world::make_ray_query(
		const foundation::vec3& start, const foundation::vec3& end )
	{
		const auto displacement = end - start;
		const auto length = displacement.length( );
		if ( length < 1e-8f )
			return std::nullopt;

		ray_query query{};
		query.origin = start;
		query.direction = displacement / length;
		query.length = length;
		query.origin_components[ 0 ] = start.x;
		query.origin_components[ 1 ] = start.y;
		query.origin_components[ 2 ] = start.z;

		const float direction[ 3 ]{ query.direction.x, query.direction.y, query.direction.z };
		for ( int axis = 0; axis < 3; ++axis )
		{
			query.inverse_direction[ axis ] = std::abs( direction[ axis ] ) > 1e-8f
				? 1.0f / direction[ axis ]
				: std::copysign( 1e12f, direction[ axis ] );
		}
		return query;
	}

	std::optional<collision_world::ray_contact> collision_world::intersect_triangle(
		const ray_query& ray, const triangle& candidate, std::int32_t triangle_index,
		float distance_limit )
	{
		constexpr float parallel_epsilon = 1e-8f;
		constexpr float contact_epsilon = 1e-5f;
		const auto edge_a = candidate.v1 - candidate.v0;
		const auto edge_b = candidate.v2 - candidate.v0;
		const auto determinant_vector = ray.direction.cross( edge_b );
		const auto determinant = edge_a.dot( determinant_vector );
		if ( std::abs( determinant ) < parallel_epsilon )
			return std::nullopt;

		const auto inverse_determinant = 1.0f / determinant;
		const auto from_vertex = ray.origin - candidate.v0;
		const auto barycentric_u = from_vertex.dot( determinant_vector ) * inverse_determinant;
		if ( barycentric_u < 0.0f || barycentric_u > 1.0f )
			return std::nullopt;

		const auto barycentric_vector = from_vertex.cross( edge_a );
		const auto barycentric_v = ray.direction.dot( barycentric_vector ) * inverse_determinant;
		if ( barycentric_v < 0.0f || barycentric_u + barycentric_v > 1.0f )
			return std::nullopt;

		const auto distance = edge_b.dot( barycentric_vector ) * inverse_determinant;
		if ( distance <= contact_epsilon || distance >= distance_limit )
			return std::nullopt;

		auto normal = edge_a.cross( edge_b );
		const auto normal_length = normal.length( );
		if ( normal_length > parallel_epsilon )
			normal = normal / normal_length;
		else
			normal = {};

		return ray_contact{
			.distance = distance,
			.position = ray.origin + ray.direction * distance,
			.normal = normal,
			.surface = candidate.surface,
			.triangle_index = triangle_index,
			.entering = normal.dot( ray.direction ) < 0.0f,
			.solid_id = candidate.solid_id
		};
	}

	void collision_world::traverse_ray( const ray_query& ray, std::int32_t excluded_triangle,
		bool nearest_only, std::vector<ray_contact>& contacts ) const
	{
		if ( m_nodes.empty( ) )
			return;

		static thread_local std::vector<std::int32_t> pending{};
		pending.clear( );
		if ( pending.capacity( ) < 64 ) pending.reserve( 64 );
		pending.push_back( 0 );
		auto distance_limit = ray.length;

		while ( !pending.empty( ) )
		{
			const auto node_index = pending.back( );
			pending.pop_back( );
			if ( node_index < 0 || static_cast<std::size_t>( node_index ) >= m_nodes.size( ) )
				continue;

			const auto& node = m_nodes[ node_index ];
			if ( !node.bounds.intersects_ray( ray.origin_components, ray.inverse_direction, distance_limit ) )
				continue;

			if ( node.left >= 0 )
			{
				if ( node.right >= 0 )
					pending.push_back( node.right );
				pending.push_back( node.left );
				continue;
			}

			const auto first = std::max( node.tri_start, 0 );
			const auto last = std::min<std::int32_t>(
				node.tri_start + node.tri_count, static_cast<std::int32_t>( m_indices.size( ) ) );
			for ( auto slot = first; slot < last; ++slot )
			{
				const auto triangle_index = m_indices[ slot ];
				if ( triangle_index == excluded_triangle || triangle_index < 0
					|| static_cast<std::size_t>( triangle_index ) >= m_triangles.size( ) )
				{
					continue;
				}

				const auto contact = intersect_triangle(
					ray, m_triangles[ triangle_index ], triangle_index, distance_limit );
				if ( !contact )
					continue;

				if ( nearest_only )
				{
					contacts.assign( 1, *contact );
					distance_limit = contact->distance;
				}
				else
				{
					contacts.push_back( *contact );
				}
			}
		}
	}

	collision_world::trace_result collision_world::trace_ray( const foundation::vec3& start,
		const foundation::vec3& end, std::int32_t exclude_tri ) const
	{
		trace_result result{};
		result.end_pos = end;
		const auto ray = make_ray_query( start, end );
		if ( !ray )
			return result;

		std::shared_ptr<const collision_world> entity_collision{};
		std::shared_lock lock( m_mutex );
		static thread_local std::vector<ray_contact> contacts{};
		contacts.clear( );
		if ( contacts.capacity( ) < 1 ) contacts.reserve( 1 );
		traverse_ray( *ray, exclude_tri, true, contacts );
		entity_collision = m_entity_collision;
		if ( !contacts.empty( ) )
		{
			const auto& contact = contacts.front( );
			result.hit = true;
			result.fraction = contact.distance / ray->length;
			result.distance = contact.distance;
			result.end_pos = contact.position;
			result.normal = contact.normal;
			result.surface = contact.surface;
			result.triangle_index = contact.triangle_index;
		}
		lock.unlock( );

		if ( entity_collision )
		{
			const auto entity_exclusion = exclude_tri < -1 ? -exclude_tri - 2 : -1;
			auto dynamic = entity_collision->trace_ray( start, end, entity_exclusion );
			if ( dynamic.hit && ( !result.hit || dynamic.distance < result.distance ) )
			{
				dynamic.triangle_index = -dynamic.triangle_index - 2;
				return dynamic;
			}
		}
		return result;
	}

	std::vector<collision_world::hit_entry> collision_world::trace_ray_all(
		const foundation::vec3& start, const foundation::vec3& end ) const
	{
		const auto ray = make_ray_query( start, end );
		if ( !ray )
			return {};

		std::shared_ptr<const collision_world> entity_collision{};
		std::shared_lock lock( m_mutex );
		static thread_local std::vector<ray_contact> contacts{};
		contacts.clear( );
		traverse_ray( *ray, -1, false, contacts );
		entity_collision = m_entity_collision;

		std::vector<hit_entry> hits;
		hits.reserve( contacts.size( ) );
		for ( const auto& contact : contacts )
		{
			hits.push_back( hit_entry{
				.distance = contact.distance,
				.fraction = contact.distance / ray->length,
				.position = contact.position,
				.normal = contact.normal,
				.surface = contact.surface,
				.triangle_index = contact.triangle_index,
				.is_enter = contact.entering,
				.solid_id = contact.solid_id
			} );
		}
		lock.unlock( );
		if ( entity_collision )
		{
			auto dynamic = entity_collision->trace_ray_all( start, end );
			for ( auto& hit : dynamic )
			{
				hit.triangle_index = -hit.triangle_index - 2;
				hits.push_back( hit );
			}
		}
		std::ranges::sort( hits, {}, &hit_entry::distance );
		return hits;
	}

	collision_world::segment_build_result collision_world::build_segments(
		std::vector<hit_entry> hits, float ray_length ) const
	{
		return collision_detail::build_segments( std::move( hits ), ray_length );
	}

	std::vector<collision_world::triangle> collision_world::triangles( ) const
	{
		std::shared_lock lock( this->m_mutex );
		auto result = this->m_triangles;
		const auto entities = this->m_entity_collision;
		lock.unlock( );
		if ( entities )
		{
			auto dynamic = entities->triangles( );
			result.insert( result.end( ), std::make_move_iterator( dynamic.begin( ) ),
				std::make_move_iterator( dynamic.end( ) ) );
		}
		return result;
	}

	std::vector<foundation::vec3> collision_world::get_render_vertices( ) const
	{
		const auto all = triangles( );
		std::vector<foundation::vec3> verts;
		verts.reserve( all.size( ) * 3 );
		for ( const auto& t : all )
		{
			verts.push_back( t.v0 );
			verts.push_back( t.v1 );
			verts.push_back( t.v2 );
		}
		return verts;
	}

	collision_world::render_snapshot collision_world::render_geometry( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return {
			.world = this->m_world_render,
			.entities = this->m_entity_render,
			.world_revision = this->m_world_render_revision,
			.entity_revision = this->m_entity_render_revision };
	}

	std::uint64_t collision_world::geometry_revision( ) const
	{
		return this->m_geometry_revision.load( std::memory_order_acquire );
	}

	std::size_t collision_world::count( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return this->m_triangles.size( ) + this->m_entity_triangle_count;
	}

	bool collision_world::valid( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return !this->m_triangles.empty( );
	}

	void collision_world::aabb::expand( const foundation::vec3& point )
	{
		mins[ 0 ] = std::min( mins[ 0 ], point.x );
		mins[ 1 ] = std::min( mins[ 1 ], point.y );
		mins[ 2 ] = std::min( mins[ 2 ], point.z );
		maxs[ 0 ] = std::max( maxs[ 0 ], point.x );
		maxs[ 1 ] = std::max( maxs[ 1 ], point.y );
		maxs[ 2 ] = std::max( maxs[ 2 ], point.z );
	}

	void collision_world::aabb::expand( const aabb& other )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			mins[ axis ] = std::min( mins[ axis ], other.mins[ axis ] );
			maxs[ axis ] = std::max( maxs[ axis ], other.maxs[ axis ] );
		}
	}

	int collision_world::aabb::longest_axis( ) const
	{
		const std::array extents{
			maxs[ 0 ] - mins[ 0 ],
			maxs[ 1 ] - mins[ 1 ],
			maxs[ 2 ] - mins[ 2 ]
		};
		return static_cast<int>( std::distance(
			extents.begin( ), std::ranges::max_element( extents ) ) );
	}

	bool collision_world::aabb::intersects_ray( const float origin[ 3 ],
		const float inverse_direction[ 3 ], float max_distance ) const
	{
		float entrance{};
		auto exit = max_distance;
		for ( int axis = 0; axis < 3; ++axis )
		{
			auto near_distance = ( mins[ axis ] - origin[ axis ] ) * inverse_direction[ axis ];
			auto far_distance = ( maxs[ axis ] - origin[ axis ] ) * inverse_direction[ axis ];
			if ( near_distance > far_distance )
				std::swap( near_distance, far_distance );

			entrance = std::max( entrance, near_distance );
			exit = std::min( exit, far_distance );
			if ( entrance > exit )
				return false;
		}
		return true;
	}

	void collision_world::rebuild_accel( const bool rebuild_world_render )
	{
		m_nodes.clear( );
		m_indices.clear( );
		m_tri_bounds.clear( );
		m_centroids.clear( );

		const auto triangle_count = static_cast<std::int32_t>( m_triangles.size( ) );
		if ( triangle_count == 0 )
		{
			m_world_render.reset( );
			m_entity_render.reset( );
			return;
		}

		m_indices.resize( triangle_count );
		std::iota( m_indices.begin( ), m_indices.end( ), 0 );
		m_tri_bounds.resize( triangle_count );
		m_centroids.resize( static_cast<std::size_t>( triangle_count ) * 3 );

		for ( std::int32_t index = 0; index < triangle_count; ++index )
		{
			auto& bounds = m_tri_bounds[ index ];
			bounds.expand( m_triangles[ index ].v0 );
			bounds.expand( m_triangles[ index ].v1 );
			bounds.expand( m_triangles[ index ].v2 );
			const auto base = static_cast<std::size_t>( index ) * 3;
			for ( int axis = 0; axis < 3; ++axis )
				m_centroids[ base + axis ] = ( bounds.mins[ axis ] + bounds.maxs[ axis ] ) * 0.5f;
		}

		m_nodes.reserve( static_cast<std::size_t>( triangle_count ) * 2 );
		build_recursive( 0, triangle_count, 0 );

		m_tri_bounds.clear( );
		m_tri_bounds.shrink_to_fit( );
		m_centroids.clear( );
		m_centroids.shrink_to_fit( );

		rebuild_render_geometry( rebuild_world_render, true );
	}

	void collision_world::rebuild_render_geometry(
		const bool rebuild_world, const bool rebuild_entities )
	{
		using mesh = render_snapshot::mesh;
		struct vertex_key
		{
			std::uint32_t x{}, y{}, z{};
			[[nodiscard]] bool operator==( const vertex_key& other ) const noexcept = default;
		};
		struct vertex_hash
		{
			[[nodiscard]] std::size_t operator()( const vertex_key& value ) const noexcept
			{
				auto hash = static_cast<std::size_t>( value.x ) * 0x9E3779B185EBCA87ull;
				hash ^= static_cast<std::size_t>( value.y ) + 0x9E3779B9u
					+ ( hash << 6 ) + ( hash >> 2 );
				hash ^= static_cast<std::size_t>( value.z ) + 0x85EBCA6Bu
					+ ( hash << 6 ) + ( hash >> 2 );
				return hash;
			}
		};

		const auto world_end = m_world_triangle_count > 0
			&& m_world_triangle_count <= m_triangles.size( )
			? m_world_triangle_count : m_triangles.size( );
		const auto ordered = m_indices.size( ) == m_triangles.size( );

		const auto build_mesh = [ & ]( const std::size_t first_triangle,
			const std::size_t end_triangle ) -> std::shared_ptr<const mesh>
		{
			if ( first_triangle >= end_triangle )
				return {};

			auto result = std::make_shared<mesh>( );
			result->indices.reserve( ( end_triangle - first_triangle ) * 3 );
			result->vertices.reserve( std::min<std::size_t>(
				( end_triangle - first_triangle ) * 3, 1'000'000 ) );
			std::unordered_map<vertex_key, std::uint32_t, vertex_hash> vertex_map{};
			vertex_map.reserve( result->vertices.capacity( ) );

			constexpr std::size_t k_batch_triangles{ 256 };
			mesh::batch batch{};
			batch.mins = { 1e12f, 1e12f, 1e12f };
			batch.maxs = { -1e12f, -1e12f, -1e12f };
			batch.first_index = 0;
			std::size_t batch_triangles{};

			const auto flush_batch = [ & ]
			{
				if ( batch.index_count == 0 ) return;
				result->batches.push_back( batch );
				batch = {};
				batch.mins = { 1e12f, 1e12f, 1e12f };
				batch.maxs = { -1e12f, -1e12f, -1e12f };
				batch.first_index = static_cast<std::uint32_t>( result->indices.size( ) );
				batch_triangles = 0;
			};

			const auto append_vertex = [ & ]( const foundation::vec3& vertex )
			{
				const vertex_key key{
					std::bit_cast<std::uint32_t>( vertex.x ),
					std::bit_cast<std::uint32_t>( vertex.y ),
					std::bit_cast<std::uint32_t>( vertex.z ) };
				const auto [ it, inserted ] = vertex_map.try_emplace(
					key, static_cast<std::uint32_t>( result->vertices.size( ) ) );
				if ( inserted ) result->vertices.push_back( vertex );
				result->indices.push_back( it->second );
				++batch.index_count;
				batch.mins.x = std::min( batch.mins.x, vertex.x );
				batch.mins.y = std::min( batch.mins.y, vertex.y );
				batch.mins.z = std::min( batch.mins.z, vertex.z );
				batch.maxs.x = std::max( batch.maxs.x, vertex.x );
				batch.maxs.y = std::max( batch.maxs.y, vertex.y );
				batch.maxs.z = std::max( batch.maxs.z, vertex.z );
			};

			for ( std::size_t slot = 0; slot < m_triangles.size( ); ++slot )
			{
				const auto triangle_index = ordered
					? static_cast<std::size_t>( m_indices[ slot ] ) : slot;
				if ( triangle_index < first_triangle || triangle_index >= end_triangle )
					continue;
				const auto& triangle = m_triangles[ triangle_index ];
				append_vertex( triangle.v0 );
				append_vertex( triangle.v1 );
				append_vertex( triangle.v2 );
				if ( ++batch_triangles >= k_batch_triangles ) flush_batch( );
			}
			flush_batch( );
			return result;
		};

		if ( rebuild_world ) m_world_render = build_mesh( 0, world_end );
		if ( rebuild_entities ) m_entity_render = build_mesh( world_end, m_triangles.size( ) );
	}

	std::int32_t collision_world::build_recursive(
		std::int32_t start, std::int32_t end, std::int32_t depth )
	{
		const auto node_index = static_cast<std::int32_t>( m_nodes.size( ) );
		m_nodes.emplace_back( );
		for ( auto slot = start; slot < end; ++slot )
			m_nodes[ node_index ].bounds.expand( m_tri_bounds[ m_indices[ slot ] ] );

		const auto count = end - start;
		if ( count <= k_max_leaf_tris || depth >= k_max_depth )
		{
			m_nodes[ node_index ].tri_start = start;
			m_nodes[ node_index ].tri_count = count;
			return node_index;
		}

		aabb centroid_bounds{};
		for ( auto slot = start; slot < end; ++slot )
		{
			const auto base = static_cast<std::size_t>( m_indices[ slot ] ) * 3;
			centroid_bounds.expand( foundation::vec3{
				m_centroids[ base ], m_centroids[ base + 1 ], m_centroids[ base + 2 ] } );
		}

		const auto axis = centroid_bounds.longest_axis( );
		const auto split = start + count / 2;
		std::nth_element( m_indices.begin( ) + start, m_indices.begin( ) + split,
			m_indices.begin( ) + end, [ & ]( std::int32_t lhs, std::int32_t rhs )
			{
				return m_centroids[ static_cast<std::size_t>( lhs ) * 3 + axis ]
					< m_centroids[ static_cast<std::size_t>( rhs ) * 3 + axis ];
			} );

		m_nodes[ node_index ].left = build_recursive( start, split, depth + 1 );
		m_nodes[ node_index ].right = build_recursive( split, end, depth + 1 );
		return node_index;
	}

}
