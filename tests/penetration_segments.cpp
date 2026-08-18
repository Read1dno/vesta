#include <cassert>
#include <cmath>
#include <initializer_list>
#include <numbers>

#include <simulation/collision_segments.hpp>
#include <simulation/penetration_solver.hpp>

namespace {

	using world = game::collision_world;

	world::hit_entry contact( float distance, bool entering,
		std::uint64_t solid, float penetration = 1.0f,
		std::uint16_t material = 67, float density = 0.0f )
	{
		return {
			.distance = distance,
			.fraction = distance / 100.0f,
			.surface = { .penetration = penetration,
				.surface_type = material, .density = density },
			.is_enter = entering,
			.solid_id = solid
		};
	}

	bool near( float left, float right )
	{
		return std::abs( left - right ) < 0.0001f;
	}

}

int main( )
{
	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1 ), contact( 12.0f, false, 1 ),
			contact( 20.0f, true, 2 ), contact( 22.0f, false, 2 ) }, 100.0f );
		assert( built.segments.size( ) == 2 );
		assert( near( built.segments[ 0 ].thickness, 2.0f ) );
		assert( near( built.segments[ 1 ].thickness, 2.0f ) );
		assert( built.records.size( ) == 4 );
		assert( built.records[ 0 ].range_loss );
		assert( built.records[ 0 ].first_contact == 0 );
		assert( built.records[ 0 ].last_contact == 1 );
		assert( !built.records[ 1 ].range_loss );
		assert( built.records[ 2 ].first_contact == 2 );
		assert( built.records[ 2 ].last_contact == 2 );
		assert( built.records[ 3 ].first_contact == 2 );
		assert( built.records[ 3 ].last_contact == 3 );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1, 1.0f ), contact( 25.0f, false, 1, 1.0f ),
			contact( 20.0f, true, 2, 0.5f ), contact( 30.0f, false, 2, 0.5f ) }, 100.0f );
		assert( built.segments.size( ) == 1 );
		assert( near( built.segments[ 0 ].enter_distance, 10.0f ) );
		assert( near( built.segments[ 0 ].exit_distance, 30.0f ) );
		assert( near( built.segments[ 0 ].thickness, 20.0f ) );
		assert( near( built.segments[ 0 ].min_pen_mod, 0.5f ) );
		assert( built.records.size( ) == 2 );
		assert( built.records[ 1 ].first_contact == 0 );
		assert( built.records[ 1 ].last_contact == 3 );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1 ), contact( 20.0f, false, 1 ),
			contact( 20.001f, true, 2 ), contact( 30.0f, false, 2 ) }, 100.0f );
		assert( built.segments.size( ) == 2 );
		assert( near( built.segments[ 0 ].thickness, 10.0f ) );
		assert( near( built.segments[ 1 ].thickness, 9.999f ) );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1, 0.5f ), contact( 40.0f, false, 1, 0.5f ),
			contact( 11.0f, true, 2, 0.5f ), contact( 39.0f, false, 2, 0.5f ),
			contact( 12.0f, true, 3, 0.5f ), contact( 38.0f, false, 3, 0.5f ),
			contact( 13.0f, true, 4, 0.5f ), contact( 37.0f, false, 4, 0.5f ) }, 100.0f );
		assert( built.segments.size( ) == 1 );
		assert( near( built.segments[ 0 ].enter_distance, 10.0f ) );
		assert( near( built.segments[ 0 ].exit_distance, 40.0f ) );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1, 0.5f ),
			contact( 11.0f, true, 2, 0.5f ),
			contact( 12.0f, false, 1, 0.5f ),
			contact( 80.0f, false, 2, 0.5f ) }, 100.0f );
		assert( built.segments.size( ) == 1 );
		assert( near( built.segments[ 0 ].enter_distance, 10.0f ) );
		assert( near( built.segments[ 0 ].exit_distance, 80.0f ) );
		assert( near( built.segments[ 0 ].thickness, 70.0f ) );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 7 ), contact( 10.0f, true, 7 ),
			contact( 12.0f, false, 7 ), contact( 12.0f, false, 7 ),
			contact( 20.0f, true, 7 ), contact( 22.0f, false, 7 ) }, 100.0f );
		assert( built.segments.size( ) == 2 );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 10.0f, true, 9 ) }, 100.0f );
		assert( built.segments.empty( ) );
		assert( built.unresolved_before( 11.0f ) );
		assert( !built.unresolved_before( 9.0f ) );
	}

	{
		auto built = game::collision_detail::build_segments( {
			contact( 69.28f, true, 4813, 0.4f, 77, 2000.0f ),
			contact( 70.70f, false, 4813, 0.4f, 77, 2000.0f ),
			contact( 194.23f, true, 1, 0.7f, 50, 2000.0f ),
			contact( 198.83f, true, 1, 0.5f, 67, 2000.0f ),
			contact( 276.97f, true, 1, 0.9f, 87, 700.0f ),
			contact( 276.971f, false, 1, 0.5f, 67, 2400.0f ),
			contact( 277.63f, false, 1, 0.5f, 67, 2000.0f ),
			contact( 280.38f, false, 1, 0.9f, 87, 700.0f ) }, 470.0f );
		assert( built.segments.size( ) == 2 );
		assert( near( built.segments[ 1 ].enter_distance, 194.23f ) );
		assert( near( built.segments[ 1 ].exit_distance, 280.38f ) );
		assert( built.segments[ 1 ].thickness > 86.0f );
		assert( !simulation::detail::pass_through_world(
			built, 470.0f, 2.5f, 115.0f, 0.99f, true ) );
	}

	{
		auto thin = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1, 0.5f, 67, 2400.0f ),
			contact( 20.0f, false, 1, 0.5f, 67, 2400.0f ) }, 100.0f );
		auto passage = simulation::detail::pass_through_world(
			thin, 80.0f, 3.0f, 115.0f, 0.99f, true );
		assert( passage );
		assert( passage->penetrations == 1 );
		assert( passage->damage > 79.0f && passage->damage < 81.0f );

		auto thick = game::collision_detail::build_segments( {
			contact( 10.0f, true, 1, 0.5f, 67, 2400.0f ),
			contact( 50.0f, false, 1, 0.5f, 67, 2400.0f ) }, 100.0f );
		assert( !simulation::detail::pass_through_world(
			thick, 80.0f, 3.0f, 115.0f, 0.99f, true ) );
	}

	{
		std::vector<world::hit_entry> hits{};
		for ( std::uint64_t wall = 0; wall < 5; ++wall )
		{
			const auto start = 10.0f + static_cast<float>( wall ) * 4.0f;
			hits.push_back( contact( start, true, wall, 3.0f, 87 ) );
			hits.push_back( contact( start + 1.0f, false, wall, 3.0f, 87 ) );
		}
		auto built = game::collision_detail::build_segments(
			std::move( hits ), 100.0f );
		auto passage = simulation::detail::pass_through_world(
			built, 90.0f, 10.0f, 500.0f, 0.99f, true );
		assert( passage );
		assert( passage->penetrations == 5 );
	}
}
