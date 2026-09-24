#pragma once
#include <chrono>
namespace simulation::seed_schedule {
[[nodiscard]] constexpr std::chrono::microseconds poll_interval(bool pending, bool active) noexcept {
    // Poll all phases; a predicted boundary must not hide target/reaction changes.
    return std::chrono::microseconds(pending ? 500 : active ? 1000 : 2000);
}
}
