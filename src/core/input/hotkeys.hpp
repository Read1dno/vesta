#pragma once

#include <cstdint>

namespace platform::windows {

	struct lifecycle_key_bindings
	{
		std::uint16_t menu{};
		std::uint16_t exit{};
	};

	[[nodiscard]] const lifecycle_key_bindings& lifecycle_keys( );
	[[nodiscard]] bool is_lifecycle_key( std::uint16_t virtual_key );

}
