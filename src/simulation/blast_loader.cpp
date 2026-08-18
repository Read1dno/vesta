#include <stdafx.hpp>

namespace game {

	namespace detail_bd_runtime {

		constexpr std::size_t k_min_points{ 4096 };
		constexpr std::size_t k_max_points{ 4'000'000 };
		constexpr std::size_t k_max_sites{ 8 };
		constexpr std::size_t k_max_slots{ 8'000'000 };
		constexpr std::size_t k_page_size{ 0x1000 };

		struct runtime_entry
		{
			std::uint32_t metadata{};
			std::uint32_t unknown0{};
			float position[ 3 ]{};
			std::uint32_t unknown1{};
			std::uint32_t damage_count{};
			std::uint32_t unknown2{};
			std::uintptr_t damage{};
			std::uint64_t damage_capacity{};
		};

		static_assert( sizeof( runtime_entry ) == 0x30 );

		struct damage_ref
		{
			std::uintptr_t address{};
			std::uint32_t point_index{};
		};

		static bool valid_site( const blast_model::site_info& site )
		{
			const float values[]{
				site.mins.x, site.mins.y, site.mins.z,
				site.maxs.x, site.maxs.y, site.maxs.z, site.scale };

			if ( !std::ranges::all_of( values, [ ]( float value ) { return std::isfinite( value ); } ) )
			{
				return false;
			}

			const foundation::vec3 extent{
				site.maxs.x - site.mins.x,
				site.maxs.y - site.mins.y,
				site.maxs.z - site.mins.z };

			return extent.x > 8.0f && extent.x < 8192.0f
				&& extent.y > 8.0f && extent.y < 8192.0f
				&& extent.z > 8.0f && extent.z < 8192.0f
				&& site.scale > 64.0f && site.scale < 100000.0f;
		}

	}

	void blast_model::parse( )
	{
		using namespace detail_bd_runtime;

		const auto started = std::chrono::steady_clock::now( );
		const auto signature = app::context().process.scan_signature( app::context().modules.client,
			"48 8B D0 48 8D 0D ? ? ? ? E8 ? ? ? ? 33 D2 48 8D 4C 24 ?" );

		if ( !signature )
		{
			app::context().diagnostics.warning( "[bomb_dmg] runtime object signature not found" );
			return;
		}

		const auto object = app::context().process.decode_rip( signature + 3 );
		const auto slot_count = app::context().process.load<std::uint32_t>( object + 0x0c ) & 0x7fffffffu;
		const auto entries_address = app::context().process.load<std::uintptr_t>( object + 0x10 );
		const auto point_count = app::context().process.load<std::uint32_t>( object + 0x28 );
		const auto site_count = app::context().process.load<std::uint32_t>( object + 0x98 );
		const auto sites_address = app::context().process.load<std::uintptr_t>( object + 0xa0 );

		if ( point_count < k_min_points || point_count > k_max_points
			|| slot_count < point_count || slot_count > k_max_slots
			|| site_count == 0 || site_count > k_max_sites
			|| !entries_address || !sites_address )
		{
			app::context().diagnostics.warning( "[bomb_dmg] runtime object is not ready or its layout changed "
				"(object={:#x}, points={}, slots={}, sites={})", object, point_count, slot_count, site_count );
			return;
		}

		std::vector<site_info> sites( site_count );
		if ( !app::context().process.copy( sites_address, sites.data( ), sites.size( ) * sizeof( site_info ) )
			|| !std::ranges::all_of( sites, valid_site ) )
		{
			app::context().diagnostics.warning( "[bomb_dmg] invalid bombsite array at {:#x}", sites_address );
			return;
		}

		std::vector<runtime_entry> entries( slot_count );
		if ( !app::context().process.copy( entries_address, entries.data( ), entries.size( ) * sizeof( runtime_entry ) ) )
		{
			app::context().diagnostics.warning( "[bomb_dmg] failed to read {} spatial-hash slots at {:#x}", slot_count, entries_address );
			return;
		}

		std::vector<point> points;
		std::vector<damage_ref> damage_refs;
		points.reserve( point_count );
		damage_refs.reserve( point_count );

		for ( const auto& entry : entries )
		{
			if ( ( entry.metadata & 0x80000000u ) != 0 )
			{
				continue;
			}

			if ( entry.damage_count != site_count || !entry.damage )
			{
				app::context().diagnostics.warning( "[bomb_dmg] invalid active hash slot (damage={:#x}, count={})",
					entry.damage, entry.damage_count );
				return;
			}

			const auto x = std::lround( entry.position[ 0 ] );
			const auto y = std::lround( entry.position[ 1 ] );
			const auto z = std::lround( entry.position[ 2 ] );
			constexpr auto min_coord = std::numeric_limits<std::int16_t>::min( );
			constexpr auto max_coord = std::numeric_limits<std::int16_t>::max( );

			if ( x < min_coord || x > max_coord || y < min_coord || y > max_coord || z < min_coord || z > max_coord )
			{
				app::context().diagnostics.warning( "[bomb_dmg] invalid grid position ({}, {}, {})", x, y, z );
				return;
			}

			points.push_back( {
				static_cast<std::int16_t>( x ),
				static_cast<std::int16_t>( y ),
				static_cast<std::int16_t>( z ) } );
			damage_refs.push_back( { entry.damage, static_cast<std::uint32_t>( points.size( ) - 1 ) } );
		}

		if ( points.size( ) != point_count )
		{
			app::context().diagnostics.warning( "[bomb_dmg] active-slot count mismatch (header={}, table={})", point_count, points.size( ) );
			return;
		}

		std::ranges::sort( damage_refs, { }, &damage_ref::address );
		std::vector<damage_value> damage_values( static_cast<std::size_t>( site_count ) * point_count );
		std::array<std::uint8_t, k_page_size> page{};
		std::vector<std::uint32_t> raw( site_count );
		std::size_t page_reads{};

		for ( std::size_t first = 0; first < damage_refs.size( ); )
		{
			const auto page_address = damage_refs[ first ].address & ~( static_cast<std::uintptr_t>( k_page_size ) - 1 );
			auto last = first + 1;
			while ( last < damage_refs.size( )
				&& ( damage_refs[ last ].address & ~( static_cast<std::uintptr_t>( k_page_size ) - 1 ) ) == page_address )
			{
				++last;
			}

			const auto page_ok = app::context().process.copy( page_address, page.data( ), page.size( ) );
			++page_reads;

			for ( auto i = first; i < last; ++i )
			{
				const auto& ref = damage_refs[ i ];
				const auto offset = static_cast<std::size_t>( ref.address - page_address );
				const auto byte_count = raw.size( ) * sizeof( std::uint32_t );

				if ( page_ok && offset + byte_count <= page.size( ) )
				{
					std::memcpy( raw.data( ), page.data( ) + offset, byte_count );
				}
				else if ( !app::context().process.copy( ref.address, raw.data( ), byte_count ) )
				{
					app::context().diagnostics.warning( "[bomb_dmg] failed to read damage values at {:#x}", ref.address );
					return;
				}

				for ( std::size_t site = 0; site < site_count; ++site )
				{
					const auto packed = raw[ site ];
					damage_values[ site * point_count + ref.point_index ] = {
						static_cast<std::uint16_t>( packed & 0xffffu ),
						static_cast<std::uint8_t>( ( packed >> 16 ) & 0xffu ),
						static_cast<std::uint8_t>( packed >> 24 ) };
				}
			}

			first = last;
		}

		std::unordered_map<std::uint64_t, cell> cells;
		cells.reserve( point_count );
		std::size_t vertical_overflow{};

		for ( std::size_t i = 0; i < points.size( ); ++i )
		{
			const auto cx = ( static_cast<std::int32_t>( points[ i ].x ) - 5 ) / 10;
			const auto cy = ( static_cast<std::int32_t>( points[ i ].y ) - 5 ) / 10;
			auto& cell = cells[ cell_key( cx, cy ) ];

			if ( cell.count < std::size( cell.idx ) )
			{
				cell.idx[ cell.count++ ] = static_cast<std::uint32_t>( i );
			}
			else
			{
				++vertical_overflow;
			}
		}

		grid_bounds bounds{};
		if ( !points.empty( ) )
		{
			bounds.min_x = bounds.max_x = ( static_cast<std::int32_t>( points.front( ).x ) - 5 ) / 10;
			bounds.min_y = bounds.max_y = ( static_cast<std::int32_t>( points.front( ).y ) - 5 ) / 10;
			for ( const auto& point : points )
			{
				const auto x = ( static_cast<std::int32_t>( point.x ) - 5 ) / 10;
				const auto y = ( static_cast<std::int32_t>( point.y ) - 5 ) / 10;
				bounds.min_x = std::min( bounds.min_x, x );
				bounds.min_y = std::min( bounds.min_y, y );
				bounds.max_x = std::max( bounds.max_x, x );
				bounds.max_y = std::max( bounds.max_y, y );
			}
			bounds.valid = true;
		}

		{
			std::unique_lock lock( this->m_mutex );
			this->m_sites = std::move( sites );
			this->m_points = std::move( points );
			this->m_damage_values = std::move( damage_values );
			this->m_cells = std::move( cells );
			this->m_bounds = bounds;
		}

		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now( ) - started ).count( );
		app::context().diagnostics.success( "[bomb_dmg] loaded runtime object {:#x}: points={}, sites={}, slots={}, pages={}, z-overflow={}, {} ms",
			object, point_count, site_count, slot_count, page_reads, vertical_overflow, elapsed );
	}

}
