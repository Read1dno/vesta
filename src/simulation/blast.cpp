#include <stdafx.hpp>

namespace game {

	namespace detail_bd {

		static float dist_to_aabb( const foundation::vec3& p, const foundation::vec3& mins, const foundation::vec3& maxs )
		{
			const auto dx = std::max( { mins.x - p.x, 0.0f, p.x - maxs.x } );
			const auto dy = std::max( { mins.y - p.y, 0.0f, p.y - maxs.y } );
			const auto dz = std::max( { mins.z - p.z, 0.0f, p.z - maxs.z } );
			return std::sqrt( dx * dx + dy * dy + dz * dz );
		}

	}

	void blast_model::clear( )
	{
		std::unique_lock lock( this->m_mutex );
		this->m_sites.clear( );
		this->m_points.clear( );
		this->m_damage_values.clear( );
		this->m_cells.clear( );
		this->m_bounds = {};
	}

	bool blast_model::valid( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return !this->m_points.empty( ) && !this->m_sites.empty( );
	}

	std::size_t blast_model::point_count( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return this->m_points.size( );
	}

	std::int32_t blast_model::site_count( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return static_cast< std::int32_t >( this->m_sites.size( ) );
	}

	blast_model::site_info blast_model::site( std::int32_t index ) const
	{
		std::shared_lock lock( this->m_mutex );
		if ( index < 0 || index >= static_cast< std::int32_t >( this->m_sites.size( ) ) )
		{
			return {};
		}

		return this->m_sites[ index ];
	}

	blast_model::grid_bounds blast_model::bounds( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return this->m_bounds;
	}

	std::int32_t blast_model::site_for_position( const foundation::vec3& pos ) const
	{
		std::shared_lock lock( this->m_mutex );

		auto best{ -1 };
		auto best_dist{ 1e18f };

		for ( std::size_t s = 0; s < this->m_sites.size( ); ++s )
		{
			const auto d = detail_bd::dist_to_aabb( pos, this->m_sites[ s ].mins, this->m_sites[ s ].maxs );
			if ( d < best_dist )
			{
				best_dist = d;
				best = static_cast< std::int32_t >( s );
			}
		}

		return best;
	}

	const blast_model::cell* blast_model::find_cell( std::int32_t cx, std::int32_t cy ) const
	{
		const auto it = this->m_cells.find( cell_key( cx, cy ) );
		return it != this->m_cells.end( ) ? &it->second : nullptr;
	}

	std::int32_t blast_model::nearest_point_index( const foundation::vec3& pos ) const
	{
		const auto center_x = static_cast< std::int32_t >( std::lround( ( pos.x - 5.0f ) / 10.0f ) );
		const auto center_y = static_cast< std::int32_t >( std::lround( ( pos.y - 5.0f ) / 10.0f ) );
		auto best_idx{ -1 };
		auto best_dist_sqr{ std::numeric_limits< float >::max( ) };

		const auto consider_cell = [ & ]( std::int32_t cx, std::int32_t cy )
			{
				const auto* c = this->find_cell( cx, cy );
				if ( !c ) return;

				for ( std::uint8_t i = 0; i < c->count; ++i )
				{
					const auto idx = c->idx[ i ];
					const auto& point = this->m_points[ idx ];
					const auto dx = static_cast< float >( point.x ) - pos.x;
					const auto dy = static_cast< float >( point.y ) - pos.y;
					const auto dz = static_cast< float >( point.z ) - pos.z;
					const auto distance_sqr = dx * dx + dy * dy + dz * dz;
					if ( distance_sqr < best_dist_sqr )
					{
						best_dist_sqr = distance_sqr;
						best_idx = static_cast< std::int32_t >( idx );
					}
				}
			};

		for ( auto radius = 0; radius <= 512; ++radius )
		{
			if ( radius == 0 )
			{
				consider_cell( center_x, center_y );
			}
			else
			{
				for ( auto d = -radius; d <= radius; ++d )
				{
					consider_cell( center_x + d, center_y - radius );
					consider_cell( center_x + d, center_y + radius );
				}
				for ( auto d = -radius + 1; d < radius; ++d )
				{
					consider_cell( center_x - radius, center_y + d );
					consider_cell( center_x + radius, center_y + d );
				}
			}

			if ( best_idx >= 0 )
			{
				const auto next_ring_min = std::max( 0.0f, ( static_cast<float>( radius ) + 0.5f ) * 10.0f );
				if ( next_ring_min * next_ring_min > best_dist_sqr )
				{
					break;
				}
			}
		}

		return best_idx;
	}

	int blast_model::predicted_damage( const foundation::vec3& pos, std::int32_t site,
		const foundation::vec3& eye_angles, bool ducked ) const
	{
		std::shared_lock lock( this->m_mutex );

		const auto n = this->m_points.size( );
		if ( n == 0 || site < 0 || site >= static_cast< std::int32_t >( this->m_sites.size( ) ) )
		{
			return -1;
		}

		const auto best_idx = this->nearest_point_index( pos );
		if ( best_idx < 0 )
		{
			return -1;
		}

		return calculate_damage(
			this->m_damage_values[ static_cast< std::size_t >( site ) * n + best_idx ],
			this->m_sites[ site ], eye_angles, ducked );
	}

	bool blast_model::cell_damage( std::int32_t cell_x, std::int32_t cell_y, float ref_z,
		std::int32_t site, const foundation::vec3& eye_angles, bool ducked,
		float& out_damage, float& out_z ) const
	{
		std::shared_lock lock( this->m_mutex );

		const auto n = this->m_points.size( );
		if ( n == 0 || site < 0 || site >= static_cast< std::int32_t >( this->m_sites.size( ) ) )
		{
			return false;
		}
		if ( !this->find_cell( cell_x, cell_y ) )
		{
			return false;
		}

		const foundation::vec3 position{
			static_cast< float >( cell_x ) * 10.0f + 5.0f,
			static_cast< float >( cell_y ) * 10.0f + 5.0f,
			ref_z };
		const auto best_idx = this->nearest_point_index( position );
		if ( best_idx < 0 )
		{
			return false;
		}

		out_damage = static_cast< float >( calculate_damage(
			this->m_damage_values[ static_cast< std::size_t >( site ) * n + best_idx ],
			this->m_sites[ site ], eye_angles, ducked ) );
		out_z = static_cast< float >( this->m_points[ best_idx ].z );
		return true;
	}

	bool blast_model::sample_grid( std::int32_t min_x, std::int32_t min_y, int width, int height,
		float ref_z, std::int32_t site, std::vector<grid_sample>& out_samples ) const
	{
		std::shared_lock lock( this->m_mutex );

		const auto n = this->m_points.size( );
		if ( n == 0 || width <= 0 || height <= 0 ||
			site < 0 || site >= static_cast< std::int32_t >( this->m_sites.size( ) ) )
		{
			out_samples.clear( );
			return false;
		}

		out_samples.assign( static_cast< std::size_t >( width ) * height, {} );
		const auto& site_info = this->m_sites[ site ];

		for ( const auto& [ key, cell ] : this->m_cells )
		{
			if ( cell.count == 0 )
			{
				continue;
			}

			const auto cell_x = static_cast< std::int32_t >( static_cast< std::uint32_t >( key >> 32 ) );
			const auto cell_y = static_cast< std::int32_t >( static_cast< std::uint32_t >( key ) );
			const auto ix = cell_x - min_x;
			const auto iy = cell_y - min_y;
			if ( ix < 0 || ix >= width || iy < 0 || iy >= height )
			{
				continue;
			}

			const foundation::vec3 position{
				static_cast< float >( cell_x ) * 10.0f + 5.0f,
				static_cast< float >( cell_y ) * 10.0f + 5.0f,
				ref_z };
			const auto best_idx = this->nearest_point_index( position );
			if ( best_idx < 0 )
			{
				continue;
			}

			const auto& value = this->m_damage_values[ static_cast< std::size_t >( site ) * n + best_idx ];
			const foundation::vec3 wave_angles{
				static_cast< float >( value.pitch ) / 255.0f * 360.0f,
				static_cast< float >( value.yaw ) / 255.0f * 360.0f,
				0.0f };

			auto& sample = out_samples[ static_cast< std::size_t >( iy ) * width + ix ];
			wave_angles.to_directions( &sample.wave_forward, nullptr, nullptr );
			sample.z = static_cast< float >( this->m_points[ best_idx ].z );
			sample.base_damage = calculate_base_damage( value, site_info );
			sample.valid = true;
		}

		return true;
	}

#if 0
	bool blast_model::sample_grid_surfaces( const std::int32_t min_x,
		const std::int32_t min_y, const int width, const int height,
		const std::int32_t site, const foundation::vec3& reference_position,
		std::vector<std::vector<grid_sample>>& out_surfaces ) const
	{

		out_surfaces.resize( 1 );
		if ( !sample_grid( min_x, min_y, width, height,
			reference_position.z, site, out_surfaces.front( ) ) )
		{
			out_surfaces.clear( );
			return false;
		}
		return true;

#if 0
		std::shared_lock lock( this->m_mutex );
		const auto count = this->m_points.size( );
		if ( count == 0 || width <= 0 || height <= 0 || site < 0
			|| site >= static_cast<std::int32_t>( this->m_sites.size( ) ) )
		{
			out_surfaces.clear( );
			return false;
		}

		std::vector<std::uint32_t> parent( count );
		std::vector<std::uint32_t> component_size( count, 1u );
		std::iota( parent.begin( ), parent.end( ), 0u );
		const auto find_root = [ & ]( std::uint32_t value )
		{
			auto root = value;
			while ( parent[ root ] != root ) root = parent[ root ];
			while ( parent[ value ] != value )
			{
				const auto next = parent[ value ];
				parent[ value ] = root;
				value = next;
			}
			return root;
		};
		const auto unite = [ & ]( const std::uint32_t lhs, const std::uint32_t rhs )
		{
			auto a = find_root( lhs );
			auto b = find_root( rhs );
			if ( a == b ) return;
			if ( component_size[ a ] < component_size[ b ] ) std::swap( a, b );
			parent[ b ] = a;
			component_size[ a ] += component_size[ b ];
		};

		constexpr float max_surface_step{ 56.0f };
		for ( const auto& [ key, cell ] : this->m_cells )
		{
			if ( cell.count == 0 ) continue;
			const auto cell_x = static_cast<std::int32_t>( static_cast<std::uint32_t>( key >> 32 ) );
			const auto cell_y = static_cast<std::int32_t>( static_cast<std::uint32_t>( key ) );
			for ( int oy = -1; oy <= 1; ++oy )
			{
				for ( int ox = -1; ox <= 1; ++ox )
				{
					if ( oy < 0 || ( oy == 0 && ox < 0 ) ) continue;
					const auto* neighbour = this->find_cell( cell_x + ox, cell_y + oy );
					if ( !neighbour ) continue;
					for ( std::uint8_t a = 0; a < cell.count; ++a )
					{
						for ( std::uint8_t b = 0; b < neighbour->count; ++b )
						{
							const auto ia = cell.idx[ a ];
							const auto ib = neighbour->idx[ b ];
							if ( ia == ib ) continue;
							if ( std::abs( static_cast<float>( this->m_points[ ia ].z )
								- static_cast<float>( this->m_points[ ib ].z ) ) <= max_surface_step )
							{
								unite( ia, ib );
							}
						}
					}
				}
			}
		}

		std::unordered_map<std::uint32_t, std::uint32_t> sizes{};
		for ( std::uint32_t index = 0; index < count; ++index )
			++sizes[ find_root( index ) ];

		auto reference_index = std::uint32_t{};
		auto reference_distance = std::numeric_limits<float>::max( );
		if ( std::isfinite( reference_position.x )
			&& std::isfinite( reference_position.y )
			&& std::isfinite( reference_position.z ) )
		{
			for ( std::uint32_t index = 0; index < count; ++index )
			{
				const auto& point = this->m_points[ index ];
				const auto dx = static_cast<float>( point.x ) - reference_position.x;
				const auto dy = static_cast<float>( point.y ) - reference_position.y;
				const auto dz = static_cast<float>( point.z ) - reference_position.z;
				const auto distance = dx * dx + dy * dy + dz * dz;
				if ( distance < reference_distance )
				{
					reference_distance = distance;
					reference_index = index;
				}
			}
		}
		const auto reference_root = find_root( reference_index );

		const std::vector<std::pair<std::uint32_t, std::uint32_t>> roots{
			{ reference_root, sizes[ reference_root ] } };

		const auto slots = static_cast<std::size_t>( width ) * height;
		out_surfaces.assign( roots.size( ), std::vector<grid_sample>( slots ) );
		std::unordered_map<std::uint32_t, std::size_t> surface_for_root{};
		for ( std::size_t surface = 0; surface < roots.size( ); ++surface )
			surface_for_root.emplace( roots[ surface ].first, surface );

		const auto& site_info = this->m_sites[ site ];
		for ( std::uint32_t index = 0; index < count; ++index )
		{
			const auto found = surface_for_root.find( find_root( index ) );
			if ( found == surface_for_root.end( ) ) continue;
			const auto& point = this->m_points[ index ];
			const auto cell_x = static_cast<std::int32_t>( std::lround(
				( static_cast<float>( point.x ) - 5.0f ) / 10.0f ) );
			const auto cell_y = static_cast<std::int32_t>( std::lround(
				( static_cast<float>( point.y ) - 5.0f ) / 10.0f ) );
			const auto ix = cell_x - min_x;
			const auto iy = cell_y - min_y;
			if ( ix < 0 || ix >= width || iy < 0 || iy >= height ) continue;

			const auto& value = this->m_damage_values[
				static_cast<std::size_t>( site ) * count + index ];
			const foundation::vec3 wave_angles{
				static_cast<float>( value.pitch ) / 255.0f * 360.0f,
				static_cast<float>( value.yaw ) / 255.0f * 360.0f, 0.0f };
			auto& sample = out_surfaces[ found->second ][
				static_cast<std::size_t>( iy ) * width + ix ];
			wave_angles.to_directions( &sample.wave_forward, nullptr, nullptr );
			sample.z = static_cast<float>( point.z );
			sample.base_damage = calculate_base_damage( value, site_info );
			sample.valid = true;
		}

		auto& surface = out_surfaces.front( );
		for ( int pass = 0; pass < 2; ++pass )
		{
			std::vector<std::pair<std::size_t, grid_sample>> fills{};
			for ( int iy = 1; iy + 1 < height; ++iy )
			{
				for ( int ix = 1; ix + 1 < width; ++ix )
				{
					const auto slot = static_cast<std::size_t>( iy ) * width + ix;
					if ( surface[ slot ].valid ) continue;
					std::array<const grid_sample*, 8> neighbours{};
					int neighbour_count{};
					for ( int oy = -1; oy <= 1; ++oy )
					{
						for ( int ox = -1; ox <= 1; ++ox )
						{
							if ( ox == 0 && oy == 0 ) continue;
							const auto& candidate = surface[
								static_cast<std::size_t>( iy + oy ) * width + ix + ox ];
							if ( candidate.valid ) neighbours[ neighbour_count++ ] = &candidate;
						}
					}
					const auto left = surface[ slot - 1 ].valid;
					const auto right = surface[ slot + 1 ].valid;
					const auto up = surface[ slot - width ].valid;
					const auto down = surface[ slot + width ].valid;
					if ( neighbour_count < 3 && !( left && right ) && !( up && down ) )
						continue;
					auto min_z = std::numeric_limits<float>::max( );
					auto max_z = std::numeric_limits<float>::lowest( );
					grid_sample fill{};
					for ( int i = 0; i < neighbour_count; ++i )
					{
						const auto& candidate = *neighbours[ i ];
						min_z = std::min( min_z, candidate.z );
						max_z = std::max( max_z, candidate.z );
						fill.wave_forward += candidate.wave_forward;
						fill.z += candidate.z;
						fill.base_damage += candidate.base_damage;
					}
					if ( max_z - min_z > max_surface_step ) continue;
					const auto inverse = 1.0f / static_cast<float>( neighbour_count );
					fill.wave_forward *= inverse;
					fill.wave_forward.normalize( );
					fill.z *= inverse;
					fill.base_damage = static_cast<std::int32_t>( std::lround(
						static_cast<float>( fill.base_damage ) * inverse ) );
					fill.valid = true;
					fills.emplace_back( slot, fill );
				}
			}
			if ( fills.empty( ) ) break;
			for ( const auto& [ slot, fill ] : fills ) surface[ slot ] = fill;
		}

		return !out_surfaces.empty( );
#endif
	}
#endif

	bool blast_model::sample_grid_surfaces( const std::int32_t site,
		const float minimum_iso, const float maximum_iso,
		std::vector<grid_quad>& out_surfaces ) const
	{
		std::shared_lock lock( this->m_mutex );
		const auto point_count = this->m_points.size( );
		if ( point_count == 0 || site < 0
			|| site >= static_cast<std::int32_t>( this->m_sites.size( ) ) )
		{
			out_surfaces.clear( );
			return false;
		}

		out_surfaces.clear( );
		out_surfaces.reserve( this->m_cells.size( ) );
		const auto& site_info = this->m_sites[ site ];
		constexpr auto max_edge_step = 96.0f;
		constexpr auto invalid_index = std::numeric_limits<std::uint32_t>::max( );

		const auto closest_to = [ & ]( const cell& candidates, const float z )
			{
				auto best = invalid_index;
				auto best_delta = max_edge_step + 1.0f;
				for ( std::uint8_t i = 0; i < candidates.count; ++i )
				{
					const auto index = candidates.idx[ i ];
					const auto delta = std::abs(
						static_cast<float>( this->m_points[ index ].z ) - z );
					if ( delta < best_delta )
					{
						best_delta = delta;
						best = index;
					}
				}
				return best_delta <= max_edge_step ? best : invalid_index;
			};

		const auto make_sample = [ & ]( const std::uint32_t index )
			{
				const auto& value = this->m_damage_values[
					static_cast<std::size_t>( site ) * point_count + index ];
				const foundation::vec3 wave_angles{
					static_cast<float>( value.pitch ) / 255.0f * 360.0f,
					static_cast<float>( value.yaw ) / 255.0f * 360.0f, 0.0f };
				grid_sample sample{};
				wave_angles.to_directions( &sample.wave_forward, nullptr, nullptr );
				sample.z = static_cast<float>( this->m_points[ index ].z );
				sample.base_damage = calculate_base_damage( value, site_info );
				sample.valid = true;
				return sample;
			};

		for ( const auto& [ key, c0 ] : this->m_cells )
		{
			if ( c0.count == 0 ) continue;
			const auto cell_x = static_cast<std::int32_t>(
				static_cast<std::uint32_t>( key >> 32 ) );
			const auto cell_y = static_cast<std::int32_t>(
				static_cast<std::uint32_t>( key ) );
			const auto* c1 = this->find_cell( cell_x + 1, cell_y );
			const auto* c2 = this->find_cell( cell_x + 1, cell_y + 1 );
			const auto* c3 = this->find_cell( cell_x, cell_y + 1 );
			if ( !c1 || !c2 || !c3 ) continue;

			for ( std::uint8_t layer = 0; layer < c0.count; ++layer )
			{
				const auto i0 = c0.idx[ layer ];
				const auto z0 = static_cast<float>( this->m_points[ i0 ].z );
				const auto i1 = closest_to( *c1, z0 );
				const auto i3 = closest_to( *c3, z0 );
				if ( i1 == invalid_index || i3 == invalid_index ) continue;

				auto i2 = invalid_index;
				auto best_error = std::numeric_limits<float>::max( );
				const auto z1 = static_cast<float>( this->m_points[ i1 ].z );
				const auto z3 = static_cast<float>( this->m_points[ i3 ].z );
				for ( std::uint8_t candidate = 0; candidate < c2->count; ++candidate )
				{
					const auto index = c2->idx[ candidate ];
					const auto z2 = static_cast<float>( this->m_points[ index ].z );
					const auto edge_1 = std::abs( z2 - z1 );
					const auto edge_3 = std::abs( z2 - z3 );
					if ( edge_1 > max_edge_step || edge_3 > max_edge_step ) continue;
					const auto error = std::max( edge_1, edge_3 )
						+ std::abs( z2 - z0 ) * 0.25f;
					if ( error < best_error )
					{
						best_error = error;
						i2 = index;
					}
				}
				if ( i2 == invalid_index ) continue;

				grid_quad quad{};
				quad.cell_x = cell_x;
				quad.cell_y = cell_y;
				quad.point_indices = { i0, i1, i2, i3 };
				for ( std::size_t corner = 0; corner < 4; ++corner )
				{
					quad.samples[ corner ] = make_sample( quad.point_indices[ corner ] );
					quad.damage[ corner ] = static_cast<float>(
						calculate_sample_damage_worst_case( quad.samples[ corner ] ) );
				}

				const auto [ low, high ] = std::minmax_element(
					quad.damage.begin( ), quad.damage.end( ) );
				if ( *low <= maximum_iso && *high >= minimum_iso )
					out_surfaces.push_back( std::move( quad ) );
			}
		}

		return !out_surfaces.empty( );
	}

	bool blast_model::evaluate_grid( const std::vector<grid_sample>& samples,
		const foundation::vec3& eye_angles, bool ducked, std::vector<float>& out_damage )
	{
		foundation::vec3 player_forward{};
		eye_angles.to_directions( &player_forward, nullptr, nullptr );

		out_damage.resize( samples.size( ) );
		for ( std::size_t i = 0; i < samples.size( ); ++i )
		{
			out_damage[ i ] = samples[ i ].valid
				? static_cast< float >( calculate_sample_damage( samples[ i ], player_forward, ducked ) )
				: NAN;
		}
		return !samples.empty( );
	}

	bool blast_model::evaluate_grid_worst_case( const std::vector<grid_sample>& samples,
		std::vector<float>& out_damage )
	{
		out_damage.resize( samples.size( ) );
		for ( std::size_t i = 0; i < samples.size( ); ++i )
		{
			out_damage[ i ] = samples[ i ].valid
				? static_cast< float >( calculate_sample_damage_worst_case( samples[ i ] ) )
				: NAN;
		}
		return !samples.empty( );
	}

	std::int32_t blast_model::calculate_base_damage( const damage_value& value, const site_info& site )
	{
		const auto wave = static_cast< float >( value.wave_dist );
		const auto span = std::clamp( wave, 0.0f, 1800.0f );
		auto base_damage = 0.0f;

		if ( span == 0.0f )
		{
			base_damage = wave < site.scale ? 100.0f : 0.0f;
		}
		else
		{
			base_damage = 100.0f + ( wave - site.scale ) * -100.0f / span;
		}

		return std::clamp( static_cast< int >( base_damage ), 0, 255 );
	}

	std::int32_t blast_model::calculate_sample_damage( const grid_sample& sample,
		const foundation::vec3& player_forward, bool ducked )
	{
		auto damage = sample.base_damage;
		if ( damage >= 100 )
		{
			return damage;
		}

		const auto bias = [ ]( float x, float amount )
			{
				x = std::clamp( x, 0.0f, 1.0f );
				amount = std::clamp( amount, std::numeric_limits< float >::min( ), 1.0f );
				return x / ( ( 1.0f / amount - 2.0f ) * ( 1.0f - x ) + 1.0f );
			};

		const auto apply_bias = [ & ]( float amount )
			{
				damage = std::clamp( static_cast< int >(
					bias( static_cast< float >( damage ) / 100.0f, amount ) * 100.0f ), 0, 255 );
			};

		if ( ducked )
		{
			apply_bias( 0.45f );
		}

		const auto facing = std::clamp( ( sample.wave_forward.dot( player_forward ) + 1.0f ) * 0.5f, 0.0f, 1.0f );
		apply_bias( 0.53f - facing * 0.06f );
		return damage;
	}

	std::int32_t blast_model::calculate_sample_damage_worst_case( const grid_sample& sample )
	{

		const foundation::vec3 opposite_wave{
			-sample.wave_forward.x,
			-sample.wave_forward.y,
			-sample.wave_forward.z };
		return calculate_sample_damage( sample, opposite_wave, false );
	}

	int blast_model::calculate_damage( const damage_value& value, const site_info& site,
		const foundation::vec3& eye_angles, bool ducked )
	{

		foundation::vec3 player_forward{};
		eye_angles.to_directions( &player_forward, nullptr, nullptr );

		const foundation::vec3 wave_angles{
			static_cast< float >( value.pitch ) / 255.0f * 360.0f,
			static_cast< float >( value.yaw ) / 255.0f * 360.0f,
			0.0f };
		grid_sample sample{};
		wave_angles.to_directions( &sample.wave_forward, nullptr, nullptr );
		sample.base_damage = calculate_base_damage( value, site );
		sample.valid = true;
		return calculate_sample_damage( sample, player_forward, ducked );
	}

}
