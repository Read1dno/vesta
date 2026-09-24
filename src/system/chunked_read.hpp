#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <stop_token>
#include <vector>

namespace platform
{

template <class T, class Read, class Consume>
[[nodiscard]] bool read_chunks(std::uintptr_t address, std::size_t count, Read &&read, Consume &&consume,
                               std::stop_token stop = {})
{
	constexpr std::size_t chunk_count = 4096;
	if (!address || !count || count > (UINTPTR_MAX - address) / sizeof(T) || stop.stop_requested())
		return false;
	std::vector<T> buffer(std::min(count, chunk_count));
	for (std::size_t offset = 0; offset < count; offset += buffer.size())
	{
		if (stop.stop_requested())
			return false;
		const auto length = std::min(buffer.size(), count - offset);
		if (!read(address + offset * sizeof(T), buffer.data(), length * sizeof(T)) ||
		    !consume(std::span<const T>(buffer.data(), length)))
			return false;
	}
	return !stop.stop_requested();
}

} // namespace platform
