#include <stdafx.hpp>

namespace foundation {

	frame_limiter::frame_limiter( std::uint32_t frames_per_second ) noexcept
	{
		set_rate( frames_per_second );
	}

	void frame_limiter::set_rate( std::uint32_t frames_per_second ) noexcept
	{
		if ( frames_per_second == m_rate )
		{
			return;
		}
		m_rate = frames_per_second;
		m_period = frames_per_second == 0 ? clock::duration::zero( ) :
			std::chrono::duration_cast<clock::duration>(
				std::chrono::seconds{ 1 } ) / frames_per_second;
		m_deadline = clock::now( );
	}

	void frame_limiter::wait( ) noexcept
	{
		if ( m_period <= clock::duration::zero( ) )
		{
			m_deadline = clock::now( );
			return;
		}

		m_deadline += m_period;
		const auto now = clock::now( );
		if ( now >= m_deadline )
		{
			m_deadline = now;
			return;
		}

		constexpr auto spin_window = std::chrono::microseconds{ 50 };
		if ( m_deadline - now > spin_window )
			std::this_thread::sleep_until( m_deadline - spin_window );
		while ( clock::now( ) < m_deadline )
			YieldProcessor( );
	}

}
