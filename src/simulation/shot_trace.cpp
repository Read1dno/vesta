#include <stdafx.hpp>
#include <simulation/shot_trace.hpp>
#include <simulation/shot_clock_evidence.hpp>
#include <core/memory/compatibility.hpp>
#include <external/json.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

namespace simulation::shot_trace {
std::int64_t qpc_now() noexcept
{
    LARGE_INTEGER value{};
    return QueryPerformanceCounter(&value) ? value.QuadPart : 0;
}
namespace {
using json = nlohmann::json;
constexpr std::size_t queue_capacity = 4096;
constexpr std::size_t batch_capacity = 128;
constexpr std::uint64_t rotation_bytes = 64ull * 1024 * 1024;
constexpr std::uint64_t disk_reserve_bytes = 1024ull * 1024 * 1024;
constexpr std::array reason_names{"snapshot","policy","reaction","cooldown","pending","no_targets",
    "no_hit","recheck","auto_stop","changed_state","stale_delivery","input_failed","submitted",
    "collision","inactive","weapon","plan_unavailable","restricted"};
static_assert(reason_names.size() == static_cast<unsigned>(decision_reason::count));
struct cycle_payload { cycle_record value{}; decision_reason reason{}; };
struct decisions_payload {
    std::array<std::uint64_t, static_cast<unsigned>(decision_reason::count)> counts{};
    std::int64_t elapsed_ms{};
};
struct expired_payload { unsigned id{}; int observed_tick{}; };
struct weapon_payload { snapshot_record value{}; };
struct input_payload {
    unsigned id{};
    std::uintptr_t pawn{}, weapon{}, target{};
    int tick{}, next_tick{}, player_tick{}, sim_tick_at_press{-1}, clip{}, item{}, next_attack{};
    foundation::vec3 angles{}, next_angles{}, view{}, punch{}, velocity{};
    float inaccuracy{}, spread{}, recoil{}, fraction{};
    std::int64_t input_start_qpc{}, input_end_qpc{};
    int input_backend{};
    ray_sample ray{};
};
struct consumed_payload {
    unsigned id{};
    int observed_tick{}, shots{}, candidate_tick{};
    bool valid{}, repeated_marker{};
    float latency_ms{}, fire_time{}, wat{};
    foundation::vec3 punch{};
};
using payload = std::variant<decisions_payload, cycle_payload, expired_payload, weapon_payload,
    input_payload, consumed_payload, target_record>;
struct event_record {
    std::uint64_t sequence{}, cycle_id{};
    std::int64_t qpc{};
    DWORD tid{};
    payload data{};
};
static_assert(std::is_trivially_copyable_v<payload>);
static_assert(std::is_trivially_copyable_v<event_record>);
struct pending_shot {
    std::uintptr_t pawn{}, weapon{};
    unsigned id{};
    int candidate_tick{};
    std::uint64_t cycle_id{};
    std::int64_t qpc{};
};
struct observed_marker { std::uintptr_t pawn{}, weapon{}; float time{}; };
thread_local pending_shot pending;
thread_local observed_marker last_marker;
thread_local decision_scope* active_scope{};
struct channel {
    std::mutex mutex;
    std::condition_variable ready;
    std::array<event_record, queue_capacity> events;
    std::size_t read{}, count{}, high_water{};
    std::atomic<bool> accepting{}, stop_requested{}, writer_running{};
    std::atomic<std::uint64_t> sequence{}, accepted{}, written{}, dropped_busy{}, dropped_full{}, dropped_io{}, cycles{}, flushes{}, suppressed_idle{};
    std::atomic<int> io_errors{};
    std::thread writer;
    std::mutex shutdown_mutex;
    std::filesystem::path directory, stop_path;
    std::int64_t frequency{}, started_qpc{};
    DWORD pid{};
    bool explicit_directory{};
    std::uint64_t disk_available{};
};
channel& output() { static auto* value = new channel; return *value; }
std::uint64_t dropped(const channel& queue) noexcept
{
    return queue.dropped_busy.load(std::memory_order_relaxed)
        + queue.dropped_full.load(std::memory_order_relaxed)
        + queue.dropped_io.load(std::memory_order_relaxed);
}
template<class Value>
void enqueue(const Value& value, std::uint64_t cycle_id = 0, std::int64_t at = 0) noexcept
{
    auto& queue = output();
    if (!queue.accepting.load(std::memory_order_relaxed)) return;
    const auto counter = at ? at : qpc_now();
    const auto sequence = queue.sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto tid = GetCurrentThreadId();
    std::unique_lock lock(queue.mutex, std::try_to_lock);
    if (!lock) { queue.dropped_busy.fetch_add(1, std::memory_order_relaxed); return; }
    // The writer closes admission under this same lock before its final drain.
    if (!queue.accepting.load(std::memory_order_relaxed)) {
        queue.dropped_busy.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (queue.count == queue.events.size()) {
        queue.dropped_full.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto& record = queue.events[(queue.read + queue.count) % queue.events.size()];
    record.sequence = sequence;
    record.qpc = counter;
    record.tid = tid;
    record.cycle_id = cycle_id;
    record.data = value;
    ++queue.count;
    queue.high_water = std::max(queue.high_water, queue.count);
    queue.accepted.fetch_add(1, std::memory_order_relaxed);
    lock.unlock();
    queue.ready.notify_one();
}
json vector_json(const foundation::vec3& value) { return json::array({value.x, value.y, value.z}); }
json snapshot_json(const snapshot_record& value)
{
    return {{"pawn",value.pawn},{"controller",value.controller},{"weapon",value.weapon},{"target",value.target},
        {"tick",value.tick},{"simulation_tick",value.simulation_tick},{"tick_base",value.tick_base},{"ping",value.ping},
        {"clip",value.clip},{"item",value.item},{"next_attack",value.next_attack},{"host_mode",value.host_mode},
        {"weapon_ready",value.weapon_ready},{"reloading",value.reloading},{"inaccuracy",value.inaccuracy},
        {"spread",value.spread},{"recoil",value.recoil},{"eye",vector_json(value.eye)},
        {"view",vector_json(value.view)},{"velocity",vector_json(value.velocity)},{"accuracy_terms",value.accuracy_terms},
        {"weapon_vdata",value.weapon_vdata},{"fire_mode",value.fire_mode},{"pattern_seed",value.pattern_seed},
        {"num_bullets",value.num_bullets},{"flags",value.flags},{"flags_valid",value.flags_valid},{"ground_entity",value.ground_entity},
        {"walking",value.walking},{"on_ground",value.on_ground},{"current_time",value.current_time},
        {"wat",value.wat},{"last_shot_time",value.last_shot_time}};
}
json plan_json(const plan_record& value)
{
    return {{"tick",value.tick},{"next_tick",value.next_tick},{"phase",value.phase},
        {"fraction",value.fraction},{"next_fraction",value.next_fraction},{"source",vector_json(value.source)},
        {"hash",vector_json(value.hash)},{"next_hash",vector_json(value.next_hash)},
        {"direction",vector_json(value.direction)},{"next_direction",vector_json(value.next_direction)},
        {"punch",vector_json(value.punch)},{"next_punch",vector_json(value.next_punch)}};
}
json serialize(const event_record& event, const channel& queue)
{
    json out{{"seq",event.sequence},{"qpc",event.qpc},{"qpc_frequency",queue.frequency},
        {"pid",queue.pid},{"tid",event.tid},{"cycle_id",event.cycle_id},{"dropped_total",dropped(queue)}};
    std::visit([&](const auto& data) {
        using type = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<type, decisions_payload>) {
            out["event"]="decisions"; out["elapsed_ms"]=data.elapsed_ms;
            for (std::size_t i=0;i<reason_names.size();++i) out[reason_names[i]]=data.counts[i];
        } else if constexpr (std::is_same_v<type, cycle_payload>) {
            const auto& c=data.value;
            out["event"]="cycle";out["id"]=c.id;
            const auto reason=static_cast<unsigned>(data.reason);
            out["reason"]=reason<reason_names.size()?reason_names[reason]:"invalid";
            out["start_qpc"]=c.start_qpc;out["end_qpc"]=event.qpc;
            out["snapshot_end_qpc"]=c.snapshot_end_qpc;out["plan_qpc"]=c.plan_qpc;
            out["target_qpc"]=c.target_qpc;out["final_start_qpc"]=c.final_start_qpc;
            out["final_snapshot_qpc"]=c.final_snapshot_qpc;out["final_plan_qpc"]=c.final_plan_qpc;
            out["final_ray_qpc"]=c.final_ray_qpc;out["terminal_start_qpc"]=c.terminal_start_qpc;
            out["terminal_end_qpc"]=c.terminal_end_qpc;out["delivery_qpc"]=c.delivery_qpc;
            out["input_start_qpc"]=c.input_start_qpc;out["input_end_qpc"]=c.input_end_qpc;
            out["initial"]=snapshot_json(c.initial);out["final"]=snapshot_json(c.final);
            out["initial_plan"]=plan_json(c.initial_plan);out["final_plan"]=plan_json(c.final_plan);
            out["terminal_tick"]=c.terminal_tick;out["delivery_tick"]=c.delivery_tick;
            out["phase_tick"]=c.phase_tick;out["minimum_us"]=c.minimum_us;out["maximum_us"]=c.maximum_us;
            out["phase_us"]=c.phase_us;out["final_age_us"]=c.final_age_us;out["terminal_age_us"]=c.terminal_age_us;
            out["terminal_angles"]=vector_json(c.terminal_angles);out["terminal_punch"]=vector_json(c.terminal_punch);
            out["terminal_recoil"]=c.terminal_recoil;out["terminal_clip"]=c.terminal_clip;
            out["terminal_reloading"]=c.terminal_reloading;out["same_bucket"]=c.same_bucket;out["same_punch"]=c.same_punch;
            out["input_ok"]=c.input_ok;out["input_proxy"]=c.input_proxy;out["pre_cock"]=c.pre_cock;
            out["input_backend"]=c.input_backend;
            out["policy"]={{"min_damage",c.policy[0]},{"hitbox_parts",c.policy[1]},{"seed_mode",c.policy[2]},
                {"reaction_ms",c.policy[3]},{"predictive",c.policy[4]},{"wall_policy",c.policy[5]},
                {"lethal_only",c.policy[6]},{"activation_key",c.policy[7]}};
        } else if constexpr (std::is_same_v<type, expired_payload>) {
            out["event"]="expired";out["id"]=data.id;out["observed_tick"]=data.observed_tick;
        } else if constexpr (std::is_same_v<type, weapon_payload>) {
            const auto& s=data.value;
            out["event"]="weapon_snapshot";out["tick"]=s.tick;out["simulation_tick"]=s.simulation_tick;
            out["tick_base"]=s.tick_base;out["ping"]=s.ping;out["weapon"]=s.weapon;
            out["inaccuracy"]=s.inaccuracy;out["spread"]=s.spread;out["recoil"]=s.recoil;
            out["view"]=vector_json(s.view);out["velocity"]=vector_json(s.velocity);
        } else if constexpr (std::is_same_v<type, input_payload>) {
            out["event"]="input";out["id"]=data.id;out["pawn"]=data.pawn;out["weapon"]=data.weapon;
            out["tick"]=data.tick;out["next_tick"]=data.next_tick;out["angles"]=vector_json(data.angles);
            out["next_angles"]=vector_json(data.next_angles);out["inaccuracy"]=data.inaccuracy;
            out["spread"]=data.spread;out["recoil"]=data.recoil;out["target"]=data.target;
            out["fraction"]=data.fraction;out["view"]=vector_json(data.view);out["prepared_punch"]=vector_json(data.punch);
            out["velocity"]=vector_json(data.velocity);out["player_tick"]=data.player_tick;
            out["sim_tick_at_press"]=data.sim_tick_at_press;out["sim_tick_at_press_valid"]=data.sim_tick_at_press>=0;out["sim_tick_at_press_source"]="not_sampled";out["sim_tick_source"]="cycle_snapshot_no_extra_read";
            out["phase"]=data.tick==data.next_tick?"next":"current";out["clip"]=data.clip;
            out["item"]=data.item;out["next_attack"]=data.next_attack;out["seed"]=data.ray.seed;
            out["pellet"]=data.ray.pellet;out["ray_origin"]=vector_json(data.ray.origin);
            out["ray_direction"]=vector_json(data.ray.direction);
            out["input_start_qpc"]=data.input_start_qpc;out["input_end_qpc"]=data.input_end_qpc;
            out["input_backend"]=data.input_backend;
        } else if constexpr (std::is_same_v<type, consumed_payload>) {
            out["event"]="consumed";out["id"]=data.id;out["valid"]=data.valid;
            out["observed_tick"]=data.observed_tick;out["latency_ms"]=data.latency_ms;
            out["candidate_tick"]=data.candidate_tick;out["shots"]=data.shots;
            if(data.valid) {
                const auto clock=data.repeated_marker?std::nullopt:infer_shot_clock(data.fire_time,data.wat);
                out["fire_time"]=data.fire_time;out["fire_time_ticks"]=data.fire_time*64.0f;
                out["saved_punch"]=vector_json(data.punch);out["wat_tick_offset"]=data.wat;
                out["repeated_shot_marker"]=data.repeated_marker;out["clock_estimate_valid"]=clock.has_value();
                out["estimated_shot_ticks"]=clock?clock->estimated_ticks:0.0;
                out["estimated_tick_min"]=clock?clock->minimum_tick:-1;
                out["estimated_tick_max"]=clock?clock->maximum_tick:-1;
                out["candidate_outside_estimate"]=clock?clock->excludes(data.candidate_tick):false;
            }
        } else if constexpr (std::is_same_v<type, target_record>) {
            out["event"]="target";out["pawn"]=data.pawn;out["controller"]=data.controller;
            out["stage"]=data.stage;out["health"]=data.health;out["armor"]=data.armor;out["helmet"]=data.helmet;
            out["simulation_tick"]=data.simulation_tick;out["simulation_time"]=data.simulation_time;
            out["origin"]=vector_json(data.origin);out["velocity"]=vector_json(data.velocity);
            out["capsules"]=json::array();
            for(int i=0;i<std::clamp(data.count,0,static_cast<int>(data.capsules.size()));++i) {
                const auto& c=data.capsules[static_cast<std::size_t>(i)];
                out["capsules"].push_back({{"hitbox",c.hitbox},{"bone",c.bone},{"hitgroup",c.hitgroup},
                    {"start",vector_json(c.start)},{"end",vector_json(c.end)},{"radius",c.radius}});
            }
        }
    },event.data);
    return out;
}
void close_admission(channel& queue) noexcept
{
    std::lock_guard lock(queue.mutex);
    queue.accepting.store(false, std::memory_order_relaxed);
    queue.stop_requested.store(true, std::memory_order_relaxed);
}
void write_health(channel& queue, const char* status, const std::filesystem::path& log,
    std::uint64_t bytes, unsigned part, const char* stop_reason)
{
    std::size_t depth{},high_water{};
    { std::lock_guard lock(queue.mutex);depth=queue.count;high_water=queue.high_water; }
    json health{{"status",status},{"qpc",qpc_now()},{"qpc_frequency",queue.frequency},
        {"pid",queue.pid},{"writer_tid",GetCurrentThreadId()},{"started_qpc",queue.started_qpc},
        {"attempted",queue.sequence.load()},{"accepted",queue.accepted.load()},
        {"written",queue.written.load()},{"dropped_total",dropped(queue)},
        {"dropped_lock_busy",queue.dropped_busy.load()},{"dropped_queue_full",queue.dropped_full.load()},
        {"dropped_io",queue.dropped_io.load()},{"queue_depth",depth},{"queue_capacity",queue_capacity},
        {"queue_high_water",high_water},{"io_errors",queue.io_errors.load()},{"flushes",queue.flushes.load()},
        {"suppressed_idle_cycles",queue.suppressed_idle.load()},
        {"inactive_cycles_are_aggregated",true},
        {"cycle_policy","all_work_cycles; inactive/collision/pending before any snapshot are counted only"},
        {"segment",part},{"segment_bytes",bytes},{"rotate_bytes",rotation_bytes},
        {"disk_available_bytes",queue.disk_available},{"disk_reserve_bytes",disk_reserve_bytes},
        {"current_file",log.filename().string()},{"stop_reason",stop_reason},
        {"accuracy_terms",json::array({"penalty","turning","move","air","strafe","move_factor",
            "max_speed_0","max_speed_1","move_base_0","move_base_1","jump_initial","jump_apex",
            "walking","grounded","next_ratio","postpone_fraction","accuracy_view_pitch","accuracy_view_yaw","speed","velocity_length"})}};
    const auto temporary=queue.directory/(L"health."+std::to_wstring(queue.pid)+L".tmp");
    { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
      if(!file) {queue.io_errors.fetch_add(1);return;}
      file<<health.dump()<<'\n';file.flush();if(!file) {queue.io_errors.fetch_add(1);return;} }
    if(!MoveFileExW(temporary.c_str(),(queue.directory/L"health.json").c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) queue.io_errors.fetch_add(1);
}
void writer_main() noexcept
{
    auto& queue=output();
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    queue.writer_running.store(true);
    std::filesystem::path log;
    std::uint64_t bytes{};
    unsigned part{};
    const char* stop_reason="";
    try {
        std::ofstream file;
        const auto open_segment=[&] {
            if(!queue.explicit_directory && part==0) log=queue.directory/L"vesta.seed-trace.jsonl";
            else log=queue.directory/std::format(L"vesta.{}.{}.{:06}.jsonl",queue.pid,queue.started_qpc,part);
            file.open(log,std::ios::binary|std::ios::app);
            if(!file) throw std::runtime_error("open trace segment");
            std::error_code ec;bytes=std::filesystem::file_size(log,ec);if(ec)bytes=0;
        };
        open_segment();
        auto health_at=std::chrono::steady_clock::now();
        auto flush_at=health_at;
        auto marker_at=health_at-std::chrono::seconds(1);
        auto disk_at=marker_at;
        std::vector<event_record> batch;batch.reserve(batch_capacity);
        write_health(queue,"running",log,bytes,part,stop_reason);
        for(;;) {
            const auto marker_now=std::chrono::steady_clock::now();
            if(marker_now-disk_at>=std::chrono::seconds(1)) {
                std::error_code space_error;
                const auto space=std::filesystem::space(queue.directory,space_error);
                if(!space_error) {
                    queue.disk_available=space.available;
                    if(space.available<disk_reserve_bytes) {
                        close_admission(queue);stop_reason="disk_reserve";
                        std::ofstream stop(queue.stop_path,std::ios::app);stop<<"vesta disk_reserve\n";
                    }
                }
                disk_at=marker_now;
            }
            if(marker_now-marker_at>=std::chrono::milliseconds(100)) {
                std::error_code marker_error;
                if(std::filesystem::exists(queue.stop_path,marker_error)) {
                    close_admission(queue);if(!*stop_reason)stop_reason="STOP";
                }
                marker_at=marker_now;
            }
            batch.clear();
            {
                std::unique_lock lock(queue.mutex);
                queue.ready.wait_for(lock,std::chrono::milliseconds(100),[&] {
                    return queue.count || queue.stop_requested.load(std::memory_order_relaxed);
                });
                const auto take=std::min(queue.count,batch_capacity);
                for(std::size_t i=0;i<take;++i) {
                    batch.push_back(queue.events[queue.read]);
                    queue.read=(queue.read+1)%queue.events.size();--queue.count;
                }
            }
            for(const auto& event:batch) {
                auto line=serialize(event,queue).dump();
                if(bytes && bytes+line.size()+1>rotation_bytes) {
                    file.flush();if(!file)throw std::runtime_error("flush trace segment");
                    queue.flushes.fetch_add(1);file.close();++part;open_segment();
                }
                file<<line<<'\n';
                if(!file)throw std::runtime_error("write trace event");
                bytes+=line.size()+1;queue.written.fetch_add(1,std::memory_order_relaxed);
            }
            const auto now=std::chrono::steady_clock::now();
            if(now-flush_at>=std::chrono::milliseconds(100)) {
                file.flush();if(!file)throw std::runtime_error("flush trace batch");
                queue.flushes.fetch_add(1);flush_at=now;
            }
            if(now-health_at>=std::chrono::seconds(1)) {
                write_health(queue,"running",log,bytes,part,stop_reason);health_at=now;
            }
            if(queue.stop_requested.load(std::memory_order_relaxed)) {
                std::lock_guard lock(queue.mutex);
                if(!queue.count)break;
            }
        }
        file.flush();if(!file)throw std::runtime_error("flush final trace batch");
        queue.flushes.fetch_add(1);file.close();
        if(!*stop_reason)stop_reason="shutdown";
        write_health(queue,"stopped",log,bytes,part,stop_reason);
    } catch(...) {
        queue.io_errors.fetch_add(1);
        close_admission(queue);
        const auto accepted=queue.accepted.load(),written=queue.written.load();
        queue.dropped_io.fetch_add(accepted>written?accepted-written:0);
        try {write_health(queue,"error",log,bytes,part,"io_error");}catch(...){}
    }
    queue.writer_running.store(false);
}
}

decision_scope::decision_scope(decision_reason value,bool active) noexcept
    : reason(value), enabled(active && output().accepting.load(std::memory_order_relaxed))
{
    if(!enabled)return;
    record.id=output().cycles.fetch_add(1,std::memory_order_relaxed)+1;
    record.start_qpc=qpc_now();previous=active_scope;active_scope=this;
}
decision_scope::~decision_scope()
{
    if(!enabled)return;
    const auto at=qpc_now();
    const bool idle=(reason==decision_reason::inactive||reason==decision_reason::collision
        ||reason==decision_reason::pending)&&!record.initial.pawn&&!record.snapshot_end_qpc&&!record.plan_qpc;
    if(idle)output().suppressed_idle.fetch_add(1,std::memory_order_relaxed);
    else enqueue(cycle_payload{record,reason},record.id,at);
    active_scope=previous;
    struct counters {
        decisions_payload value{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    };
    static thread_local counters stats;
    const auto index=static_cast<unsigned>(reason);
    if(index<stats.value.counts.size())++stats.value.counts[index];
    const auto now=std::chrono::steady_clock::now();
    if(now-stats.start<std::chrono::seconds(1))return;
    stats.value.elapsed_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-stats.start).count();
    enqueue(stats.value,0,at);stats.value={};stats.start=now;
}
void initialize()
{
    static std::once_flag once;
    std::call_once(once,[] {
        auto& queue=output();
        try {
            const auto length=GetEnvironmentVariableW(L"VESTA_SEED_TRACE_DIR",nullptr,0);
            if(length) {
                std::wstring path(length,L'\0');
                const auto copied=GetEnvironmentVariableW(L"VESTA_SEED_TRACE_DIR",path.data(),length);
                if(!copied || copied>=length)return;
                path.resize(copied);queue.directory=path;queue.explicit_directory=true;
                if(!queue.directory.is_absolute())return;
            } else {
                wchar_t path[32768]{};
                const auto copied=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
                if(!copied || copied>=std::size(path))return;
                queue.directory=std::filesystem::path(path).parent_path();
            }
            std::filesystem::create_directories(queue.directory);
            queue.stop_path=queue.directory.parent_path()/L"STOP";
            LARGE_INTEGER frequency{};
            if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return;
            queue.frequency=frequency.QuadPart;queue.pid=GetCurrentProcessId();queue.started_qpc=qpc_now();
            // Resolve the existing receipt probe once, never on the input path.
            (void)game::compatibility::shot_punch_offset();
            queue.accepting.store(true);
            queue.writer=std::thread(writer_main);
            std::atexit(shutdown);
        } catch(...) {queue.accepting.store(false);queue.io_errors.fetch_add(1);}
    });
}
bool enabled() noexcept
{
    static const bool requested=[] {
        wchar_t value[2]{};
        const bool request=GetEnvironmentVariableW(L"VESTA_SEED_TRACE",value,2)==1&&value[0]==L'1';
        if(request)initialize();return request;
    }();
    return requested&&output().accepting.load(std::memory_order_relaxed);
}
void shutdown() noexcept
{
    auto& queue=output();
    close_admission(queue);queue.ready.notify_all();
    std::lock_guard lock(queue.shutdown_mutex);
    if(queue.writer.joinable()&&queue.writer.get_id()!=std::this_thread::get_id())queue.writer.join();
}
void submit_target(const target_record& value) noexcept {enqueue(value,value.cycle_id);}
void expired(int observed_tick)
{
    if(pending.id)enqueue(expired_payload{pending.id,observed_tick},pending.cycle_id);
}
void weapon_snapshot(int selected_tick,int simulation_tick,int tick_base,int ping,
    float inaccuracy,float spread,float recoil,foundation::vec3 view,
    foundation::vec3 velocity,std::uintptr_t weapon)
{
    if(!output().accepting.load(std::memory_order_relaxed))return;
    static thread_local int last_tick{-1};
    if(selected_tick==last_tick)return;last_tick=selected_tick;
    weapon_payload value{};
    auto& s=value.value;s.tick=selected_tick;s.simulation_tick=simulation_tick;s.tick_base=tick_base;
    s.ping=ping;s.inaccuracy=inaccuracy;s.spread=spread;s.recoil=recoil;s.view=view;s.velocity=velocity;s.weapon=weapon;
    enqueue(value,active_scope?active_scope->record.id:0);
}
void input(std::uintptr_t pawn,std::uintptr_t weapon,int tick,int next_tick,
    foundation::vec3 angles,foundation::vec3 next_angles,float inaccuracy,float spread,float recoil,
    std::uintptr_t target,float fraction,foundation::vec3 view,foundation::vec3 punch,
    foundation::vec3 velocity,int player_tick,int clip,int item,int next_attack,ray_sample ray)
{
    if(!output().accepting.load(std::memory_order_relaxed))return;
    const auto at=qpc_now();
    static thread_local unsigned next_id{};
    const auto cycle_id=active_scope?active_scope->record.id:0;
    pending={pawn,weapon,++next_id,tick,cycle_id,at};
    input_payload value{};value.id=pending.id;value.pawn=pawn;value.weapon=weapon;value.target=target;
    value.tick=tick;value.next_tick=next_tick;value.angles=angles;value.next_angles=next_angles;
    value.inaccuracy=inaccuracy;value.spread=spread;value.recoil=recoil;value.fraction=fraction;
    value.view=view;value.punch=punch;value.velocity=velocity;value.player_tick=player_tick;
    value.clip=clip;value.item=item;value.next_attack=next_attack;value.ray=ray;
    if(active_scope) {
        value.sim_tick_at_press=active_scope->record.final.simulation_tick;
        value.input_start_qpc=active_scope->record.input_start_qpc;
        value.input_end_qpc=active_scope->record.input_end_qpc;
        value.input_backend=active_scope->record.input_backend;
    }
    enqueue(value,cycle_id,at);
}
void consumed(std::uintptr_t pawn,int observed_tick,int shots)
{
    if(!output().accepting.load(std::memory_order_relaxed)||!pending.id||pawn!=pending.pawn)return;
    const auto at=qpc_now();
    const auto saved=pending;pending={};
    consumed_payload value{};value.id=saved.id;value.observed_tick=observed_tick;value.shots=shots;
    value.candidate_tick=saved.candidate_tick;
    value.latency_ms=static_cast<float>(double(at-saved.qpc)*1000.0/double(output().frequency));
    if(value.latency_ms>1500) {enqueue(value,saved.cycle_id,at);return;}
    float confirm_time{},confirm_wat{};
    foundation::vec3 confirm_punch{};
    auto& process=app::context().process;
    const auto offset=game::compatibility::shot_punch_offset();
    const auto wat_offset=SCHEMA("C_CSWeaponBase","m_flWatTickOffset"_id);
    value.valid=offset&&wat_offset>0&&process.copy(saved.weapon
        +SCHEMA("C_CSWeaponBase","m_fLastShotTime"_id),&value.fire_time,sizeof(value.fire_time))
        &&process.copy(saved.weapon+offset,&value.punch,sizeof(value.punch))
        &&process.copy(saved.weapon+wat_offset,&value.wat,sizeof(value.wat))
        &&process.copy(saved.weapon+SCHEMA("C_CSWeaponBase","m_fLastShotTime"_id),&confirm_time,sizeof(confirm_time))
        &&process.copy(saved.weapon+offset,&confirm_punch,sizeof(confirm_punch))
        &&process.copy(saved.weapon+wat_offset,&confirm_wat,sizeof(confirm_wat))
        &&value.fire_time==confirm_time&&value.wat==confirm_wat
        &&value.punch.x==confirm_punch.x&&value.punch.y==confirm_punch.y&&value.punch.z==confirm_punch.z
        &&std::isfinite(value.wat)&&std::isfinite(value.fire_time)&&std::isfinite(value.punch.x)
        &&std::isfinite(value.punch.y)&&std::isfinite(value.punch.z);
    if(value.valid) {
        value.repeated_marker=last_marker.pawn==saved.pawn&&last_marker.weapon==saved.weapon&&last_marker.time==value.fire_time;
        last_marker={saved.pawn,saved.weapon,value.fire_time};
    }
    enqueue(value,saved.cycle_id,at);
}
}
