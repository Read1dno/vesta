#pragma once
#include <optional>
#include <simulation/seed_window.hpp>
namespace simulation {
std::optional<seed_window::recoil_pair> read_recoil_state(std::uintptr_t pawn);
}
