#include "test_support.hpp"
#include <render/overlay/render_policy.hpp>
#include <core/state/pose_schedule.hpp>
#include <core/state/pose_freshness.hpp>
#include <render/menu/card_metrics.hpp>

int main()
{
    using namespace std::chrono;
    for (bool attached : {false, true})
        for (bool esp : {false, true})
            for (bool chams : {false, true})
                for (bool hardware : {false, true})
                    for (bool gpu : {false, true}) {
                        const auto plan = render::plan_features(attached, esp, chams, hardware, gpu);
                        VESTA_CHECK(plan.gpu_effects == (hardware && gpu));
                        VESTA_CHECK(plan.sample_players == (attached && (esp || (hardware && gpu && chams))));
                    }
    VESTA_CHECK(render::plan_features(true, true, true, false, false).sample_players);
    VESTA_CHECK(render::overlay_gpu_priority < 0);
    for (auto fps : {30u, 60u, 144u, 190u, 240u})
        VESTA_CHECK(render::pose_sample_limit(true, fps) == 0);
    VESTA_CHECK(render::pose_sample_limit(true, 1000) == 240);
    VESTA_CHECK(render::pose_sample_limit(false, 190) == 240);

    using schedule = game::detail::pose_sample_schedule;
    const auto start = schedule::clock::time_point{} + seconds(1);
    for (auto fps : {30u, 60u, 144u, 190u, 240u}) {
        schedule cadence;
        cadence.configure(render::pose_sample_limit(true, fps), start);
        for (unsigned n=0; n<fps; ++n) {
            const auto frame = start + duration_cast<schedule::clock::duration>(seconds(1))*n/fps;
            VESTA_CHECK(cadence.due(frame));
            cadence.sampled(frame + microseconds(100));
        }
    }
    schedule bounded;
    bounded.configure(240, start);
    unsigned reads{};
    for (unsigned n=0; n<10000; ++n) {
        const auto frame = start + milliseconds(n);
        if (bounded.due(frame)) { ++reads; bounded.sampled(frame+microseconds(100)); }
    }
    VESTA_CHECK(reads >= 2399 && reads <= 2401);
    const auto after_pause = start + seconds(20);
    VESTA_CHECK(bounded.due(after_pause));
    bounded.sampled(after_pause + milliseconds(10));
    VESTA_CHECK(!bounded.due(after_pause + milliseconds(10)));
    bounded.reset();
    bounded.configure(240, after_pause);
    VESTA_CHECK(bounded.due(after_pause));

    game::detail::pose_identity id{1, 10, 20, 20, 30, 40, 2, true};
    VESTA_CHECK(game::detail::pose_frame_usable(id,id,start,start+milliseconds(10)));
    auto changed=id; changed.presentation+=2;
    VESTA_CHECK(!game::detail::pose_frame_usable(id,changed,start,start));
    for (float height : {12.f, 32.f, 64.f, 160.f, 310.f}) {
        const auto rows=render::menu::wrapped_text_rows(height,4.f,42.f);
        VESTA_CHECK(rows*42.f >= height+12.f);
    }
    std::cout << "render_latency: CPU ESP demand, shared pose epoch, per-frame sampling at 30..240 FPS, capped reads=" << reads << "/10s, no catch-up, text height PASS\n";
}
