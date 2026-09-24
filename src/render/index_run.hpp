#pragma once
#include <cstdint>
#include <limits>
#include <utility>

namespace render
{

class index_run
{
  public:
	template <class Emit> void append(std::uint32_t first, std::uint32_t count, Emit &&emit)
	{
		if (!count)
			return;
		const auto end = static_cast<std::uint64_t>(m_first) + m_count;
		if (m_count && (end != first || count > std::numeric_limits<std::uint32_t>::max() - m_count))
			flush(emit);
		if (!m_count)
			m_first = first;
		m_count += count;
	}
	template <class Emit> void flush(Emit &&emit)
	{
		if (m_count)
			emit(m_first, m_count);
		m_count = 0;
	}

  private:
	std::uint32_t m_first{};
	std::uint32_t m_count{};
};

} // namespace render
