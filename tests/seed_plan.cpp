#include "fixtures/seed_plan/harness.hpp"
#include <cstdio>

int main() {
    using controller = features::aimbot::aimbot_t;
    using namespace std::chrono_literals;
    unsigned cases{}, failed{};
    const auto check = [&](const char* name, bool value) {
        ++cases;
        if (!value) { ++failed; std::printf("FAIL %s\n", name); }
    };
    controller value;
    const auto now = std::chrono::steady_clock::time_point{100s};
    fixture::view = {10.0f, 20.0f, 0.0f};
    fixture::punch = {};
    fixture::readable = true;
    fixture::punch_readable = true;
    fixture::native_recoil.reset();

    const auto plan = value.build_seed_plan(0x10000, {}, false, 100, now, true);
    check("candidate_created", bool(plan));
    check("phase_is_ambiguous", plan && plan->phase == controller::seed_prediction_phase::ambiguous);
    check("tick_recorded", plan && plan->source_tick == 100 && plan->current.tick == 100 && plan->next.tick == 100);
    check("source_angles_from_pawn", plan && plan->source_angles.x == 10.0f && plan->source_angles.y == 20.0f);
    check("current_fraction", plan && plan->current.fraction == 0.0f);
    check("last_fraction", plan && plan->next.fraction == simulation::seed_window::k_ray_fractions.back());
    check("stable_no_recoil_angles", plan && plan->current.direction_angles.x == 10.0f
        && plan->next.direction_angles.y == 20.0f);
    check("possible_either", plan && value.possible_seed_match(*plan, {true, false})
        && value.possible_seed_match(*plan, {false, true}));
    check("safe_requires_both", plan && !value.safe_seed_match(*plan, {true, false})
        && value.safe_seed_match(*plan, {true, true}));
    check("invalid_tick_rejected", !value.build_seed_plan(0x10000, {}, false, 0, now, true));

    fixture::readable = false;
    check("failed_view_read_rejected", !value.build_seed_plan(0x10000, {}, false, 100, now, true));
    fixture::readable = true;
    fixture::view.x = NAN;
    check("invalid_view_rejected", !value.build_seed_plan(0x10000, {}, false, 100, now, true));
    fixture::view = {10.0f, 20.0f, 0.0f};
    fixture::punch_readable = false;
    check("failed_recoil_read_rejected", !value.build_seed_plan(0x10000, {}, false, 100, now, true));

    std::printf("seed_plan_current cases=%u failed=%u\n", cases, failed);
    return failed ? 1 : 0;
}
