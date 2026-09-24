#include <stdafx.hpp>
#include <simulation/shot_trace.hpp>
#include <external/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
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
