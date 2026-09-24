#include <stdafx.hpp>
#include <simulation/shot_trace.hpp>
#include <simulation/shot_clock_evidence.hpp>
#include <core/memory/compatibility.hpp>
#include <fstream>
#include <condition_variable>

namespace simulation::shot_trace {
namespace {
struct pending_shot {
    std::uintptr_t pawn{}, weapon{};
    unsigned id{};
    int candidate_tick{};
    std::chrono::steady_clock::time_point at{};
};
thread_local pending_shot pending;
struct observed_marker {
    std::uintptr_t pawn{}, weapon{};
    float time{};
};
thread_local observed_marker last_marker;
struct channel {
    std::mutex mutex;
    std::condition_variable ready;
    std::array<std::string,64> lines;
    unsigned read{}, count{}, accepted{};
};
channel& output() { static auto* value = new channel; return *value; }
void write(std::string line)
{
    auto& queue = output();
    const std::unique_lock lock(queue.mutex, std::try_to_lock);
    if (!lock || queue.count == queue.lines.size() || queue.accepted >= 2048) return;
    static const auto frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value.QuadPart; }();
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter) || frequency <= 0 || line.empty() || line.front() != '{') return;
    line.insert(1, std::format(R"("qpc":{},"qpc_frequency":{},)", counter.QuadPart, frequency));
    queue.lines[(queue.read + queue.count) % queue.lines.size()] = std::move(line);
    ++queue.count;
    ++queue.accepted;
    queue.ready.notify_one();
}
}
decision_scope::~decision_scope()
{
    if (!enabled) return;
    struct counters {
        std::array<unsigned,static_cast<unsigned>(decision_reason::count)> counts{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        unsigned reports{};
    };
    static thread_local counters stats;
    if (stats.reports>=120) return;
    ++stats.counts[static_cast<unsigned>(reason)];
    const auto now=std::chrono::steady_clock::now();
    if (now-stats.start<std::chrono::seconds(1)) return;
    constexpr std::array names{"snapshot","policy","reaction","cooldown","pending","no_targets",
        "no_hit","recheck","auto_stop","changed_state","stale_delivery","input_failed","submitted",
        "collision","inactive","weapon","plan_unavailable","restricted"};
    static_assert(names.size() == static_cast<unsigned>(decision_reason::count));
    auto line = std::format(R"({{"event":"decisions","elapsed_ms":{})",
        std::chrono::duration_cast<std::chrono::milliseconds>(now-stats.start).count());
    for (std::size_t i=0;i<names.size();++i) line += std::format(",\"{}\":{}",names[i],stats.counts[i]);
    write(line + "}");
    stats.counts={};stats.start=now;++stats.reports;
}

void initialize()
{
    static const bool started = [] {
        (void)game::compatibility::shot_punch_offset();
        try {
        std::thread([] {
            wchar_t path[32768]{};
            const auto length = GetModuleFileNameW(nullptr, path, std::size(path));
            if (!length || length >= std::size(path)) return;
            std::ofstream file(std::filesystem::path(path).parent_path() / L"vesta.seed-trace.jsonl",
                std::ios::trunc);
            if (!file) return;
            auto& queue = output();
            for (;;) {
                std::string line;
                {
                    std::unique_lock lock(queue.mutex);
                    queue.ready.wait(lock, [&] { return queue.count != 0; });
                    line = std::move(queue.lines[queue.read]);
                    queue.read = (queue.read + 1) % queue.lines.size();
                    --queue.count;
                }
                file << line << '\n';
                file.flush();
            }
        }).detach();
        } catch (...) { return false; }
        return true;
    }();
    (void)started;
}
bool enabled() noexcept
{
    static const bool requested = [] {
        wchar_t value[2]{};
        const bool requested = GetEnvironmentVariableW(L"VESTA_SEED_TRACE",value,2)==1 && value[0]==L'1';
        if (requested) initialize();
        return requested;
    }();
    return requested;
}
void expired(int observed_tick)
{
    if (pending.id) write(std::format(
        R"({{"event":"expired","id":{},"observed_tick":{}}})", pending.id, observed_tick));
}

void input(std::uintptr_t pawn, std::uintptr_t weapon, int tick, int next_tick,
    foundation::vec3 angles, foundation::vec3 next_angles,
    float inaccuracy, float spread, float recoil, std::uintptr_t target,
    float fraction, foundation::vec3 view, foundation::vec3 punch,
    foundation::vec3 velocity, int player_tick, int clip, int item, int next_attack, ray_sample ray)
{
    static thread_local unsigned next_id{};
    int sim_tick_at_press{-1};
    (void)app::context().process.copy(pawn + SCHEMA("C_BaseEntity","m_nSimulationTick"_id),
        &sim_tick_at_press, sizeof(sim_tick_at_press));
    pending = {pawn, weapon, ++next_id, tick, std::chrono::steady_clock::now()};
    write(std::format(
        R"({{"event":"input","id":{},"tick":{},"next_tick":{},"angles":[{},{},{}],"next_angles":[{},{},{}],"inaccuracy":{},"spread":{},"recoil":{},"target":{},"fraction":{},"view":[{},{},{}],"prepared_punch":[{},{},{}],"velocity":[{},{},{}],"player_tick":{},"sim_tick_at_press":{},"phase":"{}","clip":{},"item":{},"next_attack":{},"weapon":{},"seed":{},"pellet":{},"ray_origin":[{},{},{}],"ray_direction":[{},{},{}]}})",
        pending.id,tick,next_tick,angles.x,angles.y,angles.z,
        next_angles.x,next_angles.y,next_angles.z,inaccuracy,spread,recoil,target,
        fraction,view.x,view.y,view.z,punch.x,punch.y,punch.z,
        velocity.x,velocity.y,velocity.z,player_tick,sim_tick_at_press,tick==next_tick ? "next" : "current",clip,item,next_attack,weapon,ray.seed,ray.pellet,
        ray.origin.x,ray.origin.y,ray.origin.z,ray.direction.x,ray.direction.y,ray.direction.z));
}
void consumed(std::uintptr_t pawn, int observed_tick, int shots)
{
    if (!pending.id || pawn != pending.pawn) return;
    const auto latency = std::chrono::duration<float,std::milli>(
        std::chrono::steady_clock::now() - pending.at).count();
    const auto saved = pending;
    pending = {};
    if (latency > 1500) return;
    float fire_time{},confirm_time{},wat{},confirm_wat{};
    foundation::vec3 punch{},confirm_punch{};
    auto& process = app::context().process;
    const auto offset = game::compatibility::shot_punch_offset();
    const auto wat_offset=SCHEMA("C_CSWeaponBase","m_flWatTickOffset"_id);
    const bool valid = offset && wat_offset>0 && process.copy(saved.weapon
        + SCHEMA("C_CSWeaponBase","m_fLastShotTime"_id), &fire_time, sizeof(fire_time))
        && process.copy(saved.weapon + offset, &punch, sizeof(punch))
        && process.copy(saved.weapon+wat_offset,&wat,sizeof(wat))
        && process.copy(saved.weapon+SCHEMA("C_CSWeaponBase","m_fLastShotTime"_id),&confirm_time,sizeof(confirm_time))
        && process.copy(saved.weapon+offset,&confirm_punch,sizeof(confirm_punch))
        && process.copy(saved.weapon+wat_offset,&confirm_wat,sizeof(confirm_wat))
        && fire_time==confirm_time && wat==confirm_wat
        && punch.x==confirm_punch.x && punch.y==confirm_punch.y && punch.z==confirm_punch.z
        && std::isfinite(wat) && std::isfinite(fire_time) && std::isfinite(punch.x)
        && std::isfinite(punch.y) && std::isfinite(punch.z);
    if (!valid) {
        write(std::format(R"({{"event":"consumed","id":{},"valid":false,"observed_tick":{},"latency_ms":{}}})",
            saved.id,observed_tick,latency));
        return;
    }
    const bool repeated_marker = last_marker.pawn == saved.pawn
        && last_marker.weapon == saved.weapon && last_marker.time == fire_time;
    last_marker = {saved.pawn, saved.weapon, fire_time};
    const auto clock = repeated_marker ? std::nullopt : infer_shot_clock(fire_time, wat);
    write(std::format(
        R"({{"event":"consumed","id":{},"valid":true,"observed_tick":{},"shots":{},"latency_ms":{},"fire_time":{},"fire_time_ticks":{},"saved_punch":[{},{},{}],"wat_tick_offset":{},"repeated_shot_marker":{},"clock_estimate_valid":{},"estimated_shot_ticks":{},"estimated_tick_min":{},"estimated_tick_max":{},"candidate_tick":{},"candidate_outside_estimate":{}}})",
        saved.id,observed_tick,shots,latency,fire_time,fire_time*64.0f,punch.x,punch.y,punch.z,wat,repeated_marker,clock.has_value(),
        clock ? clock->estimated_ticks : 0.0,clock ? clock->minimum_tick : -1,
        clock ? clock->maximum_tick : -1,saved.candidate_tick,
        clock ? clock->excludes(saved.candidate_tick) : false));
}
}
