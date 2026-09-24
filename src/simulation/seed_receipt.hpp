#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace simulation {
class seed_receipt {
public:
    void reset() noexcept { *this = {}; }

    [[nodiscard]] bool observe(std::uintptr_t weapon, float time, int prediction_tick) noexcept {
        if (!weapon || !std::isfinite(time) || time < 0 || prediction_tick <= 0
            || prediction_tick < confirmed_tick_) return false;
        select_weapon(weapon);
        // Prediction can retract last-shot time without retracting this input.
        return true;
    }

    void arm(std::uintptr_t weapon, float before, int prediction_tick) noexcept {
        select_weapon(weapon);
        before_ = before;
        submitted_tick_ = prediction_tick;
        pending_ = true;
    }

    [[nodiscard]] bool consumed(std::uintptr_t weapon, float time) const noexcept {
        return pending_ && weapon == weapon_ && std::isfinite(time)
            && time > before_ && time > confirmed_time_;
    }

    void acknowledge(float time) noexcept {
        if (!consumed(weapon_, time)) return;
        confirmed_time_ = time;
        confirmed_tick_ = std::max(confirmed_tick_, submitted_tick_);
        pending_ = false;
    }

private:
    void select_weapon(std::uintptr_t weapon) noexcept {
        if (weapon == weapon_) return;
        weapon_ = weapon;
        confirmed_time_ = -1.0f;
        pending_ = false;
    }
    std::uintptr_t weapon_{};
    float before_{}, confirmed_time_{-1.0f};
    int submitted_tick_{}, confirmed_tick_{};
    bool pending_{};
};
}
