#include <system/latest_job.hpp>
#include <cstdio>
#include <future>
#include <thread>

int main()
{
	platform::latest_job_queue queue;
	int published{};
	queue.request("dust2", [] {});
	const auto old = queue.next({});
	queue.request("inferno", [&] { published = 0; });
	queue.request("nuke", [] {});
	if (!old || !old->stop.stop_requested())
		return 1;
	if (queue.commit(old->stop, [&] { published = 1; }))
		return 2;
	const auto newest = queue.next({});
	if (!newest || newest->key != "nuke")
		return 3;
	if (!queue.commit(newest->stop, [&] { published = 2; }) || published != 2)
		return 4;
	queue.cancel();
	if (queue.commit(newest->stop, [] {}))
		return 5;
	std::promise<bool> result;
	auto future = result.get_future();
	std::jthread waiting([&](std::stop_token stop) { result.set_value(!queue.next(stop)); });
	waiting.request_stop();
	if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready || !future.get())
		return 6;
	for (int i = 0; i < 10000; ++i)
		queue.request(std::to_string(i), [] {});
	if (queue.next({})->key != "9999")
		return 7;
	std::puts("PASS map jobs: cancel, coalesce, reject stale publication, interrupt idle wait");
}
