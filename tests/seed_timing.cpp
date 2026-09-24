#include <simulation/seed_timing.hpp>
#include <simulation/seed_schedule.hpp>

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
    VESTA_CHECK(select_tick(false, -1, std::numeric_limits<int>::max(), -1) == -1);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(false,true)==1000us);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(true,true)==500us);
    VESTA_CHECK(simulation::seed_schedule::poll_interval(false,false)==2000us);
    return 0;
}
