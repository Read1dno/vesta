#pragma once

#include <chrono>
#include <cstdint>

namespace foundation
{

class frame_schedule
{
  public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    void set_rate(std::uint32_t rate, time_point now) noexcept
    {
        if (rate == m_rate)
            return;
        m_rate = rate;
        m_period = rate ? std::chrono::duration_cast<clock::duration>(std::chrono::seconds(1)) / rate
                        : clock::duration{};
        m_deadline = now;
    }

    [[nodiscard]] time_point deadline() const noexcept
    {
        return m_deadline;
    }

    void advance(time_point now) noexcept
    {
        if (m_period <= clock::duration{})
        {
            m_deadline = now;
            return;
        }
        // Preserve phase after a short timer overshoot; never replay missed frames.
        m_deadline += m_period;
        if (m_deadline <= now)
            m_deadline = now + m_period;
    }

    void reset(time_point now) noexcept
    {
        m_deadline = now;
    }

  private:
    std::uint32_t m_rate{};
    clock::duration m_period{};
    time_point m_deadline{};
};

} // namespace foundation
