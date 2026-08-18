#include <stdafx.hpp>

namespace game {
	namespace {
		[[nodiscard]] std::uintptr_t view_render_instance( )
		{

			static const auto instance =
				app::context().process.locate_vtable_object( app::context().modules.client, "CViewRender" );
			return instance;
		}
	}

	bool camera_state::sample( foundation::vec3& origin, foundation::vec3& angles ) const
	{
		const auto view_render = view_render_instance( );
		if ( !view_render )
		{
			return false;
		}

		const auto view_state = view_render + 0x10;
		struct camera_sample
		{
			foundation::vec3 origin{};
			foundation::vec3 angles{};
		} sample{};
		static_assert( sizeof( camera_sample ) == 0x18 );

		if ( app::context().process.copy( view_state, &sample, sizeof( sample ) ) )
		{
			origin = sample.origin;
			angles = sample.angles;
		}
		else
		{

			origin = app::context().process.load<foundation::vec3>( view_state );
			angles = app::context().process.load<foundation::vec3>( view_state + 0xc );
		}
		return std::isfinite( origin.x ) && std::isfinite( origin.y ) && std::isfinite( origin.z )
			&& std::isfinite( angles.x ) && std::isfinite( angles.y ) && std::isfinite( angles.z );
	}

	void camera_state::update( )
	{
		const auto raw_matrix = app::context().process.load<foundation::matrix4>( app::context().addresses.view_matrix );
		auto matrix = raw_matrix;
		const auto now = std::chrono::steady_clock::now( );
		const float horizon = this->m_presentation_horizon_seconds.load(
			std::memory_order_relaxed );
		const auto row_norm = []( const foundation::matrix4& value, const int row )
		{
			return std::sqrt(
				value[ row ][ 0 ] * value[ row ][ 0 ]
				+ value[ row ][ 1 ] * value[ row ][ 1 ]
				+ value[ row ][ 2 ] * value[ row ][ 2 ] );
		};

		const matrix_sample* reference{};
		for ( std::size_t offset = 1; offset <= this->m_matrix_history_count; ++offset )
		{
			const auto index = ( this->m_matrix_history_next
				+ this->m_matrix_history.size( ) - offset ) % this->m_matrix_history.size( );
			const auto age = std::chrono::duration<float>(
				now - this->m_matrix_history[ index ].time ).count( );
			if ( age >= 0.004f )
			{
				reference = &this->m_matrix_history[ index ];
				break;
			}
		}

		if ( reference && horizon > 0.0f )
		{
			const float age = std::chrono::duration<float>( now - reference->time ).count( );
			const float current_x = row_norm( raw_matrix, 0 );
			const float current_y = row_norm( raw_matrix, 1 );
			const float previous_x = row_norm( reference->matrix, 0 );
			const float previous_y = row_norm( reference->matrix, 1 );
			const bool stable_projection = age <= 0.030f
				&& current_x > 0.0001f && current_y > 0.0001f
				&& previous_x > 0.0001f && previous_y > 0.0001f
				&& std::abs( current_x / previous_x - 1.0f ) < 0.06f
				&& std::abs( current_y / previous_y - 1.0f ) < 0.06f;
			if ( stable_projection )
			{
				const float factor = std::clamp( horizon / age, 0.0f, 1.35f );
				for ( int row = 0; row < 4; ++row )
				{
					for ( int column = 0; column < 4; ++column )
					{
						matrix[ row ][ column ] = raw_matrix[ row ][ column ]
							+ ( raw_matrix[ row ][ column ]
								- reference->matrix[ row ][ column ] ) * factor;
					}
				}
			}
		}

		this->m_matrix_history[ this->m_matrix_history_next ] = { raw_matrix, now };
		this->m_matrix_history_next = ( this->m_matrix_history_next + 1 )
			% this->m_matrix_history.size( );
		this->m_matrix_history_count = std::min(
			this->m_matrix_history_count + 1, this->m_matrix_history.size( ) );
		const auto [ display_width, display_height ] = zdraw::get_display_size( );
		this->m_frame_matrix = matrix;
		this->m_frame_width = static_cast<float>( display_width );
		this->m_frame_height = static_cast<float>( display_height );
		{
			std::unique_lock lock( this->m_state_mutex );
			this->m_matrix = matrix;
		}
		const auto view_render = view_render_instance( );
		if ( !view_render )
		{
			return;
		}

		const auto view = view_render + 0x10;
		if ( view )
		{
			struct camera_state
			{
				foundation::vec3 origin{};
				foundation::vec3 angles{};
				float fov{};
			} state{};
			static_assert( sizeof( camera_state ) == 0x1c );

			if ( !app::context().process.copy( view, &state, sizeof( state ) ) )
			{
				state.origin = app::context().process.load<foundation::vec3>( view + 0x0 );
				state.angles = app::context().process.load<foundation::vec3>( view + 0xc );
				state.fov = app::context().process.load<float>( view + 0x18 );
			}
			std::unique_lock lock( this->m_state_mutex );
			this->m_origin = state.origin;
			this->m_angles = state.angles;
			this->m_fov = state.fov;
		}
		else
		{
			std::unique_lock lock( this->m_state_mutex );
			this->m_origin = { k_invalid, k_invalid, k_invalid };
			this->m_angles = { k_invalid, k_invalid, k_invalid };
			this->m_fov = k_invalid;
		}
	}

	foundation::vec2 camera_state::project( const foundation::vec3& world_pos )
	{

		const auto& m = this->m_frame_matrix;

		if ( m[ 3 ][ 3 ] == 0.0f )
		{
			return { static_cast<float>( this->k_invalid ), static_cast<float>( this->k_invalid ) };
		}

		const auto w = m[ 3 ][ 0 ] * world_pos.x + m[ 3 ][ 1 ] * world_pos.y + m[ 3 ][ 2 ] * world_pos.z + m[ 3 ][ 3 ];

		constexpr auto k_near_plane{ 7.0f };
		if ( !( w >= k_near_plane ) )
		{
			return { static_cast<float>( this->k_invalid ), static_cast<float>( this->k_invalid ) };
		}

		const auto x = m[ 0 ][ 0 ] * world_pos.x + m[ 0 ][ 1 ] * world_pos.y + m[ 0 ][ 2 ] * world_pos.z + m[ 0 ][ 3 ];
		const auto y = m[ 1 ][ 0 ] * world_pos.x + m[ 1 ][ 1 ] * world_pos.y + m[ 1 ][ 2 ] * world_pos.z + m[ 1 ][ 3 ];

		const auto inv_w = 1.0f / w;
		const auto ndc_x = x * inv_w;
		const auto ndc_y = y * inv_w;

		constexpr auto k_ndc_limit{ 64.0f };
		if ( !std::isfinite( ndc_x ) || !std::isfinite( ndc_y )
			|| std::abs( ndc_x ) > k_ndc_limit || std::abs( ndc_y ) > k_ndc_limit )
		{
			return { static_cast<float>( this->k_invalid ), static_cast<float>( this->k_invalid ) };
		}

		return
		{
			this->m_frame_width * 0.5f * ( 1.0f + ndc_x ),
			this->m_frame_height * 0.5f * ( 1.0f - ndc_y )
		};
	}

	bool camera_state::sample_presentation(
		presentation_camera_sample& sample ) const
	{
		if ( !app::context().process.copy( app::context().addresses.view_matrix,
			&sample.matrix, sizeof( sample.matrix ) ) )
		{
			return false;
		}

		const auto view_render = view_render_instance( );
		if ( !view_render )
		{
			return false;
		}
		struct view_sample
		{
			foundation::vec3 origin{};
			foundation::vec3 angles{};
			float fov{};
		} view{};
		static_assert( sizeof( view_sample ) == 0x1c );
		if ( !app::context().process.copy(
			view_render + 0x10, &view, sizeof( view ) ) )
		{
			return false;
		}

		sample.origin = view.origin;
		sample.angles = view.angles;
		sample.fov = view.fov;
		return std::isfinite( sample.origin.x )
			&& std::isfinite( sample.origin.y )
			&& std::isfinite( sample.origin.z )
			&& std::isfinite( sample.angles.x )
			&& std::isfinite( sample.angles.y )
			&& std::isfinite( sample.angles.z )
			&& std::isfinite( sample.fov );
	}

	void camera_state::begin_presentation_frame(
		const presentation_camera_sample& sample,
		const std::uint32_t width, const std::uint32_t height )
	{
		this->m_frame_matrix = sample.matrix;
		this->m_frame_width = static_cast<float>( width );
		this->m_frame_height = static_cast<float>( height );
		std::unique_lock lock( this->m_state_mutex );
		this->m_matrix = sample.matrix;
		this->m_origin = sample.origin;
		this->m_angles = sample.angles;
		this->m_fov = sample.fov;
	}

}
