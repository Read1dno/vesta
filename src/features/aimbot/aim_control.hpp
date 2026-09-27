#pragma once

#include <numbers>
#include <core/math/vector.hpp>

#include <algorithm>
#include <cmath>

namespace features::aimbot::detail {

// A new command can still describe an older camera state; only bounded
// feedback may correct the faster worker's predicted camera position.
[[nodiscard]] inline float selection_cost(const float fov, const bool held) noexcept
{
	return fov - (held ? 0.12f : 0.0f);
}

[[nodiscard]] constexpr bool should_trace_candidate(
	const float fov, const float limit, const bool enforce_fov ) noexcept
{
	return !enforce_fov || fov <= limit;
}

struct reconciliation_result
{
	foundation::vec3 control{};
	foundation::vec3 pending{};
	bool manual_override{};
};

[[nodiscard]] inline reconciliation_result reconcile_control_angles(
	const foundation::vec3& predicted, const foundation::vec3& observed,
	const foundation::vec3& previous_camera, const foundation::vec3& camera,
	const foundation::vec3& pending_input, const float worker_dt,
	const bool pending_expired = false ) noexcept
{
	const auto pitch_error = observed.x - predicted.x;
	const auto yaw_error = foundation::wrap_yaw( observed.y - predicted.y );
	if ( !std::isfinite( pitch_error ) || !std::isfinite( yaw_error ) )
		return { predicted, pending_input, false };
	if ( pending_expired ) return { observed, {}, false };

	const auto camera_pitch = camera.x - previous_camera.x;
	const auto camera_yaw = foundation::wrap_yaw( camera.y - previous_camera.y );
	if ( !std::isfinite( camera_pitch ) || !std::isfinite( camera_yaw ) )
		return { predicted, pending_input, false };
	const auto pending_length_sqr = pending_input.x * pending_input.x
		+ pending_input.y * pending_input.y;
	const auto consumed = pending_length_sqr > 0.0001f
		? std::clamp( ( camera_pitch * pending_input.x
			+ camera_yaw * pending_input.y ) / pending_length_sqr, 0.0f, 1.0f )
		: 0.0f;
	const auto unexplained = std::hypot(
		camera_pitch - pending_input.x * consumed,
		camera_yaw - pending_input.y * consumed );
	if ( std::isfinite( unexplained ) && unexplained >= 0.15f )
		return { observed, {}, true };

	const auto gain = std::clamp( worker_dt / 0.08f, 0.0f, 0.10f );
	const auto correction_limit = 0.044f;
	return { {
		predicted.x + std::clamp( pitch_error * gain,
			-correction_limit, correction_limit ),
		foundation::wrap_yaw( predicted.y + std::clamp( yaw_error * gain,
			-correction_limit, correction_limit ) ),
		predicted.z }, pending_input * ( 1.0f - consumed ), false };
}

} // namespace features::aimbot::detail
