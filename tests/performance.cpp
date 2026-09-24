#include <system/performance.hpp>
#include <external/json.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

int main()
{
	namespace perf = platform::performance;
	std::filesystem::remove(VESTA_PERF_TEST_OUTPUT);
	perf::flush_if_due(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(25));
	{
		perf::scope parent(perf::zone::render_frame);
		{
			perf::scope child(perf::zone::chams);
			volatile unsigned value{};
			for (unsigned i = 0; i != 1000000; ++i)
				value = value + i;
			(void)value;
		}
		for (unsigned i = 0; i != 190; ++i)
			perf::record_present();
	}
	perf::flush_if_due(true);
	bool process{}, parent{}, child{};
	std::uint64_t parent_ns{}, child_ns{};
	std::ifstream in(VESTA_PERF_TEST_OUTPUT);
	for (std::string line; std::getline(in, line);)
	{
		const auto record = nlohmann::json::parse(line);
		if (record["type"] == "process")
		{
			if (record["presented"] != 190 || record["logical_cpus"].get<unsigned>() == 0 ||
			    record["wall_ns"].get<std::uint64_t>() == 0 || record["cpu_percent"].get<double>() < 0)
				return 1;
			process = true;
		}
		if (record["type"] == "zone" && record["name"] == "render_frame")
		{
			parent_ns = record["total_ns"].get<std::uint64_t>();
			if (record["calls"] != 1 || !record.contains("self_cycles"))
				return 2;
			parent = true;
		}
		if (record["type"] == "zone" && record["name"] == "chams")
		{
			child_ns = record["total_ns"].get<std::uint64_t>();
			if (record["calls"] != 1 || record["self_cycles"].get<std::uint64_t>() == 0)
				return 3;
			child = true;
		}
	}
	if (!process || !parent || !child || parent_ns < child_ns)
		return 4;
	std::puts("PASS performance CPU sample, nested exclusive cycles, successful-present counter and JSONL");
}
