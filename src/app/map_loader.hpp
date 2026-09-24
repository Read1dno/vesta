#pragma once

#include <system/latest_job.hpp>
#include <thread>

namespace app
{

class map_loader
{
  public:
	map_loader();
	~map_loader();
	void request(std::string map);

  private:
	void run(std::stop_token stop);
	platform::latest_job_queue queue_;
	std::jthread worker_;
};

} // namespace app
