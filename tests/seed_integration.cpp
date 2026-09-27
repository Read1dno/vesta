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
    VESTA_CHECK(plan.find("const auto recoil = snapshot_recoil")!=std::string::npos);
    VESTA_CHECK(plan.find("std::optional{ *snapshot_recoil } : simulation::read_recoil_state( pawn )")!=std::string::npos);
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
    VESTA_CHECK(weapon.find("native_velocity::read(process, pawn, velocity_offsets, ctx.velocity)")!=std::string::npos);
    VESTA_CHECK(weapon.find("ctx.seed_recoil = *recoil")!=std::string::npos);
    VESTA_CHECK(weapon.find("confirmed_tick_base != ctx.player_tick || confirmed_simulation_tick != ctx.seed_simulation_tick")!=std::string::npos);
    VESTA_CHECK(weapon.find("m_nSpreadSeed")!=std::string::npos);
    VESTA_CHECK(weapon.find("snapshot_epoch")==std::string::npos);
    const auto seed_begin = aim.find("void aimbot_t::seed_tick(");
    const auto final_start = aim.find("const auto final_now =", seed_begin);
    const auto final_controller = aim.find("const auto final_controller =", seed_begin);
    const auto final_targets = aim.find("const auto& final_players =", seed_begin);
    const auto final_velocity = aim.find("const auto final_velocity = final_ctx.velocity", seed_begin);
    const auto final_plan = aim.find("auto final_plan = this->build_seed_plan", seed_begin);
    VESTA_CHECK(final_start < final_controller && final_targets < final_velocity && final_velocity < final_plan);
    VESTA_CHECK(aim.find("false, &final_ctx.seed_recoil", final_plan)!=std::string::npos);
    VESTA_CHECK(aim.find("sync_seed_phase(seed_tick,") != std::string::npos);
    VESTA_CHECK(aim.find("current_phase(m_seed_phase_tick, final_tick, m_seed_phase_tick_at)") != std::string::npos);
    VESTA_CHECK(aim.find("shot_time > receipt.previous_shot_time") != std::string::npos);
    VESTA_CHECK(aim.find("const auto identity_changed =", seed_begin) < aim.find("if (m_seed_phase_receipt.weapon)", seed_begin));
    const auto reset_begin = aim.find("void aimbot_t::reset_seed(");
    const auto reset_end = aim.find("bool aimbot_t::seed_hot_path_requested", reset_begin);
    const auto reset = aim.substr(reset_begin, reset_end - reset_begin);
    VESTA_CHECK(reset.find("m_seed_phase_receipt = {}") != std::string::npos);
    VESTA_CHECK(reset.find("m_seed_network_window = {}") != std::string::npos);
    VESTA_CHECK(reset.find("m_seed_last_shots = -1") != std::string::npos);
    std::cout<<"seed_integration: intra-tick retry, reaction, clock and trace wiring PASS\n";
}
