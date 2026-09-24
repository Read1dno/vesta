#include "test_support.hpp"
#include <system/frame_schedule.hpp>
#include <render/overlay/recovery.hpp>

int main()
{
    using namespace std::chrono;
    using foundation::frame_schedule;
    const auto start = frame_schedule::time_point{} + seconds(1);
    for (auto rate : {30u, 60u, 144u, 195u, 240u, 1000u})
    {
        frame_schedule schedule;
        schedule.set_rate(rate, start);
        const auto period = duration_cast<frame_schedule::clock::duration>(seconds(1)) / rate;
        for (unsigned n = 0; n < rate * 10; ++n)
        {
            VESTA_CHECK(schedule.deadline() == start + period * n);
            schedule.advance(schedule.deadline() + microseconds(100));
        }
        VESTA_CHECK(schedule.deadline() == start + period * (rate * 10));
        const auto late = schedule.deadline() + seconds(2);
        schedule.advance(late);
        VESTA_CHECK(schedule.deadline() == late + period);
        schedule.set_rate(0, late);
        schedule.advance(late);
        VESTA_CHECK(schedule.deadline() == late);
    }
    render::presentation_retry retry;
    VESTA_CHECK(retry.ready(start));
    retry.failed(start);
    VESTA_CHECK(!retry.ready(start + milliseconds(249)));
    VESTA_CHECK(retry.ready(start + milliseconds(250)));
    retry.failed(start + milliseconds(250));
    VESTA_CHECK(!retry.ready(start + milliseconds(749)));
    VESTA_CHECK(retry.ready(start + milliseconds(750)));
    retry.succeeded();
    VESTA_CHECK(retry.ready(start));
}
