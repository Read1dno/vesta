#include <render/index_run.hpp>
#include <vector>
#include <random>
#include <cstdio>
#include <limits>

int main()
{
	std::mt19937 random(71273);
	for (unsigned trial = 0; trial < 1000; ++trial)
	{
		std::vector<unsigned> expected, actual;
		render::index_run run;
		const auto emit = [&](unsigned first, unsigned count) {
			for (unsigned j = 0; j < count; ++j)
				actual.push_back(first + j);
		};
		unsigned offset{};
		for (unsigned i = 0; i < 100; ++i)
		{
			const unsigned count = random() % 20;
			if ((random() % 3) != 0)
			{
				for (unsigned j = 0; j < count; ++j)
					expected.push_back(offset + j);
				run.append(offset, count, emit);
			}
			offset += count;
		}
		run.flush(emit);
		run.flush(emit);
		if (actual != expected)
			return 1;
	}
	render::index_run run;
	unsigned calls{}, indices{};
	const auto emit = [&](unsigned, unsigned count) {
		++calls;
		indices += count;
	};
	for (unsigned i = 0; i < 10000; ++i)
		run.append(i * 3, 3, emit);
	run.flush(emit);
	if (calls != 1 || indices != 30000)
		return 2;
	std::vector<std::pair<unsigned, unsigned>> ranges;
	const auto collect = [&](unsigned first, unsigned count) { ranges.emplace_back(first, count); };
	const auto max = std::numeric_limits<unsigned>::max();
	run.append(0, max, collect);
	run.append(max, 1, collect);
	run.append(0, 3, collect);
	run.append(0, 3, collect);
	run.flush(collect);
	if (ranges != std::vector<std::pair<unsigned, unsigned>>{{0, max}, {max, 1}, {0, 3}, {0, 3}})
		return 3;
	std::puts("PASS index_run 1000 randomized masks, gaps, empty, overflow, overlap; contiguous draws 10000 "
	          "-> 1, indices 30000 -> 30000");
}
