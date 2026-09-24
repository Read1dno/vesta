#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace platform
{

// One pending job, one active job. A replaced job can never publish its result.
class latest_job_queue
{
  public:
	struct job
	{
		std::string key;
		std::stop_token stop;
	};

	template <class Invalidate> void request(std::string key, Invalidate &&invalidate)
	{
		std::lock_guard lock(mutex_);
		active_.request_stop();
		pending_ = std::move(key);
		invalidate();
		changed_.notify_one();
	}

	[[nodiscard]] std::optional<job> next(std::stop_token stop)
	{
		std::unique_lock lock(mutex_);
		if (!changed_.wait(lock, stop, [&] { return pending_.has_value(); }))
			return {};
		active_ = std::stop_source{};
		job result{std::move(*pending_), active_.get_token()};
		pending_.reset();
		return result;
	}

	template <class Publish> bool commit(std::stop_token token, Publish &&publish)
	{
		std::lock_guard lock(mutex_);
		if (token.stop_requested())
			return false;
		publish();
		return true;
	}

	void cancel()
	{
		std::lock_guard lock(mutex_);
		active_.request_stop();
	}

  private:
	std::mutex mutex_;
	std::condition_variable_any changed_;
	std::optional<std::string> pending_;
	std::stop_source active_;
};

} // namespace platform
