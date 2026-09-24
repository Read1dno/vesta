#pragma once

#include <array>
#include <memory>

namespace platform
{

// Single writer. Only reuse a buffer when no published frame or reader owns it.
template <class T> class snapshot_pool
{
  public:
	[[nodiscard]] std::shared_ptr<T> acquire()
	{
		for (auto &buffer : buffers_)
		{
			if (!buffer)
				buffer = std::make_shared<T>();
			if (buffer.use_count() == 1)
				return buffer;
		}
		return std::make_shared<T>();
	}

	void clear()
	{
		buffers_ = {};
	}

  private:
	std::array<std::shared_ptr<T>, 2> buffers_;
};

} // namespace platform
