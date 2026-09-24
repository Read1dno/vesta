#include <system/frame_schedule.hpp>
#include <windows.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    const bool legacy = argc > 1 && std::string_view(argv[1]) == "legacy";
    HANDLE timer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) return 2;
    using clock = std::chrono::steady_clock;
    std::cout << "scheduler=" << (legacy ? "relative_baseline" : "phase_preserving") << '\n';
    for (const auto rate : {30u, 195u, 1000u})
    {
        foundation::frame_schedule schedule;
        const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::seconds(1)) / rate;
        const auto start = clock::now();
        auto previous = start;
        schedule.set_rate(rate, start);
        const auto samples = rate * 2;
        for (unsigned i = 0; i < samples; ++i)
        {
            schedule.advance(previous);
            const auto deadline = legacy ? previous + period : schedule.deadline();
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - clock::now()).count();
            if (remaining > 0)
            {
                LARGE_INTEGER due{};
                due.QuadPart = -std::max<LONGLONG>(1, (remaining + 99) / 100);
                if (!::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) return 3;
                if (::WaitForSingleObject(timer, 1000) != WAIT_OBJECT_0) return 4;
            }
            previous = clock::now();
        }
        const auto elapsed = std::chrono::duration<double>(previous - start).count();
        std::cout << "requested=" << rate << " samples=" << samples << " measured_hz="
            << std::fixed << std::setprecision(2) << samples / elapsed << '\n';
    }
    ::CloseHandle(timer);
}
