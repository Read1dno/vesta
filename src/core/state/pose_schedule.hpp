#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace game::detail {
class pose_sample_schedule
{
public:
    using clock = std::chrono::steady_clock;
    void configure(std::uint32_t rate, clock::time_point now) noexcept
    {
        if (rate == m_rate) return;
        m_rate = rate;
        m_period = rate ? std::chrono::duration_cast<clock::duration>(std::chrono::seconds(1)) / rate
                        : clock::duration{};
        m_next = now;
    }
    [[nodiscard]] bool due(clock::time_point now) const noexcept
    {
        const auto tolerance = std::min(m_period / 16,
            std::chrono::duration_cast<clock::duration>(std::chrono::microseconds(250)));
        return !m_rate || now + tolerance >= m_next;
    }
    void sampled(clock::time_point finished) noexcept
    {
        m_next += m_period;
        // A slow read never starts a burst of catch-up reads.
        if (m_next <= finished) m_next = finished + m_period;
    }
    void reset() noexcept { m_next = {}; m_rate = 0; m_period = {}; }
private:
    std::uint32_t m_rate{};
    clock::duration m_period{};
    clock::time_point m_next{};
};
} // namespace game::detail
