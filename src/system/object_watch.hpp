#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace foundation
{

// Watches the same object's representation, including padding, without hashing.
// The owner serializes writes with update(); pointee changes are not observed.
template <typename T> class object_watch
{
	static_assert(std::is_trivially_copyable_v<T>);

  public:
	explicit object_watch(const T &value) noexcept
	{
		save(value);
	}

	[[nodiscard]] bool update(const T &value) noexcept
	{
		if (std::memcmp(m_bytes.data(), &value, sizeof(T)) == 0)
			return false;
		save(value);
		return true;
	}

  private:
	void save(const T &value) noexcept
	{
		std::memcpy(m_bytes.data(), &value, sizeof(T));
	}
	std::array<std::byte, sizeof(T)> m_bytes{};
};

} // namespace foundation
