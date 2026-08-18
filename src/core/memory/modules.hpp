#pragma once

namespace platform::windows {
	class process_session;

	class module_catalog
	{
	public:
		[[nodiscard]] bool discover( const process_session& process );

		std::uintptr_t client{};
		std::uintptr_t engine{};
		std::uintptr_t input_system{};
		std::uintptr_t tier{};
		std::uintptr_t schema{};
		std::uintptr_t physics{};
		std::uintptr_t panorama{};
	};

}
