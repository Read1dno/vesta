#pragma once
#include <optional>
#include <simulation/shot_model.hpp>
#include <simulation/seed_window.hpp>

namespace simulation {
[[nodiscard]] std::optional<seed_window::recoil_pair> read_recoil_state(std::uintptr_t pawn);
[[nodiscard]] std::optional<float> read_seed_fraction();
[[nodiscard]] int read_seed_tick(std::uintptr_t controller, std::uintptr_t pawn);

[[nodiscard]] std::optional<foundation::vec3> read_shot_punch(
    std::uintptr_t pawn, shot_model::shot_time target);
}
