#include <stdafx.hpp>

namespace foundation {

	namespace {
		constexpr float epsilon{ 1.0e-8f };

		[[nodiscard]] float safe_root( float squared ) noexcept
		{
			return squared > epsilon ? std::sqrtf( squared ) : 0.0f;
		}
	}

	float vec2::length( ) const noexcept
	{
		return safe_root( length_sqr( ) );
	}

	vec2& vec2::normalize( ) noexcept
	{
		if ( const auto magnitude = length( ); magnitude > epsilon )
			*this /= magnitude;
		return *this;
	}

	vec2 vec2::normalized( ) const noexcept
	{
		auto copy = *this;
		return copy.normalize( );
	}

	float vec3::length( ) const noexcept
	{
		return safe_root( length_sqr( ) );
	}

	float vec3::length_2d( ) const noexcept
	{
		return safe_root( x * x + y * y );
	}

	vec3& vec3::normalize( ) noexcept
	{
		if ( const auto magnitude = length( ); magnitude > epsilon )
			*this /= magnitude;
		return *this;
	}

	vec3 vec3::normalized( ) const noexcept
	{
		auto copy = *this;
		return copy.normalize( );
	}

	float vec3::distance( const vec3& value ) const noexcept
	{
		return ( *this - value ).length( );
	}

	vec3 vec3::to_right_vector( ) const noexcept
	{
		auto right = cross( { 0.0f, 0.0f, 1.0f } );
		if ( right.length_sqr( ) < 1.0e-6f )
			right = cross( { 0.0f, -1.0f, 0.0f } );
		return right.normalize( );
	}

	void vec3::to_directions( vec3* forward, vec3* right,
		vec3* up ) const noexcept
	{
		const auto pitch_sine = std::sinf( to_radians( x ) );
		const auto pitch_cosine = std::cosf( to_radians( x ) );
		const auto yaw_sine = std::sinf( to_radians( y ) );
		const auto yaw_cosine = std::cosf( to_radians( y ) );
		const auto roll_sine = std::sinf( to_radians( z ) );
		const auto roll_cosine = std::cosf( to_radians( z ) );

		if ( forward )
			*forward = {
				pitch_cosine * yaw_cosine,
				pitch_cosine * yaw_sine,
				-pitch_sine };
		if ( right )
			*right = {
				-roll_sine * pitch_sine * yaw_cosine + roll_cosine * yaw_sine,
				-roll_sine * pitch_sine * yaw_sine - roll_cosine * yaw_cosine,
				-roll_sine * pitch_cosine };
		if ( up )
			*up = {
				roll_cosine * pitch_sine * yaw_cosine + roll_sine * yaw_sine,
				roll_cosine * pitch_sine * yaw_sine - roll_sine * yaw_cosine,
				roll_cosine * pitch_cosine };
	}

	rotation rotation::from_euler( const vec3& euler ) noexcept
	{
		const auto half_pitch = to_radians( euler.x ) * 0.5f;
		const auto half_yaw = to_radians( euler.y ) * 0.5f;
		const auto half_roll = to_radians( euler.z ) * 0.5f;
		const auto cp = std::cosf( half_pitch );
		const auto sp = std::sinf( half_pitch );
		const auto cy = std::cosf( half_yaw );
		const auto sy = std::sinf( half_yaw );
		const auto cr = std::cosf( half_roll );
		const auto sr = std::sinf( half_roll );
		return {
			sr * cp * cy - cr * sp * sy,
			cr * sp * cy + sr * cp * sy,
			cr * cp * sy - sr * sp * cy,
			cr * cp * cy + sr * sp * sy };
	}

	vec3 rotation::apply( const vec3& value ) const noexcept
	{
		return foundation::rotate( *this, value );
	}

	void basis_from_angles( const vec3& angles, vec3& forward,
		vec3& right, vec3& up ) noexcept
	{
		angles.to_directions( &forward, &right, &up );
	}

	void normalize_euler( vec3& angles ) noexcept
	{
		while ( angles.y > 180.0f ) angles.y -= 360.0f;
		while ( angles.y < -180.0f ) angles.y += 360.0f;
		while ( angles.x > 89.0f ) angles.x -= 180.0f;
		while ( angles.x < -89.0f ) angles.x += 180.0f;
		angles.z = 0.0f;
	}

	vec3 direction_to_angles( const vec3& direction ) noexcept
	{
		return {
			to_degrees( std::atan2f( -direction.z, direction.length_2d( ) ) ),
			to_degrees( std::atan2f( direction.y, direction.x ) ),
			0.0f };
	}

	vec3 look_at_angles( const vec3& origin, const vec3& target ) noexcept
	{
		return direction_to_angles( target - origin );
	}

	float angular_distance( const vec3& view_angles,
		const vec3& origin, const vec3& target ) noexcept
	{
		const auto target_angles = look_at_angles( origin, target );
		return vec2{
			view_angles.x - target_angles.x,
			wrap_yaw( view_angles.y - target_angles.y ) }.length( );
	}

	float wrap_yaw( float yaw ) noexcept
	{
		return std::remainder( yaw, 360.0f );
	}

	vec3 rotate( const rotation& orientation, const vec3& value ) noexcept
	{
		const vec3 axis{ orientation.x, orientation.y, orientation.z };
		const auto twice_cross = axis.cross( value ) * 2.0f;
		return value + twice_cross * orientation.w + axis.cross( twice_cross );
	}

}
