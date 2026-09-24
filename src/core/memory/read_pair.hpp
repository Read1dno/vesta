#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace foundation
{
template <class First, class Second, std::size_t Offset, class Read>
[[nodiscard]] std::pair<First, Second> read_pair(std::uintptr_t address, Read &&read)
{
	static_assert(std::is_trivially_copyable_v<First> && std::is_trivially_copyable_v<Second>);
	static_assert(Offset >= sizeof(First));
	std::pair<First, Second> result{};
	std::array<std::byte, Offset + sizeof(Second)> bytes;
	if (read(address, bytes.data(), bytes.size()))
	{
		std::memcpy(&result.first, bytes.data(), sizeof(First));
		std::memcpy(&result.second, bytes.data() + Offset, sizeof(Second));
	}
	else
	{
		// Preserve scalar-read behaviour at unreadable gaps/page boundaries.
		if (!read(address, &result.first, sizeof(First)))
			result.first = {};
		if (!read(address + Offset, &result.second, sizeof(Second)))
			result.second = {};
	}
	return result;
}
} // namespace foundation
