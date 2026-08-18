#include <stdafx.hpp>
#include <features/trigger/seed_trigger.hpp>

namespace features::trigger {

	bool seed_trigger_t::poll( )
	{
		m_controller.seed_tick( m_ballistics );
		return m_controller.seed_hot_path_requested( );
	}

	void seed_trigger_t::reset( )
	{
		m_controller.reset_seed( );
	}

}
