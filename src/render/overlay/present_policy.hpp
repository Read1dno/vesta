#pragma once

#include <chrono>
#include <dxgi.h>

namespace render
{

class present_recovery
{
  public:
	using clock = std::chrono::steady_clock;
	[[nodiscard]] bool retry_surface(HRESULT present, HRESULT device, clock::time_point now)
	{
		if (FAILED(device) ||
		    (present != DXGI_ERROR_INVALID_CALL && present != DXGI_ERROR_NOT_CURRENTLY_AVAILABLE &&
		     present != DXGI_ERROR_ACCESS_LOST))
			return false;
		if (attempts_ && now - window_ >= std::chrono::seconds(30))
			attempts_ = 0;
		if (!attempts_)
			window_ = now;
		if (attempts_ >= 3)
			return false;
		++attempts_;
		return true;
	}

  private:
	clock::time_point window_{};
	unsigned attempts_{};
};

} // namespace render
