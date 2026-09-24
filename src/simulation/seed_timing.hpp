#pragma once

#include <cmath>
#include <chrono>
#include <limits>

namespace simulation::seed_timing {
	[[nodiscard]] constexpr int select_tick( bool host_session, int host_tick,
		int simulation_tick, int tick_base ) noexcept
	{
		if ( host_session && host_tick > 0 ) return host_tick;
		if ( !host_session && simulation_tick >= 0 && simulation_tick < std::numeric_limits<int>::max() ) return simulation_tick + 1;
		return tick_base >= 0 && tick_base < std::numeric_limits<int>::max() ? tick_base + 1 : -1;
	}

	[[nodiscard]] inline bool primary_ready( int next_tick, float next_ratio,
		int current_tick ) noexcept
	{
		return current_tick > 0 && std::isfinite( next_ratio )
			&& ( next_tick < current_tick
				|| ( next_tick == current_tick && next_ratio <= 0.001f ) );
	}

    [[nodiscard]] constexpr int phase(std::chrono::microseconds age) noexcept
    {
        if (age.count() < 0 || age.count() >= 14625) return -1;
        if (age.count() <= 6000) return 0;
        return age.count() >= 11000 ? 2 : 1;
    }
    [[nodiscard]] constexpr bool fresh_decision(std::chrono::microseconds evaluation_age,
        std::chrono::microseconds terminal_age, std::chrono::microseconds prepared_phase,
        std::chrono::microseconds delivery_phase) noexcept
    {
        return evaluation_age.count() >= 0 && evaluation_age.count() <= 4000
            && terminal_age.count() >= 0 && terminal_age.count() <= 1000
            && phase(prepared_phase) >= 0 && phase(prepared_phase) == phase(delivery_phase);
    }
}
