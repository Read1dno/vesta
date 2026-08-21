#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <core/math/vector.hpp>
#include <simulation/swept_sphere.hpp>

namespace simulation::geometry {

struct swept_hull_contact
{
	float distance{};
	foundation::vec3 center{};
	foundation::vec3 point{};
	foundation::vec3 normal{};
};

[[nodiscard]] inline std::optional<swept_hull_contact> sweep_aabb_triangle(
	const foundation::vec3& origin, const foundation::vec3& direction,
	float maximum_distance, const foundation::vec3& half_extents,
	const foundation::vec3& a, const foundation::vec3& b,
	const foundation::vec3& c ) noexcept
{
	if ( maximum_distance <= 0.0f || half_extents.x <= 0.0f
		|| half_extents.y <= 0.0f || half_extents.z <= 0.0f )
		return std::nullopt;

	const auto edge0 = b - a;
	const auto edge1 = c - b;
	const auto edge2 = a - c;
	const auto triangle_normal = edge0.cross( c - a );
	if ( triangle_normal.length_sqr( ) <= 1e-12f ) return std::nullopt;

	const foundation::vec3 axis_x{ 1.0f, 0.0f, 0.0f };
	const foundation::vec3 axis_y{ 0.0f, 1.0f, 0.0f };
	const foundation::vec3 axis_z{ 0.0f, 0.0f, 1.0f };
	const std::array axes{
		axis_x, axis_y, axis_z, triangle_normal,
		axis_x.cross( edge0 ), axis_x.cross( edge1 ), axis_x.cross( edge2 ),
		axis_y.cross( edge0 ), axis_y.cross( edge1 ), axis_y.cross( edge2 ),
		axis_z.cross( edge0 ), axis_z.cross( edge1 ), axis_z.cross( edge2 ) };

	auto entry = -std::numeric_limits<float>::infinity( );
	auto exit = std::numeric_limits<float>::infinity( );
	auto entry_normal = foundation::vec3{};
	auto overlap_normal = foundation::vec3{};
	auto minimum_overlap = std::numeric_limits<float>::infinity( );
	auto initially_overlapping = true;

	for ( auto axis : axes )
	{
		const auto length_sqr = axis.length_sqr( );
		if ( length_sqr <= 1e-12f ) continue;
		axis /= std::sqrt( length_sqr );

		const auto pa = a.dot( axis );
		const auto pb = b.dot( axis );
		const auto pc = c.dot( axis );
		const auto triangle_min = std::min( { pa, pb, pc } );
		const auto triangle_max = std::max( { pa, pb, pc } );
		const auto center = origin.dot( axis );
		const auto extent = std::abs( axis.x ) * half_extents.x
			+ std::abs( axis.y ) * half_extents.y
			+ std::abs( axis.z ) * half_extents.z;
		const auto box_min = center - extent;
		const auto box_max = center + extent;
		const auto overlap = std::min( box_max, triangle_max )
			- std::max( box_min, triangle_min );
		if ( overlap < -1e-5f ) initially_overlapping = false;
		else if ( overlap < minimum_overlap )
		{
			minimum_overlap = overlap;
			const auto triangle_center = ( triangle_min + triangle_max ) * 0.5f;
			overlap_normal = center < triangle_center ? -axis : axis;
		}

		const auto speed = direction.dot( axis );
		if ( std::abs( speed ) <= 1e-8f )
		{
			if ( box_max < triangle_min - 1e-5f
				|| box_min > triangle_max + 1e-5f ) return std::nullopt;
			continue;
		}

		auto axis_entry = ( triangle_min - box_max ) / speed;
		auto axis_exit = ( triangle_max - box_min ) / speed;
		if ( axis_entry > axis_exit ) std::swap( axis_entry, axis_exit );
		if ( axis_entry > entry )
		{
			entry = axis_entry;
			entry_normal = speed > 0.0f ? -axis : axis;
		}
		exit = std::min( exit, axis_exit );
		if ( entry > exit + 1e-6f ) return std::nullopt;
	}

	if ( exit < -1e-5f || entry > maximum_distance + 1e-5f ) return std::nullopt;
	auto distance = std::max( entry, 0.0f );
	auto normal = entry_normal;
	if ( initially_overlapping )
	{
		distance = 0.0f;
		normal = overlap_normal;
		if ( direction.dot( normal ) >= -1e-6f ) return std::nullopt;
	}
	else if ( direction.dot( normal ) > 0.0f )
	{
		normal = -normal;
	}

	const auto center = origin + direction * distance;
	return swept_hull_contact{
		.distance = distance,
		.center = center,
		.point = closest_point_on_triangle( center, a, b, c ),
		.normal = normal };
}

}
