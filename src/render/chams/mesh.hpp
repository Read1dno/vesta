#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chams {

	struct bone_matrix
	{
		float m[ 3 ][ 4 ]{};

		[[nodiscard]] static bone_matrix identity( )
		{
			bone_matrix out{};
			out.m[ 0 ][ 0 ] = out.m[ 1 ][ 1 ] = out.m[ 2 ][ 2 ] = 1.0f;
			return out;
		}

		[[nodiscard]] bone_matrix operator*( const bone_matrix& other ) const
		{
			bone_matrix out{};
			for ( int row = 0; row < 3; ++row )
			{
				for ( int col = 0; col < 3; ++col )
				{
					out.m[ row ][ col ] = m[ row ][ 0 ] * other.m[ 0 ][ col ]
						+ m[ row ][ 1 ] * other.m[ 1 ][ col ]
						+ m[ row ][ 2 ] * other.m[ 2 ][ col ];
				}

				out.m[ row ][ 3 ] = m[ row ][ 0 ] * other.m[ 0 ][ 3 ]
					+ m[ row ][ 1 ] * other.m[ 1 ][ 3 ]
					+ m[ row ][ 2 ] * other.m[ 2 ][ 3 ]
					+ m[ row ][ 3 ];
			}
			return out;
		}

		[[nodiscard]] bone_matrix inverse_rigid( ) const
		{
			const auto scale_sqr = m[ 0 ][ 0 ] * m[ 0 ][ 0 ] + m[ 1 ][ 0 ] * m[ 1 ][ 0 ] + m[ 2 ][ 0 ] * m[ 2 ][ 0 ];
			const auto inv_scale_sqr = scale_sqr > 1e-12f ? 1.0f / scale_sqr : 0.0f;

			bone_matrix out{};
			for ( int row = 0; row < 3; ++row )
			{
				for ( int col = 0; col < 3; ++col )
				{
					out.m[ row ][ col ] = m[ col ][ row ] * inv_scale_sqr;
				}
			}

			for ( int row = 0; row < 3; ++row )
			{
				out.m[ row ][ 3 ] = -( out.m[ row ][ 0 ] * m[ 0 ][ 3 ] + out.m[ row ][ 1 ] * m[ 1 ][ 3 ] + out.m[ row ][ 2 ] * m[ 2 ][ 3 ] );
			}

			return out;
		}
	};

	struct bone_info
	{
		std::string name{};
		int parent{ -1 };
		bone_matrix inverse_bind{};
	};

	struct skinned_vertex
	{
		float position[ 3 ]{};
		float normal[ 3 ]{};
		float tangent[ 4 ]{};
		float uv[ 2 ]{};
		std::uint8_t bone_indices[ 4 ]{};
		std::uint8_t bone_weights[ 4 ]{};
	};

	struct draw_range
	{
		std::uint32_t index_offset{};
		std::uint32_t index_count{};
		std::string material{};
	};

	struct skinned_mesh
	{
		std::vector<skinned_vertex> vertices{};
		std::vector<std::uint32_t> indices{};
		std::vector<draw_range> draw_calls{};
		std::vector<bone_info> bones{};
		bool valid{ false };
	};

}
