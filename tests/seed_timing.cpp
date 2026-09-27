#include <simulation/seed_timing.hpp>
#include <simulation/seed_schedule.hpp>
#include <simulation/seed_evaluation.hpp>

#include "test_support.hpp"
#include <limits>

int main()
{
    using simulation::seed_timing::primary_ready;
    using simulation::seed_timing::select_tick;

    VESTA_CHECK(select_tick(false, 9000, 120, 400) == 121);
    VESTA_CHECK(select_tick(true, 9000, 120, 400) == 9000);
    VESTA_CHECK(select_tick(false, -1, -1, 400) == 401);
    VESTA_CHECK(select_tick(true, -1, 120, 400) == 401);
    VESTA_CHECK(select_tick(false, -1, -1, -1) == -1);
    const auto network_seed_tick = select_tick(false, -1, 120, 400);
    const auto player_prediction_tick = 400;
    VESTA_CHECK(network_seed_tick == 121);
    VESTA_CHECK(primary_ready(399, 0.0f, player_prediction_tick));
    VESTA_CHECK(!primary_ready(399, 0.0f, network_seed_tick));

    VESTA_CHECK(primary_ready(100, 0.5f, 101));
    VESTA_CHECK(primary_ready(101, 0.0f, 101));
    VESTA_CHECK(!primary_ready(101, 0.5f, 101));
    VESTA_CHECK(!primary_ready(102, 0.0f, 101));
    VESTA_CHECK(!primary_ready(100, std::numeric_limits<float>::quiet_NaN(), 101));
    VESTA_CHECK(!primary_ready(100, 0.0f, -1));
    using namespace std::chrono_literals;
    using simulation::seed_timing::fresh_decision;
    VESTA_CHECK(fresh_decision(1ms, 100us, 2ms, 3ms));
    VESTA_CHECK(!fresh_decision(5ms, 100us, 2ms, 3ms));
    VESTA_CHECK(!fresh_decision(1ms, 1100us, 2ms, 3ms));
    VESTA_CHECK(!fresh_decision(1ms, 100us, 6ms, 7ms));
    VESTA_CHECK(!fresh_decision(1ms, 100us, 11ms, 15ms));
    VESTA_CHECK(!fresh_decision(-1ms, 100us, 2ms, 3ms));
    using simulation::seed_timing::fresh_network_decision;
    VESTA_CHECK(!fresh_network_decision(1ms, 100us, 2ms, 3ms));
    VESTA_CHECK(fresh_network_decision(1ms, 100us, 4ms, 5ms));
    VESTA_CHECK(fresh_network_decision(1ms, 100us, 5ms, 9ms));
    VESTA_CHECK(fresh_network_decision(1ms, 100us, 9ms, 10500us));
    VESTA_CHECK(!fresh_network_decision(1ms, 100us, 9ms, 10501us));
    VESTA_CHECK(!fresh_network_decision(5ms, 100us, 5ms, 8ms));
    VESTA_CHECK(!fresh_network_decision(1ms, 1100us, 5ms, 8ms));
    VESTA_CHECK(!fresh_network_decision(1ms, 100us, 9ms, 8ms));
    simulation::seed_timing::network_phase_window feedback{};
    VESTA_CHECK(feedback.contains(6ms) && feedback.contains(9ms));
    feedback.observe(6ms.count() * 1000, -1);
    VESTA_CHECK(feedback.minimum_us == 5000);
    feedback.observe(6ms.count() * 1000, -1);
    VESTA_CHECK(feedback.minimum_us == 6500 && !feedback.contains(6ms));
    feedback.observe(9ms.count() * 1000, 1);
    feedback.observe(9ms.count() * 1000, 1);
    VESTA_CHECK(feedback.maximum_us == 8500 && !feedback.contains(9ms));
    feedback.observe(7ms.count() * 1000, 0);
    VESTA_CHECK(feedback.early_streak == 0 && feedback.late_streak == 0);
    simulation::seed_timing::network_phase_window slow{};
    slow.observe(5ms.count() * 1000, 1);
    slow.observe(5ms.count() * 1000, 1);
    VESTA_CHECK(slow.minimum_us == 2500 && slow.maximum_us == 4500);
    simulation::seed_timing::network_phase_window fast{};
    fast.observe(10ms.count() * 1000, -1);
    fast.observe(10ms.count() * 1000, -1);
    VESTA_CHECK(fast.minimum_us == 10500 && fast.maximum_us == 12500);
    VESTA_CHECK(select_tick(false, -1, std::numeric_limits<int>::max(), -1) == -1);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(false,true)==1000us);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(true,true)==500us);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(false,false)==2000us);
    using clock = std::chrono::steady_clock;
    using simulation::seed_timing::observe_phase;
    using simulation::seed_timing::current_phase;
    int phase_tick = -1;
    clock::time_point boundary{}, sampled_at{};
    const auto epoch = clock::time_point{100s};
    observe_phase(100, epoch, phase_tick, boundary, sampled_at);
    VESTA_CHECK(!current_phase(phase_tick, 100, boundary));
    observe_phase(100, epoch + 1ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(boundary == clock::time_point{});
    observe_phase(100, epoch + 15ms, phase_tick, boundary, sampled_at);
    observe_phase(101, epoch + 16ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 101, boundary));
    VESTA_CHECK(!current_phase(phase_tick, 102, boundary));
    observe_phase(101, epoch + 18ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(boundary == epoch + 16ms);
    observe_phase(104, epoch + 60ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(!current_phase(phase_tick, 104, boundary));
    observe_phase(104, epoch + 75ms, phase_tick, boundary, sampled_at);
    observe_phase(105, epoch + 76ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 105, boundary));
    observe_phase(20, epoch + 80ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(!current_phase(phase_tick, 20, boundary));
    observe_phase(-1, epoch + 81ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(phase_tick == -1 && boundary == clock::time_point{});
    observe_phase(21, epoch + 96ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(!current_phase(phase_tick, 21, boundary));
    observe_phase(21, epoch + 111ms, phase_tick, boundary, sampled_at);
    observe_phase(22, epoch + 112ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 22, boundary));

    observe_phase(24, epoch + 113ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 24, boundary));
    observe_phase(26, epoch + 114ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 26, boundary));
    observe_phase(27, epoch + 130ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(!current_phase(phase_tick, 27, boundary));
    observe_phase(28, epoch + 131ms, phase_tick, boundary, sampled_at);
    VESTA_CHECK(current_phase(phase_tick, 28, boundary));

    for (int mode = 0; mode < 3; ++mode) {
        const bool use_current = mode != 2, use_next = mode != 0;
        for (int values = 0; values < 4; ++values) {
            const bool a = (values & 1) != 0, b = (values & 2) != 0;
            int calls{};
            const auto result = simulation::seed_evaluation::required_pair(
                use_current, use_next, false, [&](bool next) { ++calls; return next ? b : a; });
            const bool expected = (!use_current || a) && (!use_next || b);
            VESTA_CHECK(((!use_current || result.first) && (!use_next || result.second)) == expected);
            VESTA_CHECK(calls == (mode == 1 && a ? 2 : 1));
        }
    }
    for (const bool hit : {false, true}) {
        int calls{};
        const auto result = simulation::seed_evaluation::required_pair(true, true, true,
            [&](bool) { ++calls; return hit; });
        VESTA_CHECK(result.first == hit && result.second == hit && calls == 1);
    }
    struct vec { float x{}, y{}, z{}; };
    struct sample { int tick{}; vec hash_angles{}, direction_angles{}; };
    sample a{100, {10, 20, 0}, {10, 20, 0}}, b = a;
    VESTA_CHECK(simulation::seed_evaluation::identical_ray(a, b));
    b.direction_angles.x += 0.001f;
    VESTA_CHECK(!simulation::seed_evaluation::identical_ray(a, b));
    b = a; ++b.tick;
    VESTA_CHECK(!simulation::seed_evaluation::identical_ray(a, b));
    b = a; b.hash_angles.y += 0.5f;
    VESTA_CHECK(!simulation::seed_evaluation::identical_ray(a, b));
    std::cout << "seed_timing: phase lifecycle and equivalent ray short-circuit PASS\n";
    return 0;
}
