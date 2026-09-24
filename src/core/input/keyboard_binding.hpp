#pragma once
#include <core/input/bindings.hpp>
#include <span>

namespace game
{

[[nodiscard]] inline input_binding keyboard_binding(std::span<const input_binding> candidates,
                                                    std::uint16_t preferred = 0)
{
	const input_binding *fallback{};
	const input_binding *ordinary{};
	for (const auto &candidate : candidates)
	{
		if (candidate.device != input_device::keyboard || candidate.virtual_key == 0)
			continue;
		if (preferred && candidate.virtual_key == preferred)
			return candidate;
		if (!fallback)
			fallback = &candidate;
		if (!ordinary && candidate.virtual_key != 0x86 && candidate.virtual_key != 0x87)
			ordinary = &candidate; // F23/F24 are reserved auxiliary bindings.
	}
	return ordinary ? *ordinary : fallback ? *fallback : input_binding{};
}

} // namespace game
