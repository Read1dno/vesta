#include <system/object_watch.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string_view>

namespace
{
using payload = std::array<std::uint8_t, 65536>;
std::uint64_t old_hash(const payload &bytes)
{
	std::uint64_t hash = 14695981039346656037ull;
	for (auto byte : bytes)
	{
		hash ^= byte;
		hash *= 1099511628211ull;
	}
	return hash;
}
} // namespace
int main(int argc, char **argv)
{
	payload data{};
	if (argc == 2)
	{
		const bool baseline = std::string_view(argv[1]) == "--baseline";
		foundation::object_watch watch(data);
		auto hash = old_hash(data);
		unsigned changes{};
		const auto begin = std::chrono::steady_clock::now();
		for (unsigned i = 0; i != 20000; ++i)
		{
			// Runtime-dependent mutation prevents hoisting either scan out of the loop.
			if ((i % 1000) == 0)
				++data[(i * 19) % data.size()];
			if (baseline)
			{
				const auto next = old_hash(data);
				changes += next != hash;
				hash = next;
			}
			else
				changes += watch.update(data);
		}
		const auto us =
		    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin)
		        .count();
		std::printf("%s bytes=65536 scans=20000 changes=%u elapsed_us=%lld\n",
		            baseline ? "BASELINE" : "MODIFIED", changes, static_cast<long long>(us));
		return changes == 20 ? 0 : 1;
	}
	foundation::object_watch watch(data);
	if (watch.update(data))
		return 1;
	for (auto position : {0u, 1u, 31u, 32u, 4095u, 65535u})
	{
		++data[position];
		if (!watch.update(data) || watch.update(data))
			return 2;
	}
	std::mt19937 random(314159);
	payload previous = data;
	for (unsigned i = 0; i != 10000; ++i)
	{
		data[random() % data.size()] = static_cast<std::uint8_t>(random());
		const bool expected = data != previous;
		if (watch.update(data) != expected || watch.update(data))
			return 3;
		previous = data;
	}
	std::puts("PASS object_watch unchanged, boundaries, repeated and 10000 randomized mutations");
}
