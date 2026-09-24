#include "test_support.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path); VESTA_CHECK(file.good());
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv) {
    VESTA_CHECK(argc==2);
    const std::filesystem::path root(argv[1]);
    const auto workers=read(root/"src/app/workers.cpp");
    const auto aim=read(root/"src/features/aimbot/aimbot.cpp");
    const auto plan=read(root/"src/features/trigger/seed_plan.cpp");
    const auto weapon=read(root/"src/simulation/seed_weapon.cpp");
    VESTA_CHECK(workers.find("simulation::read_seed_tick(controller, binding.pawn)")!=std::string::npos);
    VESTA_CHECK(workers.find("runtime.sync_phase( tick, observed_at )")!=std::string::npos);
    VESTA_CHECK(plan.find("m_seed_memo_sequence")==std::string::npos);
    VESTA_CHECK(plan.find("return matches.first && matches.second")!=std::string::npos);
    VESTA_CHECK(plan.find("const auto recoil = simulation::read_recoil_state( pawn )")!=std::string::npos);
    VESTA_CHECK(plan.find("simulation::seed_window::resolve(")!=std::string::npos);
    VESTA_CHECK(plan.find("plan.current = { seed_tick, window.angles.front()")!=std::string::npos);
    VESTA_CHECK(plan.find("read_seed_punch( pawn )")==std::string::npos);
    VESTA_CHECK(plan.find("plan.next = { seed_tick, window.angles.back()")!=std::string::npos);
    VESTA_CHECK(aim.find("read_seed_punch( final_pawn )")==std::string::npos);
    VESTA_CHECK(aim.find("shots_fired > this->m_seed_last_shots")!=std::string::npos);
    VESTA_CHECK(aim.find("m_seed_targets.try_emplace")==std::string::npos);
    VESTA_CHECK(aim.find("m_seed_reaction.observe(hotkey_down, now)")!=std::string::npos);
    VESTA_CHECK(aim.find("m_seed_reaction.ready(now, cfg.reaction_time)")<aim.find("const auto plan = this->build_seed_plan"));
    VESTA_CHECK(workers.find("seed_schedule::poll_interval")!=std::string::npos);
    VESTA_CHECK(workers.find("prewake")==std::string::npos);
    VESTA_CHECK(aim.find("m_seed_receipt")==std::string::npos);
    VESTA_CHECK(aim.find("simulation::shot_trace::")!=std::string::npos);
    VESTA_CHECK(aim.find("bullet_count, weapon_ctx.pattern_seed")!=std::string::npos);
    VESTA_CHECK(weapon.find("ctx.velocity = velocity")!=std::string::npos);
    VESTA_CHECK(weapon.find("m_nSpreadSeed")!=std::string::npos);
    VESTA_CHECK(weapon.find("snapshot_epoch")==std::string::npos);
    std::cout<<"seed_integration: intra-tick retry, reaction, clock and trace wiring PASS\n";
}
