#include <system/snapshot_pool.hpp>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

struct snapshot
{
	inline static std::atomic<int> constructions{};
	snapshot()
	{
		++constructions;
	}
	std::vector<int> values;
};

int main()
{
	platform::snapshot_pool<snapshot> pool;
	std::atomic<std::shared_ptr<const snapshot>> current;
	std::vector<int> source(32768, 42);
	int buffer_growths{};
	for (int i = 0; i < 10000; ++i)
	{
#ifdef VESTA_BASELINE_TEST
		auto next = std::make_shared<snapshot>();
#else
		auto next = pool.acquire();
#endif
		if (next->values.capacity() < source.size())
			++buffer_growths;
		next->values = source;
		current.store(std::move(next));
	}
	std::printf("frames=10000 snapshot_allocations=%d vector_growths=%d\n", snapshot::constructions.load(),
	            buffer_growths);
#ifndef VESTA_BASELINE_TEST
	if (snapshot::constructions != 2 || buffer_growths != 2)
		return 1;
	const auto held = current.load();
	for (int i = 0; i < 100; ++i)
	{
		auto next = pool.acquire();
		next->values.assign(32768, i);
		current.store(std::move(next));
	}
	if (held->values.front() != 42)
		return 2;
	std::atomic<bool> failed{};
	std::jthread reader([&](std::stop_token stop) {
		while (!stop.stop_requested())
		{
			const auto frame = current.load();
			if (!frame || frame->values.empty())
				continue;
			const auto value = frame->values.front();
			for (const auto item : frame->values)
				if (item != value)
					failed = true;
		}
	});
	for (int i = 0; i < 2000; ++i)
	{
		auto next = pool.acquire();
		next->values.assign(32768, i);
		current.store(std::move(next));
	}
	reader.request_stop();
	reader.join();
	if (failed)
		return 3;
	current.store({});
	pool.clear();
	if (held->values.front() != 42)
		return 4;
	std::puts("PASS retained snapshots remain immutable, including concurrent reads");
#endif
}
