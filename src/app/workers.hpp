#pragma once

#include <memory>
#include <string>

namespace app::workers {

	void game( );
	void pose_sampler( );
	void movement( );
	void combat( );
	void nade_helper( );
	void seed_trigger( );
	void watchdog( );

	[[nodiscard]] std::shared_ptr<const std::string> current_map( );

}
