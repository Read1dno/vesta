#include "test_support.hpp"
#include <features/aimbot/aim_control.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace foundation {
float wrap_yaw(float yaw) noexcept { return std::remainder(yaw, 360.0f); }
}

int main()
{
	using foundation::vec3;
	using features::aimbot::detail::reconcile_control_angles;
	float old_yaw{};
	float new_yaw{};
	float old_backward{};
	float new_backward{};
	for (int step = 0; step < 100; ++step)
	{
		old_yaw += 0.20f;
		new_yaw += 0.20f;
		if (step % 4 != 3) continue;
		const auto observed = old_yaw - 0.60f;
		const auto old_before = old_yaw;
		old_yaw = observed;
		old_backward = std::max(old_backward, old_before - old_yaw);
		const auto new_before = new_yaw;
		new_yaw = reconcile_control_angles(
			vec3{0.0f, new_yaw, 0.0f},
			vec3{0.0f, new_yaw - 0.60f, 0.0f},
			vec3{}, vec3{0.0f, 0.20f, 0.0f},
			vec3{0.0f, 0.80f, 0.0f}, 0.004f).control.y;
		new_backward = std::max(new_backward, new_before - new_yaw);
	}
	VESTA_CHECK(features::aimbot::detail::selection_cost(1.0f, true)
		< features::aimbot::detail::selection_cost(0.91f, false));
	VESTA_CHECK(features::aimbot::detail::selection_cost(1.0f, true)
		> features::aimbot::detail::selection_cost(0.8f, false));
	VESTA_CHECK(old_backward > 0.59f);
	VESTA_CHECK(new_backward <= 0.0441f);
	VESTA_CHECK(new_backward < old_backward / 10.0f);
	const auto manual = reconcile_control_angles(
		vec3{0.0f, 10.0f, 0.0f}, vec3{0.0f, 15.0f, 0.0f},
		vec3{}, vec3{0.0f, 5.0f, 0.0f}, vec3{}, 0.004f);
	VESTA_CHECK(manual.control.y == 15.0f && manual.manual_override);
	const auto fast_self = reconcile_control_angles(
		vec3{0.0f, 10.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f},
		vec3{}, vec3{0.0f, 2.0f, 0.0f},
		vec3{0.0f, 10.0f, 0.0f}, 0.004f);
	VESTA_CHECK(fast_self.control.y > 9.9f && !fast_self.manual_override);
	VESTA_CHECK(fast_self.pending.y > 7.9f && fast_self.pending.y < 8.1f);
	const auto delayed_self = reconcile_control_angles(
		fast_self.control, vec3{0.0f, 4.0f, 0.0f},
		vec3{0.0f, 2.0f, 0.0f}, vec3{0.0f, 4.0f, 0.0f},
		fast_self.pending, 0.004f);
	VESTA_CHECK(!delayed_self.manual_override && delayed_self.control.y > 9.8f);
	const auto manual_opposite = reconcile_control_angles(
		vec3{0.0f, 10.0f, 0.0f}, vec3{0.0f, -3.0f, 0.0f},
		vec3{}, vec3{0.0f, -3.0f, 0.0f},
		vec3{0.0f, 5.0f, 0.0f}, 0.004f);
	VESTA_CHECK(manual_opposite.control.y == -3.0f && manual_opposite.manual_override);
	const auto small_manual = reconcile_control_angles(
		vec3{0.0f, 10.0f, 0.0f}, vec3{0.0f, 10.4f, 0.0f},
		vec3{}, vec3{0.0f, 0.4f, 0.0f}, vec3{}, 0.004f);
	VESTA_CHECK(small_manual.manual_override && std::abs(small_manual.control.y - 10.4f) < 0.0001f);
	const auto expired = reconcile_control_angles(
		vec3{0.0f, 10.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f},
		vec3{}, vec3{}, vec3{0.0f, 10.0f, 0.0f}, 0.004f, true);
	VESTA_CHECK(expired.control.y == 2.0f && expired.pending.length_sqr() == 0.0f);
	const auto wrapped = reconcile_control_angles(
		vec3{0.0f, 179.9f, 0.0f}, vec3{0.0f, -179.9f, 0.0f},
		vec3{0.0f, 179.8f, 0.0f}, vec3{0.0f, -179.9f, 0.0f},
		vec3{0.0f, 0.20f, 0.0f}, 0.004f);
	VESTA_CHECK(std::abs(foundation::wrap_yaw(wrapped.control.y - 179.9f)) < 0.0441f);
	VESTA_CHECK(!features::aimbot::detail::should_trace_candidate(6.0f, 5.0f, true));
	VESTA_CHECK(features::aimbot::detail::should_trace_candidate(4.0f, 5.0f, true));
	VESTA_CHECK(features::aimbot::detail::should_trace_candidate(6.0f, 5.0f, false));
	std::cout << "baseline_boundary_backstep_deg=" << old_backward
		<< " modified_boundary_backstep_deg=" << new_backward
		<< " manual_resync_deg=" << manual.control.y
		<< " fast_self_deg=" << fast_self.control.y
		<< " manual_opposite_deg=" << manual_opposite.control.y
		<< " small_manual_deg=" << small_manual.control.y << '\n';
}
