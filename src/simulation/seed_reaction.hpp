#pragma once
#include <algorithm>
#include <chrono>

namespace simulation {
class seed_reaction {
public:
    using clock=std::chrono::steady_clock;
    void observe(bool active, clock::time_point now) noexcept {
        if (!active) { reset(); return; }
        if (!m_active || now<m_started) m_started=now;
        m_active=true;
    }
    bool ready(clock::time_point now, int delay_ms) const noexcept {
        return m_active && now>=m_started
            && now-m_started>=std::chrono::milliseconds(std::max(delay_ms,0));
    }
    void reset() noexcept { m_active=false; m_started={}; }
private:
    bool m_active{};
    clock::time_point m_started{};
};
}
