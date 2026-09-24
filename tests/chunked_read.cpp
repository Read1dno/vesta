#include <system/chunked_read.hpp>
#include <array>
#include <cstdio>
#include <limits>

int main()
{
	using record = std::array<std::uint64_t, 6>;
	constexpr std::uintptr_t base = 0x10000;
	constexpr std::size_t count = 8000000;
	std::size_t maximum{}, reads{}, consumed{};
	const auto read = [&](std::uintptr_t address, void *destination, std::size_t bytes) {
		maximum = std::max(maximum, bytes);
		++reads;
		auto *entries = static_cast<record *>(destination);
		const auto offset = (address - base) / sizeof(record);
		for (std::size_t i = 0; i < bytes / sizeof(record); ++i)
			entries[i][0] = offset + i;
		return true;
	};
	const auto consume = [&](std::span<const record> entries) {
		for (const auto &entry : entries)
			if (entry[0] != consumed++)
				return false;
		return true;
	};
	if (!platform::read_chunks<record>(base, count, read, consume) || consumed != count ||
	    maximum != 196608 || reads != 1954)
		return 1;
	std::printf("PASS streamed slots=%zu old_buffer_bytes=%zu new_buffer_bytes=%zu reads=%zu\n", count,
	            count * sizeof(record), maximum, reads);
	if (platform::read_chunks<record>(UINTPTR_MAX - 10, count, read, consume))
		return 2;
	std::stop_source stop;
	const auto cancel = [&](std::span<const record>) {
		stop.request_stop();
		return true;
	};
	reads = 0;
	if (platform::read_chunks<record>(base, count, read, cancel, stop.get_token()) || reads != 1)
		return 3;
	const auto failed_read = [](std::uintptr_t, void *, std::size_t) { return false; };
	if (platform::read_chunks<record>(base, count, failed_read, consume))
		return 4;
	std::puts("PASS chunk cancellation, partial final read, address overflow, read failure");
}
