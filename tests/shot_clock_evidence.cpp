#include <simulation/shot_clock_evidence.hpp>
#include "test_support.hpp"
#include <array>

int main() {
    using simulation::infer_shot_clock;
    auto value = infer_shot_clock(100.5f / 64, -1.25f);
    VESTA_CHECK(value && value->estimated_ticks == 100.25);
    VESTA_CHECK(value->minimum_tick == 100 && value->maximum_tick == 100);
    VESTA_CHECK(value->excludes(101) && !value->excludes(100));

    value = infer_shot_clock(100.0f / 64, -1);
    VESTA_CHECK(value && value->minimum_tick == 99 && value->maximum_tick == 100);
    VESTA_CHECK(!value->excludes(99) && !value->excludes(100));

    // Recorded native 0x1815e71f0 + 0x1815e6950 results for the current client hash.
    struct native_case { float current_ticks, wat; double selected_ticks; };
    constexpr std::array cases{
        native_case{100.5f, -1.25f, 100.25}, native_case{100, -1, 100},
        native_case{100, .75f, 101.75}, native_case{35000.5f, -.625f, 35000.875}};
    for (const auto& test : cases) {
        value = infer_shot_clock(test.current_ticks / 64, test.wat);
        VESTA_CHECK(value && value->estimated_ticks == test.selected_ticks);
        VESTA_CHECK(!value->excludes(static_cast<int>(std::floor(test.selected_ticks))));
    }
    VESTA_CHECK(!infer_shot_clock(NAN, 0));
    VESTA_CHECK(!infer_shot_clock(1, NAN));
    VESTA_CHECK(!infer_shot_clock(-1, 0));
    VESTA_CHECK(!infer_shot_clock(0, 0));
    VESTA_CHECK(!infer_shot_clock(1, -100));
    VESTA_CHECK(!infer_shot_clock(INFINITY, 0));
    std::cout << "shot_clock_evidence: native +1, fractional offset, boundary ambiguity, invalid inputs PASS\n";
}
