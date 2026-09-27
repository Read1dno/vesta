#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

    inline void observe_phase(int tick, std::chrono::steady_clock::time_point observed_at,
        int& previous_tick, std::chrono::steady_clock::time_point& boundary,
        std::chrono::steady_clock::time_point& sampled_at) noexcept
    {
        const auto gap = observed_at - sampled_at;
        const bool observed_transition = sampled_at != std::chrono::steady_clock::time_point{}
            && gap >= std::chrono::steady_clock::duration::zero()
            && gap <= std::chrono::milliseconds(4);
        sampled_at = observed_at;
        if (tick <= 0) { previous_tick = -1; boundary = {}; return; }
        if (tick == previous_tick) {
            if (!observed_transition) boundary = {};
            return;
        }
        const bool forward = previous_tick > 0 && tick > previous_tick
            && static_cast<long long>(tick) - previous_tick <= 32;
        previous_tick = tick;
        boundary = forward && observed_transition ? observed_at : std::chrono::steady_clock::time_point{};
    }

    struct phase_observation {
        std::chrono::steady_clock::time_point transition_at{};
        int high_water{-1};
        bool rewinding{};
        std::uint64_t epoch{};
    };

    // A rollback changes the clock epoch, not ownership of an already submitted input.
    [[nodiscard]] inline bool observe_phase(int tick,
        std::chrono::steady_clock::time_point observed_at, int& previous_tick,
        std::chrono::steady_clock::time_point& boundary,
        std::chrono::steady_clock::time_point& sampled_at,
        phase_observation& observation) noexcept
    {
        using clock = std::chrono::steady_clock;
        constexpr auto maximum_gap = std::chrono::microseconds(4000);
        constexpr auto tick_interval = std::chrono::microseconds(15625);
        const auto gap = observed_at - sampled_at;
        const bool timely = sampled_at != clock::time_point{}
            && gap >= clock::duration::zero() && gap <= maximum_gap;
        sampled_at = observed_at;
        const auto invalidate = [&]() {
            boundary = {};
            ++observation.epoch;
            return true;
        };
        if (tick <= 0) {
            const bool had_clock = previous_tick > 0;
            previous_tick = -1;
            boundary = {};
            observation.transition_at = {};
            observation.high_water = -1;
            observation.rewinding = false;
            if (had_clock) ++observation.epoch;
            return had_clock;
        }
        if (previous_tick <= 0) {
            previous_tick = tick;
            boundary = {};
            observation.transition_at = observed_at;
            observation.high_water = tick;
            observation.rewinding = false;
            return false;
        }
        if (tick == previous_tick) {
            if (!timely) return invalidate();
            return false;
        }
        const auto delta = static_cast<long long>(tick) - previous_tick;
        const auto elapsed = observed_at - observation.transition_at;
        observation.high_water = std::max(observation.high_water, previous_tick);
        previous_tick = tick;
        observation.transition_at = observed_at;
        if (delta < 0) {
            observation.rewinding = true;
            return invalidate();
        }
        // Returning through already observed prediction ticks is not a new boundary.
        if (observation.rewinding && tick <= observation.high_water) {
            boundary = {};
            return false;
        }
        observation.rewinding = false;
        observation.high_water = std::max(observation.high_water, tick);
        bool plausible = delta <= 32 && elapsed >= clock::duration::zero();
        if (plausible && delta > 1) {
            plausible = elapsed >= tick_interval * (delta - 1) - maximum_gap;
        }
        if (!timely || !plausible) return invalidate();
        boundary = observed_at;
        return false;
    }

    class api_call_budget {
    public:
        template<class Rep, class Period>
        void observe(std::chrono::duration<Rep, Period> elapsed) noexcept
        {
            if (elapsed < decltype(elapsed)::zero()) return;
            const auto us = std::chrono::ceil<std::chrono::microseconds>(elapsed).count();
            samples_[next_] = static_cast<int>(std::clamp<long long>(us, 0, maximum_us));
            next_ = (next_ + 1) % samples_.size();
            count_ = std::min(count_ + 1, samples_.size());
        }

        [[nodiscard]] int reserve_us() const noexcept
        {
            if (count_ == 0) return bootstrap_us;
            const int recent_max = *std::max_element(samples_.begin(), samples_.begin() + count_);
            return std::clamp(recent_max + margin_us, bootstrap_us, maximum_us);
        }

        [[nodiscard]] std::chrono::microseconds reserve() const noexcept
        {
            return std::chrono::microseconds(reserve_us());
        }

    private:
        static constexpr int bootstrap_us = 64;
        static constexpr int margin_us = 32;
        static constexpr int maximum_us = 1000;
        std::array<int, 64> samples_{};
        std::size_t next_{}, count_{};
    };

    [[nodiscard]] inline bool current_phase(int observed_tick, int selected_tick,
        std::chrono::steady_clock::time_point boundary) noexcept
    {
        return selected_tick > 0 && observed_tick == selected_tick
            && boundary != std::chrono::steady_clock::time_point{};
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
    struct network_phase_window {
        int minimum_us{5000};
        int maximum_us{10500};
        int early_streak{};
        int late_streak{};

        [[nodiscard]] constexpr bool contains(std::chrono::microseconds age) const noexcept
        {
            return age.count() >= minimum_us && age.count() <= maximum_us;
        }

        // Feedback is the observed client shot tick, never an additional ray condition.
        constexpr void observe(int phase_us, int actual_minus_selected) noexcept
        {
            if (phase_us < 0 || phase_us > 14000 || actual_minus_selected < -1
                || actual_minus_selected > 1) return;
            if (actual_minus_selected == 0) { early_streak = late_streak = 0; return; }
            if (actual_minus_selected < 0) {
                late_streak = 0;
                if (++early_streak < 2) return;
                early_streak = 0;
                const int lower = std::max(minimum_us, phase_us + 500);
                if (lower + 1000 <= maximum_us) minimum_us = lower;
                else {
                    minimum_us = std::min(lower, 12000);
                    maximum_us = std::min(14000, minimum_us + 2000);
                }
            } else {
                early_streak = 0;
                if (++late_streak < 2) return;
                late_streak = 0;
                const int upper = std::min(maximum_us, phase_us - 500);
                if (upper >= minimum_us + 1000) maximum_us = upper;
                else {
                    maximum_us = std::max(upper, 2000);
                    minimum_us = std::max(0, maximum_us - 2000);
                }
            }
        }
    };

    // Network phase is gated at input delivery; both punch fractions remain in S+1.
    [[nodiscard]] constexpr bool fresh_network_decision(std::chrono::microseconds evaluation_age,
        std::chrono::microseconds terminal_age, std::chrono::microseconds prepared_phase,
        std::chrono::microseconds delivery_phase,
        const network_phase_window& window = {}) noexcept
    {
        return evaluation_age.count() >= 0 && evaluation_age.count() <= 4000
            && terminal_age.count() >= 0 && terminal_age.count() <= 1000
            && prepared_phase.count() >= 0 && prepared_phase <= delivery_phase
            && window.contains(delivery_phase);
    }

}
