#include <stdafx.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/trigger/seed_state.hpp>
#include <simulation/shot_state.hpp>

namespace features::aimbot {
	std::optional<aimbot_t::seed_shot_plan> aimbot_t::build_seed_plan(
		std::uintptr_t pawn, const foundation::vec3& view_angles,
		bool host_session, int seed_tick,
		std::chrono::steady_clock::time_point now, bool memoize )
	{
		( void )host_session;
		( void )view_angles;
		( void )now;
		( void )memoize;
		if ( seed_tick <= 0 ) return std::nullopt;

		foundation::vec3 base{};
		if ( !app::context().process.copy(
			pawn + SCHEMA( "C_BasePlayerPawn", "v_angle"_id ),
			&base, sizeof( base ) )
			|| !simulation::shot_model::finite( base )
			|| std::abs( base.x ) > 89.0f
			|| std::abs( base.y ) > 360.0f )
		{
			return std::nullopt;
		}

		const auto recoil = simulation::read_recoil_state( pawn );
		if ( !recoil ) return std::nullopt;
		simulation::seed_window::punch_sampler sampler{ *recoil };
		simulation::seed_window::window window{};
		if ( simulation::seed_window::resolve(
			sampler, base, seed_tick, window )
			!= simulation::seed_window::status::ok )
		{
			return std::nullopt;
		}

		seed_shot_plan plan{};
		plan.current = { seed_tick, window.angles.front(),
			window.angles.front(), 0.0f, window.angles.front() - base };
		plan.next = { seed_tick, window.angles.back(),
			window.angles.back(), simulation::seed_window::k_ray_fractions.back(),
			window.angles.back() - base };
		plan.phase = seed_prediction_phase::ambiguous;
		plan.source_tick = seed_tick;
		plan.source_angles = base;
		plan.prepared_punch = plan.current.punch;
		return plan;
	}
	bool aimbot_t::possible_seed_match(
		const seed_shot_plan& plan, const std::pair<bool, bool>& matches )
	{
		if ( plan.phase == seed_prediction_phase::current )
		{
			return matches.first;
		}
		if ( plan.phase == seed_prediction_phase::next )
		{
			return matches.second;
		}
		return matches.first || matches.second;
	}

	bool aimbot_t::safe_seed_match(
		const seed_shot_plan& plan, const std::pair<bool, bool>& matches )
	{
		if ( plan.phase == seed_prediction_phase::current )
		{
			return matches.first;
		}
		if ( plan.phase == seed_prediction_phase::next )
		{
			return matches.second;
		}
		return matches.first && matches.second;
	}

}
