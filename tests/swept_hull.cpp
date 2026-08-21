#include <cassert>
#include <cmath>
#include <numbers>

#include <simulation/swept_hull.hpp>

namespace {

bool close( const float left, const float right, const float tolerance = 1e-3f )
{
	return std::abs( left - right ) <= tolerance;
}

}

int main( )
{
	using foundation::vec3;
	using simulation::geometry::sweep_aabb_triangle;
	constexpr vec3 grenade_extents{ 2.0f, 2.0f, 2.0f };

	{
		const auto hit = sweep_aabb_triangle( { 10.0f, 0.0f, 0.0f },
			{ -1.0f, 0.0f, 0.0f }, 20.0f, grenade_extents,
			{ 0.0f, -10.0f, -10.0f }, { 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 10.0f } );
		assert( hit );
		assert( close( hit->distance, 8.0f ) );
		assert( close( hit->center.x, 2.0f ) );
		assert( hit->normal.x > 0.99f );
	}

	{
		const auto inverse_sqrt_two = 1.0f / std::sqrt( 2.0f );
		const vec3 direction{ -inverse_sqrt_two, -inverse_sqrt_two, 0.0f };
		const auto hit = sweep_aabb_triangle( { 10.0f, 10.0f, 0.0f },
			direction, 30.0f, grenade_extents,
			{ 0.0f, 0.0f, -20.0f }, { 10.0f, -10.0f, 20.0f }, { -10.0f, 10.0f, 20.0f } );
		assert( hit );
		const auto expected = std::sqrt( 200.0f ) - 2.0f * std::sqrt( 2.0f );
		assert( close( hit->distance, expected ) );
		assert( hit->normal.x > 0.7f && hit->normal.y > 0.7f );
	}

	{
		const auto miss = sweep_aabb_triangle( { 10.0f, 30.0f, 30.0f },
			{ -1.0f, 0.0f, 0.0f }, 20.0f, grenade_extents,
			{ 0.0f, -10.0f, -10.0f }, { 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 10.0f } );
		assert( !miss );
	}

	{
		const auto moving_away = sweep_aabb_triangle( { 2.0f, 0.0f, 0.0f },
			{ 1.0f, 0.0f, 0.0f }, 20.0f, grenade_extents,
			{ 0.0f, -10.0f, -10.0f }, { 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 10.0f } );
		assert( !moving_away );
	}
}
