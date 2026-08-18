#pragma once

namespace foundation {

	struct vec2
	{
		float x{};
		float y{};

		constexpr vec2( ) noexcept = default;
		constexpr vec2( float x_value, float y_value ) noexcept
			: x{ x_value }, y{ y_value } {}

		friend constexpr vec2 operator+( vec2 left, const vec2& right ) noexcept
		{
			left += right;
			return left;
		}
		friend constexpr vec2 operator-( vec2 left, const vec2& right ) noexcept
		{
			left -= right;
			return left;
		}
		friend constexpr vec2 operator*( vec2 value, float scalar ) noexcept
		{
			value *= scalar;
			return value;
		}
		friend constexpr vec2 operator/( vec2 value, float scalar ) noexcept
		{
			value /= scalar;
			return value;
		}
		[[nodiscard]] constexpr vec2 operator-( ) const noexcept { return { -x, -y }; }

		constexpr vec2& operator+=( const vec2& value ) noexcept
		{
			x += value.x;
			y += value.y;
			return *this;
		}
		constexpr vec2& operator-=( const vec2& value ) noexcept
		{
			x -= value.x;
			y -= value.y;
			return *this;
		}
		constexpr vec2& operator*=( float scalar ) noexcept
		{
			x *= scalar;
			y *= scalar;
			return *this;
		}
		constexpr vec2& operator/=( float scalar ) noexcept
		{
			x /= scalar;
			y /= scalar;
			return *this;
		}
		[[nodiscard]] constexpr bool operator==( const vec2& ) const noexcept = default;

		[[nodiscard]] constexpr float dot( const vec2& value ) const noexcept
		{
			return x * value.x + y * value.y;
		}
		[[nodiscard]] constexpr float length_sqr( ) const noexcept { return dot( *this ); }
		[[nodiscard]] float length( ) const noexcept;
		vec2& normalize( ) noexcept;
		[[nodiscard]] vec2 normalized( ) const noexcept;
	};

	struct vec3
	{
		float x{};
		float y{};
		float z{};

		constexpr vec3( ) noexcept = default;
		constexpr vec3( float x_value, float y_value, float z_value ) noexcept
			: x{ x_value }, y{ y_value }, z{ z_value } {}

		friend constexpr vec3 operator+( vec3 left, const vec3& right ) noexcept
		{
			left += right;
			return left;
		}
		friend constexpr vec3 operator-( vec3 left, const vec3& right ) noexcept
		{
			left -= right;
			return left;
		}
		friend constexpr vec3 operator*( vec3 value, float scalar ) noexcept
		{
			value *= scalar;
			return value;
		}
		friend constexpr vec3 operator/( vec3 value, float scalar ) noexcept
		{
			value /= scalar;
			return value;
		}
		[[nodiscard]] constexpr vec3 operator-( ) const noexcept
		{
			return { -x, -y, -z };
		}

		constexpr vec3& operator+=( const vec3& value ) noexcept
		{
			x += value.x;
			y += value.y;
			z += value.z;
			return *this;
		}
		constexpr vec3& operator-=( const vec3& value ) noexcept
		{
			x -= value.x;
			y -= value.y;
			z -= value.z;
			return *this;
		}
		constexpr vec3& operator*=( float scalar ) noexcept
		{
			x *= scalar;
			y *= scalar;
			z *= scalar;
			return *this;
		}
		constexpr vec3& operator/=( float scalar ) noexcept
		{
			x /= scalar;
			y /= scalar;
			z /= scalar;
			return *this;
		}
		[[nodiscard]] constexpr bool operator==( const vec3& ) const noexcept = default;

		[[nodiscard]] constexpr float dot( const vec3& value ) const noexcept
		{
			return x * value.x + y * value.y + z * value.z;
		}
		[[nodiscard]] constexpr vec3 cross( const vec3& value ) const noexcept
		{
			return {
				y * value.z - z * value.y,
				z * value.x - x * value.z,
				x * value.y - y * value.x };
		}
		[[nodiscard]] constexpr float length_sqr( ) const noexcept { return dot( *this ); }
		[[nodiscard]] float length( ) const noexcept;
		[[nodiscard]] float length_2d( ) const noexcept;
		vec3& normalize( ) noexcept;
		[[nodiscard]] vec3 normalized( ) const noexcept;
		[[nodiscard]] float distance( const vec3& value ) const noexcept;
		[[nodiscard]] constexpr float distance_sqr( const vec3& value ) const noexcept
		{
			return ( *this - value ).length_sqr( );
		}
		[[nodiscard]] vec3 to_right_vector( ) const noexcept;
		void to_directions( vec3* forward, vec3* right, vec3* up ) const noexcept;
	};

	struct rotation
	{
		float x{};
		float y{};
		float z{};
		float w{ 1.0f };

		constexpr rotation( ) noexcept = default;
		constexpr rotation( float x_value, float y_value,
			float z_value, float w_value ) noexcept
			: x{ x_value }, y{ y_value }, z{ z_value }, w{ w_value } {}

		[[nodiscard]] static rotation from_euler( const vec3& euler ) noexcept;
		[[nodiscard]] vec3 apply( const vec3& value ) const noexcept;
	};

	struct affine3
	{
		float values[ 3 ][ 4 ]{};
		[[nodiscard]] constexpr const float* operator[]( int row ) const noexcept
		{
			return values[ row ];
		}
		[[nodiscard]] constexpr float* operator[]( int row ) noexcept
		{
			return values[ row ];
		}
	};

	struct matrix4
	{
		float values[ 4 ][ 4 ]{};
		[[nodiscard]] constexpr const float* operator[]( int row ) const noexcept
		{
			return values[ row ];
		}
		[[nodiscard]] constexpr float* operator[]( int row ) noexcept
		{
			return values[ row ];
		}
	};

	void basis_from_angles( const vec3& angles, vec3& forward,
		vec3& right, vec3& up ) noexcept;
	void normalize_euler( vec3& angles ) noexcept;
	[[nodiscard]] vec3 direction_to_angles( const vec3& direction ) noexcept;
	[[nodiscard]] vec3 look_at_angles( const vec3& origin,
		const vec3& target ) noexcept;
	[[nodiscard]] float angular_distance( const vec3& view_angles,
		const vec3& origin, const vec3& target ) noexcept;
	[[nodiscard]] constexpr float to_radians( float degrees ) noexcept
	{
		return degrees * ( std::numbers::pi_v<float> / 180.0f );
	}
	[[nodiscard]] constexpr float to_degrees( float radians ) noexcept
	{
		return radians * ( 180.0f / std::numbers::pi_v<float> );
	}
	[[nodiscard]] float wrap_yaw( float yaw ) noexcept;
	[[nodiscard]] vec3 rotate( const rotation& orientation,
		const vec3& value ) noexcept;

}
