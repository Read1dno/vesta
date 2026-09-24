#pragma once

namespace app
{

enum class startup_stage
{
	request_elevation,
	prepare_ui_access,
	run
};

[[nodiscard]] constexpr startup_stage select_startup_stage(bool elevated, bool ui_access)
{
	if (ui_access)
		return startup_stage::run;
	return elevated ? startup_stage::prepare_ui_access : startup_stage::request_elevation;
}

} // namespace app
