#pragma once

#include <features/aimbot/aimbot.hpp>
#include <simulation/ballistics.hpp>

namespace features::trigger {

	class seed_trigger_t
	{
	public:
		[[nodiscard]] bool poll( );
		void reset( );
		[[nodiscard]] int observed_tick( ) const noexcept
		{
			return m_controller.seed_observed_tick( );
		}
		[[nodiscard]] std::chrono::steady_clock::time_point
			observed_at( ) const noexcept
		{
			return m_controller.seed_tick_observed_at( );
		}
		[[nodiscard]] bool input_pending( ) const noexcept
		{
			return m_controller.seed_input_pending( );
		}
		void sync_phase( int tick,
			std::chrono::steady_clock::time_point observed_at ) noexcept
		{
			m_controller.sync_seed_phase( tick, observed_at );
		}

	private:
		features::aimbot::aimbot_t m_controller{};
		simulation::ballistics_t m_ballistics{};
	};

	inline seed_trigger_t& seed_trigger( )
	{
		static seed_trigger_t instance{};
		return instance;
	}

}
