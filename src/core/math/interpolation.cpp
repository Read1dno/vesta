#include <stdafx.hpp>

namespace motion {

	void scalar_transition::start( float from, float to, float duration,
		curve shape ) noexcept
	{
		m_begin = from;
		m_end = to;
		m_value = from;
		m_duration = duration;
		m_elapsed = 0.0f;
		m_curve = shape;
		m_active = true;
	}

	void scalar_transition::update( ) noexcept
	{
		if ( !m_active ) return;
		m_elapsed += zdraw::get_delta_time( );
		if ( m_duration <= 0.0f || m_elapsed >= m_duration )
		{
			m_value = m_end;
			m_active = false;
			return;
		}
		const auto progress = remap( m_elapsed / m_duration );
		m_value = std::lerp( m_begin, m_end, progress );
	}

	void scalar_transition::reset( ) noexcept
	{
		m_value = m_begin;
		m_elapsed = 0.0f;
		m_active = false;
	}

	float scalar_transition::remap( float progress ) const noexcept
	{
		switch ( m_curve )
		{
		case curve::accelerate: return progress * progress;
		case curve::decelerate:
			return 1.0f - ( 1.0f - progress ) * ( 1.0f - progress );
		case curve::smooth:
			return progress < 0.5f ? 2.0f * progress * progress :
				1.0f - 2.0f * ( 1.0f - progress ) * ( 1.0f - progress );
		default: return progress;
		}
	}

	void damped_value::update( ) noexcept
	{
		const auto elapsed = zdraw::get_delta_time( );
		const auto acceleration = ( m_target - m_value ) * m_stiffness -
			m_velocity * m_damping;
		m_velocity += acceleration * elapsed;
		m_value += m_velocity * elapsed;
	}

	void damped_value::snap( float value ) noexcept
	{
		m_value = value;
		m_target = value;
		m_velocity = 0.0f;
	}

	bool damped_value::settled( ) const noexcept
	{
		return std::abs( m_target - m_value ) < 0.001f &&
			std::abs( m_velocity ) < 0.001f;
	}

}
