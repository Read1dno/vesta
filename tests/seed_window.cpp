#include <simulation/seed_window.hpp>
#include <external/json.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>

using nlohmann::json;
using foundation::vec3;
namespace window = simulation::seed_window;
namespace model = simulation::shot_model;

namespace {
int failures{};
void check(const char* name, bool ok)
{
    if (!ok) {
        ++failures;
        std::cout << "FAIL " << name << "\n";
    }
}
vec3 vec(const json& j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }

void tick_contract()
{
    const auto network = window::candidate_ticks(false, 101);
    check("network_ticks_are_sim_and_sim_plus_one",
        network.count == 2 && network.ticks[0] == 100 && network.ticks[1] == 101);
    const auto host = window::candidate_ticks(true, 101);
    check("host_ticks_keep_both_neighbours",
        host.count == 2 && host.ticks[0] == 101 && host.ticks[1] == 102);
    check("first_tick_has_no_predecessor", window::candidate_ticks(false, 1).count == 1);
    check("invalid_tick_has_no_candidates", window::candidate_ticks(false, 0).count == 0);
}

void no_recoil_is_one_seed()
{
    window::punch_sampler sampler{{}};
    window::window resolved{};
    check("zero_recoil_resolves",
        window::resolve(sampler, {3.1f, 45.2f, 0}, 200, resolved) == window::status::ok);
    check("zero_recoil_same_angles", model::same_direction(resolved.angles[0], resolved.angles[2]));
}

void boundary_is_refused()
{
    // Base sits just below a 0.5 degree seed edge; decaying punch crosses it inside the tick.
    window::recoil_pair state{};
    state.predictable = {{199, 0.5f}, {-0.3f, 0, 0}, {-30.0f, 0, 0}};
    window::punch_sampler sampler{state};
    vec3 early{}, late{};
    check("sample_early", sampler.at({200, 0.0f}, early));
    check("sample_late", sampler.at({200, 0.99999f}, late));
    const float edge = 10.0f;
    const vec3 base{edge - (early.x + late.x) * 0.5f, 20.0f, 0};
    window::window resolved{};
    check("seed_edge_inside_tick_is_refused",
        window::resolve(sampler, base, 200, resolved) == window::status::seed_unstable);
}

void native_recordings(const char* path)
{
    std::ifstream in(path);
    check("fixture_opens", bool(in));
    std::map<std::tuple<std::string, int, float>, json> punches;
    std::vector<json> shots;
    for (std::string line; std::getline(in, line);) {
        auto j = json::parse(line);
        const auto event = j.at("event").get<std::string>();
        if (event == "punch" && j.contains("state") && j.at("state").is_array())
            punches[{j.at("recording"), j.at("time").at("tick"), j.at("time").at("fraction")}] = j;
        else if (event == "shot_state" && j.at("before").contains("punch_cache"))
            shots.push_back(std::move(j));
    }

    int paired{}, stale{}, stable{}, unstable{}, wrong_seed{}, outside{}, tail_wrong{};
    for (const auto& shot : shots) {
        const int tick = shot.at("tick");
        const float fraction = shot.at("fraction");
        const auto found = punches.find({shot.at("recording"), tick, fraction});
        if (found == punches.end()) continue;
        const auto& states = found->second.at("state");
        window::recoil_pair state{
            {{states[0].at("tick"), states[0].at("fraction")}, vec(states[0].at("angle")), vec(states[0].at("velocity"))},
            {{states[1].at("tick"), states[1].at("fraction")}, vec(states[1].at("angle")), vec(states[1].at("velocity"))}};
        // Two rows carry a punch state from another session (base tick after the shot).
        if (state.predictable.base.tick > tick || state.unpredictable.base.tick > tick) {
            ++stale;
            continue;
        }
        ++paired;
        const auto view = vec(shot.at("before").at("view"));
        const auto native = vec(shot.at("angles"));
        const auto native_seed = model::seed(native, tick);

        vec3 tail{};
        for (const auto& track : shot.at("before").at("punch_cache"))
            tail += vec(track.at("count").get<int>() ? track.at("tail") : track.at("base"));
        tail_wrong += model::seed(view + tail * 2.0f, tick) != native_seed;

        window::punch_sampler sampler{state};
        window::window resolved{};
        const auto status = window::resolve(sampler, view, tick, resolved);
        if (status == window::status::seed_unstable) {
            ++unstable;
            continue;
        }
        check("native_window_resolves", status == window::status::ok);
        ++stable;
        vec3 lo{1e9f, 1e9f, 0}, hi{-1e9f, -1e9f, 0};
        for (const auto& angles : resolved.angles) {
            wrong_seed += model::seed(angles, tick) != native_seed;
            lo = {std::min(lo.x, angles.x), std::min(lo.y, angles.y), 0};
            hi = {std::max(hi.x, angles.x), std::max(hi.y, angles.y), 0};
        }
        constexpr float slack = 1e-3f;
        outside += native.x < lo.x - slack || native.x > hi.x + slack
            || native.y < lo.y - slack || native.y > hi.y + slack;
    }
    std::cout << "SEED_WINDOW native_shots=" << paired << " stale_fixture_rows=" << stale
              << " fired=" << stable << " refused_unstable=" << unstable
              << " wrong_seed=" << wrong_seed << " native_ray_outside_window=" << outside
              << " baseline_tail2_wrong_seed=" << tail_wrong << "\n";
    check("fixture_has_native_shots", paired >= 20);
    check("every_accepted_window_has_the_native_seed", wrong_seed == 0);
    check("native_ray_inside_traced_window", outside == 0);
    check("refusals_are_rare", unstable * 4 <= paired);
}
} // namespace

int main(int argc, char** argv)
{
    tick_contract();
    no_recoil_is_one_seed();
    boundary_is_refused();
    if (argc > 1) native_recordings(argv[1]);
    else check("fixture_argument", false);
    std::cout << (failures ? "FAILED " : "PASSED ") << failures << "\n";
    return failures ? 1 : 0;
}
