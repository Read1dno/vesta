#pragma once

#include <core/input/input.hpp>
#include <core/memory/addresses.hpp>
#include <core/memory/modules.hpp>
#include <core/memory/process.hpp>
#include <render/menu/menu.hpp>
#include <render/overlay/overlay.hpp>
#include <system/diagnostics.hpp>

namespace app {

	struct context_t
	{
		platform::windows::diagnostic_sink diagnostics{};
		platform::windows::input_gateway input{};
		platform::windows::process_session process{};
		platform::windows::module_catalog modules{};
		game::address_catalog addresses{};
		overlay_t overlay{};
		menu_t menu{};
	};

	inline context_t& context( )
	{
		static context_t instance{};
		return instance;
	}

}
