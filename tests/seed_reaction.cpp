#include <simulation/seed_reaction.hpp>
#include "test_support.hpp"
int main() {
    using namespace std::chrono_literals;
    simulation::seed_reaction r;
    const auto t=simulation::seed_reaction::clock::time_point{100s};
    VESTA_CHECK(!r.ready(t,0));
    r.observe(true,t);
    VESTA_CHECK(r.ready(t,0));
    VESTA_CHECK(r.ready(t,-10));
    VESTA_CHECK(!r.ready(t+99ms,100));
    r.observe(true,t+50ms);
    VESTA_CHECK(r.ready(t+100ms,100));
    r.observe(true,t+500ms);
    VESTA_CHECK(r.ready(t+500ms,100));
    r.observe(false,t+510ms);
    VESTA_CHECK(!r.ready(t+1s,0));
    r.observe(true,t+1s);
    VESTA_CHECK(!r.ready(t+1099ms,100));
    VESTA_CHECK(r.ready(t+1100ms,100));
    r.reset();
    VESTA_CHECK(!r.ready(t+2s,0));
    r.observe(true,t);
    VESTA_CHECK(!r.ready(t-1ms,0));
    r.observe(true,t-1ms);
    VESTA_CHECK(r.ready(t-1ms,0));
    std::cout<<"seed_reaction: activation, hold, release, reactivation, rewind PASS\n";
}
