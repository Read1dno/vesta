#pragma once

#include <chrono>
#include <optional>

namespace app {

class map_absence_guard {
public:
    bool observe(bool local_valid, std::chrono::steady_clock::time_point now) noexcept {
        if (local_valid) {
            first_missing_.reset();
            return false;
        }
        if (!first_missing_) {
            first_missing_ = now;
            return false;
        }
        return now - *first_missing_ >= std::chrono::milliseconds(1500);
    }

private:
    std::optional<std::chrono::steady_clock::time_point> first_missing_{};
};

} // namespace app