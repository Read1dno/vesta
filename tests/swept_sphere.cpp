#include <cassert>
#include <cmath>
#include <numbers>

#include <simulation/swept_sphere.hpp>

namespace {

bool close( const float left, const float right, const float tolerance = 1e-3f )
{
	return std::abs( left - right ) <= tolerance;
}

}

int main( )
{
	using foundation::vec3;
	using simulation::geometry::sweep_sphere_triangle;

	{
		const auto hit = sweep_sphere_triangle( { 10.0f, 0.0f, 0.0f },
			{ -1.0f, 0.0f, 0.0f }, 20.0f, 2.0f,
			{ 0.0f, -10.0f, -10.0f }, { 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 10.0f } );
		assert( hit );
		assert( close( hit->distance, 8.0f ) );
		assert( close( hit->center.x, 2.0f ) );
		assert( close( hit->normal.x, 1.0f ) );
	}

	{
		const auto hit = sweep_sphere_triangle( { 5.0f, -5.0f, 1.0f },
			{ 0.0f, 1.0f, 0.0f }, 20.0f, 2.0f,
			{ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } );
		assert( hit );
		assert( close( hit->distance, 5.0f - std::sqrt( 3.0f ) ) );
		assert( hit->normal.y < -0.8f );
		assert( hit->normal.z > 0.4f );
	}

	{
		const auto direction = vec3{ 1.0f, 1.0f, 0.0f } / std::sqrt( 2.0f );
		const auto hit = sweep_sphere_triangle( { -5.0f, -5.0f, 0.0f },
			direction, 20.0f, 2.0f,
			{ 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } );
		assert( hit );
		assert( close( hit->distance, std::sqrt( 50.0f ) - 2.0f ) );
	}

	{
		const auto miss = sweep_sphere_triangle( { 10.0f, 30.0f, 30.0f },
			{ -1.0f, 0.0f, 0.0f }, 20.0f, 2.0f,
			{ 0.0f, -10.0f, -10.0f }, { 0.0f, 10.0f, -10.0f }, { 0.0f, 0.0f, 10.0f } );
		assert( !miss );
	}
}
