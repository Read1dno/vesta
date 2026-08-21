#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <core/math/vector.hpp>

namespace simulation::geometry {

struct swept_sphere_contact
{
	float distance{};
	foundation::vec3 center{};
	foundation::vec3 point{};
	foundation::vec3 normal{};
};

[[nodiscard]] inline foundation::vec3 closest_point_on_segment(
	const foundation::vec3& point, const foundation::vec3& a,
	const foundation::vec3& b ) noexcept
{
	const auto edge = b - a;
	const auto denominator = edge.length_sqr( );
	if ( denominator <= 1e-12f )
		return a;
	const auto t = std::clamp( ( point - a ).dot( edge ) / denominator, 0.0f, 1.0f );
	return a + edge * t;
}

[[nodiscard]] inline foundation::vec3 closest_point_on_triangle(
	const foundation::vec3& point, const foundation::vec3& a,
	const foundation::vec3& b, const foundation::vec3& c ) noexcept
{
	const auto ab = b - a;
	const auto ac = c - a;
	const auto ap = point - a;
	const auto d1 = ab.dot( ap );
	const auto d2 = ac.dot( ap );
	if ( d1 <= 0.0f && d2 <= 0.0f ) return a;

	const auto bp = point - b;
	const auto d3 = ab.dot( bp );
	const auto d4 = ac.dot( bp );
	if ( d3 >= 0.0f && d4 <= d3 ) return b;

	const auto vc = d1 * d4 - d3 * d2;
	if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f )
		return a + ab * ( d1 / ( d1 - d3 ) );

	const auto cp = point - c;
	const auto d5 = ab.dot( cp );
	const auto d6 = ac.dot( cp );
	if ( d6 >= 0.0f && d5 <= d6 ) return c;

	const auto vb = d5 * d2 - d1 * d6;
	if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f )
		return a + ac * ( d2 / ( d2 - d6 ) );

	const auto va = d3 * d6 - d5 * d4;
	if ( va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f )
		return b + ( c - b ) * ( ( d4 - d3 ) / ( d4 - d3 + d5 - d6 ) );

	const auto denominator = 1.0f / ( va + vb + vc );
	return a + ab * ( vb * denominator ) + ac * ( vc * denominator );
}

[[nodiscard]] inline bool point_in_triangle( const foundation::vec3& point,
	const foundation::vec3& a, const foundation::vec3& b,
	const foundation::vec3& c, const foundation::vec3& normal ) noexcept
{
	constexpr auto epsilon = -1e-4f;
	return normal.dot( ( b - a ).cross( point - a ) ) >= epsilon
		&& normal.dot( ( c - b ).cross( point - b ) ) >= epsilon
		&& normal.dot( ( a - c ).cross( point - c ) ) >= epsilon;
}

[[nodiscard]] inline std::optional<float> ray_sphere_distance(
	const foundation::vec3& origin, const foundation::vec3& direction,
	float maximum_distance, const foundation::vec3& center, float radius ) noexcept
{
	const auto offset = origin - center;
	const auto b = offset.dot( direction );
	const auto c = offset.length_sqr( ) - radius * radius;
	if ( c <= 0.0f ) return 0.0f;
	if ( b >= 0.0f ) return std::nullopt;
	const auto discriminant = b * b - c;
	if ( discriminant < 0.0f ) return std::nullopt;
	const auto distance = -b - std::sqrt( discriminant );
	if ( distance < 0.0f || distance > maximum_distance ) return std::nullopt;
	return distance;
}

[[nodiscard]] inline std::optional<float> ray_capsule_distance(
	const foundation::vec3& origin, const foundation::vec3& direction,
	float maximum_distance, const foundation::vec3& a,
	const foundation::vec3& b, float radius ) noexcept
{
	const auto axis = b - a;
	const auto offset = origin - a;
	const auto axis_length_sqr = axis.length_sqr( );
	if ( axis_length_sqr <= 1e-12f )
		return ray_sphere_distance( origin, direction, maximum_distance, a, radius );

	const auto axis_direction = axis.dot( direction );
	const auto axis_offset = axis.dot( offset );
	const auto direction_offset = direction.dot( offset );
	const auto offset_sqr = offset.length_sqr( );
	const auto qa = axis_length_sqr - axis_direction * axis_direction;
	const auto qb = axis_length_sqr * direction_offset - axis_offset * axis_direction;
	const auto qc = axis_length_sqr * offset_sqr - axis_offset * axis_offset
		- radius * radius * axis_length_sqr;

	if ( std::abs( qa ) > 1e-8f )
	{
		const auto discriminant = qb * qb - qa * qc;
		if ( discriminant >= 0.0f )
		{
			const auto distance = ( -qb - std::sqrt( discriminant ) ) / qa;
			const auto along = axis_offset + distance * axis_direction;
			if ( distance >= 0.0f && distance <= maximum_distance
				&& along >= 0.0f && along <= axis_length_sqr )
				return distance;
		}
	}

	auto nearest = std::numeric_limits<float>::infinity( );
	if ( const auto hit = ray_sphere_distance( origin, direction,
		maximum_distance, a, radius ) ) nearest = std::min( nearest, *hit );
	if ( const auto hit = ray_sphere_distance( origin, direction,
		maximum_distance, b, radius ) ) nearest = std::min( nearest, *hit );
	if ( !std::isfinite( nearest ) ) return std::nullopt;
	return nearest;
}

[[nodiscard]] inline std::optional<swept_sphere_contact> sweep_sphere_triangle(
	const foundation::vec3& origin, const foundation::vec3& direction,
	float maximum_distance, float radius, const foundation::vec3& a,
	const foundation::vec3& b, const foundation::vec3& c ) noexcept
{
	if ( maximum_distance <= 0.0f || radius <= 0.0f ) return std::nullopt;
	const auto raw_normal = ( b - a ).cross( c - a );
	const auto normal_length_sqr = raw_normal.length_sqr( );
	if ( normal_length_sqr <= 1e-12f ) return std::nullopt;
	const auto plane_normal = raw_normal / std::sqrt( normal_length_sqr );

	std::optional<swept_sphere_contact> nearest{};
	const auto consider = [ & ]( float distance, const foundation::vec3& point,
		foundation::vec3 normal )
	{
		if ( distance < 0.0f || distance > maximum_distance
			|| ( nearest && distance >= nearest->distance ) ) return;
		const auto normal_sqr = normal.length_sqr( );
		if ( normal_sqr <= 1e-12f ) return;
		normal /= std::sqrt( normal_sqr );
		if ( direction.dot( normal ) >= -1e-6f && distance > 1e-5f ) return;
		nearest = swept_sphere_contact{
			.distance = distance,
			.center = origin + direction * distance,
			.point = point,
			.normal = normal };
	};

	const auto initial_point = closest_point_on_triangle( origin, a, b, c );
	const auto initial_delta = origin - initial_point;
	if ( initial_delta.length_sqr( ) < radius * radius - 1e-5f )
	{
		auto normal = initial_delta;
		if ( normal.length_sqr( ) <= 1e-12f )
			normal = direction.dot( plane_normal ) <= 0.0f ? plane_normal : -plane_normal;
		if ( direction.dot( normal ) < 0.0f ) consider( 0.0f, initial_point, normal );
	}

	const auto signed_distance = ( origin - a ).dot( plane_normal );
	const auto normal_speed = direction.dot( plane_normal );
	if ( std::abs( normal_speed ) > 1e-8f )
	{
		for ( const auto side : { -1.0f, 1.0f } )
		{
			const auto contact_normal = plane_normal * side;
			if ( direction.dot( contact_normal ) >= -1e-6f ) continue;
			const auto distance = ( side * radius - signed_distance ) / normal_speed;
			if ( distance < 0.0f || distance > maximum_distance ) continue;
			const auto center = origin + direction * distance;
			const auto point = center - contact_normal * radius;
			if ( point_in_triangle( point, a, b, c, plane_normal ) )
				consider( distance, point, contact_normal );
		}
	}

	for ( const auto& edge : { std::pair{ a, b }, std::pair{ b, c }, std::pair{ c, a } } )
	{
		const auto distance = ray_capsule_distance( origin, direction,
			maximum_distance, edge.first, edge.second, radius );
		if ( !distance ) continue;
		const auto center = origin + direction * *distance;
		const auto point = closest_point_on_segment( center, edge.first, edge.second );
		consider( *distance, point, center - point );
	}
	return nearest;
}

}
