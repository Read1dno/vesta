#pragma once

#include <algorithm>
#include <chrono>

namespace render
{

class presentation_retry
{
  public:
    using clock = std::chrono::steady_clock;
    [[nodiscard]] bool ready(clock::time_point now) const noexcept
    {
        return now >= m_next;
    }
    void failed(clock::time_point now) noexcept
    {
        m_next = now + std::chrono::milliseconds(m_delay_ms);
        m_delay_ms = std::min(m_delay_ms * 2, 2000);
    }
    void succeeded() noexcept
    {
        m_next = {};
        m_delay_ms = 250;
    }

  private:
    clock::time_point m_next{};
    int m_delay_ms{250};
};

} // namespace render
