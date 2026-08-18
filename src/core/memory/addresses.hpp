#pragma once

namespace game {

	class address_catalog
	{
	public:
		bool initialize( );

		std::uintptr_t csgo_input{};
		std::uintptr_t entity_list{};
		std::uintptr_t local_player_controller{};
		std::uintptr_t global_vars{};
		std::uintptr_t view_matrix{};
		std::uintptr_t auto_accept{};
	};

}
