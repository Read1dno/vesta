#include <stdafx.hpp>
#include <simulation/shot_trace.hpp>
#include <external/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>

#include <unordered_set>

namespace {
using trace_json = nlohmann::json;
trace_json trace_read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string text;std::getline(file,text);
    return trace_json::parse(text,nullptr,false);
}
int longrun_test(const std::filesystem::path& root,std::string_view mode) {
    const auto directory=std::filesystem::absolute(root)/"vesta";
    std::filesystem::create_directories(directory);
    fixture::executable=directory/"trace-fixture.exe";
    SetEnvironmentVariableW(L"VESTA_SEED_TRACE",L"1");
    const bool rotation=mode=="rotation";
    SetEnvironmentVariableW(L"VESTA_SEED_TRACE_DIR",rotation?nullptr:directory.c_str());
    const auto original=directory/"vesta.seed-trace.jsonl";
    if(rotation) {
        std::ofstream file(original,std::ios::binary|std::ios::trunc);
        file<<"preserved-before-test\n";
        file.seekp(64ll*1024*1024-65);file.put('\n');
    }
    simulation::shot_trace::initialize();
    unsigned cases{},failed{};
    auto check=[&](bool good,const char* name){++cases;if(!good){++failed;std::cout<<"FAIL "<<name<<'\n';}};
    check(simulation::shot_trace::enabled(),"trace starts with explicit environment");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool overflow=mode=="overflow";
    const int count=rotation?32:overflow?30000:3000;
    for(int i=0;i<count;++i) {
        if(overflow||rotation) {
            simulation::shot_trace::weapon_snapshot(i+1,i,77,20,.08f,.002f,0,{1,2,0},{},0x20000);
        } else {
            simulation::shot_trace::decision_scope scope{simulation::shot_trace::decision_reason::no_hit,true};
            scope.record.initial.pawn=0x10000;scope.record.initial.weapon=0x20000;
            scope.record.initial.tick=i+1;scope.record.initial.inaccuracy=.08f;
            scope.record.snapshot_end_qpc=simulation::shot_trace::qpc_now();
            scope.record.input_start_qpc=scope.record.snapshot_end_qpc;
            scope.record.input_end_qpc=scope.record.snapshot_end_qpc;
            scope.record.input_backend=1;
            scope.record.policy[0]=30;
            if(i%500==0) {
                simulation::shot_trace::target_record target{};
                target.cycle_id=scope.record.id;target.pawn=0x30000;target.count=1;
                target.capsules[0]={0,6,1,{1,2,3},{1,2,5},3};
                simulation::shot_trace::submit_target(target);
                simulation::shot_trace::input(0x10000,0x20000,i+1,i+1,{1,2,0},{1,2,0},
                    .08f,.002f,0,0x30000,0,{1,2,0},{},{},i,30,7,0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if(!overflow&&!rotation) {
        std::ofstream marker(directory.parent_path()/"STOP");marker<<"standalone test\n";marker.close();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(simulation::shot_trace::enabled()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(!simulation::shot_trace::enabled(),"STOP disables logging without stopping the process");
    }
    simulation::shot_trace::shutdown();
    const auto health=trace_read_json(directory/"health.json");
    check(!health.is_discarded()&&health.value("status","")=="stopped","final health confirms drained writer");
    if(!health.is_discarded()) {
        check(health.value("accepted",0ull)==health.value("written",1ull),"all accepted events drained");
        check(health.value("attempted",0ull)==health.value("accepted",0ull)+health.value("dropped_total",0ull),
            "every attempted enqueue is accepted or explicitly dropped");
        check(health.value("queue_depth",1)==0&&health.value("queue_capacity",0)==4096,"bounded queue is empty after shutdown");
        check(health.value("flushes",0)>0,"batched writer flushed");
        if(overflow)check(health.value("dropped_total",0ull)>0,"overflow is observable rather than silent");
        else if(!rotation) {
            check(health.value("written",0)>2048,"logger continues beyond old 2048 event limit");
            check(health.value("stop_reason","")=="STOP","health reports STOP marker");
        }
    }
    std::uint64_t rows{},cycle_rows{},target_rows{},input_rows{},bad{},duplicates{};
    std::unordered_set<std::uint64_t> sequences;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        if(entry.path().extension()!=".jsonl"||(rotation&&entry.path()==original))continue;
        std::ifstream file(entry.path());
        for(std::string line;std::getline(file,line);) {
            const auto row=trace_json::parse(line,nullptr,false);
            if(row.is_discarded()){++bad;continue;}
            ++rows;
            if(!sequences.insert(row.value("seq",0ull)).second)++duplicates;
            if(row.value("qpc",0ll)<=0||row.value("qpc_frequency",0ll)<=0
                ||row.value("pid",0ul)==0||row.value("tid",0ul)==0)++bad;
            if(row.value("event","")=="cycle") {
                ++cycle_rows;
                if(row.value("end_qpc",0ll)<row.value("start_qpc",0ll)
                    ||row.at("initial").value("pawn",0ull)!=0x10000||!row.contains("policy"))++bad;
            }
            if(row.value("event","")=="target") {++target_rows;if(row.at("capsules").size()!=1)++bad;}
            if(row.value("event","")=="input") {
                ++input_rows;
                if(row.value("cycle_id",0ull)==0||row.value("input_start_qpc",0ll)<=0
                    ||row.value("input_backend",0)!=1)++bad;
            }
        }
    }
    check(rows>0&&bad==0,"all records have valid JSON and QPC/process metadata");
    check(duplicates==0,"accepted event sequences are unique");
    if(!rotation&&!overflow)check(cycle_rows>2048&&target_rows>0&&input_rows>0,"cycle, target and correlated input records survived long run");
    if(rotation) {
        std::ifstream file(original,std::ios::binary);std::string first;std::getline(file,first);
        check(first=="preserved-before-test"&&std::filesystem::file_size(original)==64ull*1024*1024-64,
            "existing trace was appended/rotated without truncation");
        check(health.value("segment",0)>0,"64MiB threshold starts a new segment");
    }
    std::cout<<"trace_"<<mode<<" cases="<<cases<<" failed="<<failed<<" rows="<<rows
        <<" dropped="<<health.value("dropped_total",0ull)<<'\n';
    return failed?1:0;
}
}

int main(int argc, char** argv) {
    if(argc==3)return longrun_test(argv[1],argv[2]);
    if (argc!=2) return 2;
    const auto directory=std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    const auto log=directory/"vesta.seed-trace.jsonl";
    std::filesystem::remove(log);
    fixture::executable=directory/"trace-fixture.exe";
    simulation::shot_trace::initialize();
    using fixture::sample;
    const std::array modes{sample::stable,sample::torn_time,sample::torn_wat,sample::torn_punch,
        sample::failed_read,sample::nan_wat,sample::nan_punch,sample::missing_wat,sample::zero_time,sample::boundary,sample::stable,sample::stable};
    for (auto mode : modes) {
        fixture::mode=mode;
        app::context().process={};
        simulation::shot_trace::input(0x10000,0x20000,mode==sample::boundary ? 99 : 101,102,
            {10,20,0},{10,20,0},.08f,.003f,0,0x30000,.5f,{10,20,0},{},{},100,30,7,99
#if defined(VESTA_TRACE_RAY_TEST)
            ,{{1,2,3},{0,1,0},123,0}
#endif
        );
        simulation::shot_trace::consumed(0x40000,103,1);
        simulation::shot_trace::consumed(0x10000,103,1);
        simulation::shot_trace::consumed(0x10000,104,2);
    }
    std::vector<nlohmann::json> rows;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now()<deadline) {
        rows.clear();
        std::ifstream file(log);
        for (std::string line;std::getline(file,line);) {
            auto value=nlohmann::json::parse(line,nullptr,false);
            if (!value.is_discarded()) rows.push_back(std::move(value));
        }
        if (rows.size()==modes.size()*2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    unsigned cases{},failed{};
    auto check=[&](bool okay,const char* name) {
        ++cases;
        if (!okay) { ++failed; std::cout<<"FAIL "<<name<<'\n'; }
    };
    check(rows.size()==modes.size()*2,"one acknowledgement per matching pawn/pending input");
    if (rows.size()==modes.size()*2) {
        check(!rows[21].value("repeated_shot_marker",true)
            && rows[23].value("repeated_shot_marker",false)
            && !rows[23].value("clock_estimate_valid",true),"do not treat a repeated marker as a new shot clock");
        std::int64_t prior{};
        for (const auto& row : rows) {
            const auto counter=row.value("qpc",std::int64_t{});
            check(counter>0 && counter>=prior && row.value("qpc_frequency",std::int64_t{})>0,
                "trace includes monotonic cross-process QPC");
            prior=counter;
        }
        const auto& stable=rows[1];
        check(stable.value("valid",false) && stable.value("clock_estimate_valid",false)
            && stable.value("candidate_tick",0)==101 && stable.value("candidate_outside_estimate",false)
            && stable.value("observed_tick",0)==103 && stable.value("estimated_shot_ticks",0.0)==100.25
            && stable.value("estimated_tick_min",0)==100 && stable.value("estimated_tick_max",0)==100,
            "distinguish observation time, candidate and inferred shot interval");
        check(!rows[3].value("valid",true),"reject torn shot timestamp");
        check(!rows[5].value("valid",true),"reject torn time offset");
        check(!rows[7].value("valid",true),"reject torn recoil");
        check(!rows[9].value("valid",true),"reject read failure");
        check(!rows[11].value("valid",true),"reject non-finite time offset");
        check(!rows[13].value("valid",true),"reject non-finite recoil");
        check(!rows[15].value("valid",true),"reject missing time-offset schema");
        check(rows[17].value("valid",false) && rows[17].contains("clock_estimate_valid")
            && !rows[17].value("clock_estimate_valid",true),"do not infer a tick from zero time");
        check(rows[19].value("clock_estimate_valid",false) && rows[19].value("estimated_tick_min",0)==99
            && rows[19].value("estimated_tick_max",0)==100 && !rows[19].value("candidate_outside_estimate",true),
            "preserve floating-point boundary ambiguity");
#if defined(VESTA_TRACE_RAY_TEST)
        check(rows[0].value("seed",0)==123 && rows[0].value("pellet",-1)==0
            && rows[0].at("ray_origin")==nlohmann::json::array({1,2,3})
            && rows[0].at("ray_direction")==nlohmann::json::array({0,1,0}),"record evaluated ray");
#else
        check(rows[0].contains("ray_origin") && rows[0].contains("ray_direction")
            && rows[0].contains("seed") && rows[0].contains("pellet"),"record ray fields");
#endif
    }
    using reason=simulation::shot_trace::decision_reason;
    for (unsigned i=0;i<static_cast<unsigned>(reason::count);++i) {
        simulation::shot_trace::decision_scope scope{static_cast<reason>(i),true};
    }
    { simulation::shot_trace::decision_scope disabled{reason::snapshot,false}; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    { simulation::shot_trace::decision_scope scope{reason::snapshot,true}; }
    nlohmann::json decision;
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (decision.is_null() && std::chrono::steady_clock::now()<limit) {
        std::ifstream file(log);
        for (std::string line;std::getline(file,line);) {
            auto value=nlohmann::json::parse(line,nullptr,false);
            if (!value.is_discarded() && value.value("event","")=="decisions") decision=std::move(value);
        }
        if (decision.is_null()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(!decision.is_null(),"decision counters flush as JSON");
    if (!decision.is_null()) {
        check(decision.value("snapshot",0)==2,"disabled scope does not count");
        for (auto name : {"policy","reaction","cooldown","pending","no_targets","no_hit","recheck",
                "auto_stop","changed_state","stale_delivery","input_failed","submitted","collision",
                "inactive","weapon","plan_unavailable","restricted"})
            check(decision.value(name,0)==1,name);
    }
    std::cout<<"trace_alignment cases="<<cases<<" failed="<<failed<<'\n';
    return failed ? 1 : 0;
}
