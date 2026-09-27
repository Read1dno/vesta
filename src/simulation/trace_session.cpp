#include <stdafx.hpp>
#include <simulation/trace_session.hpp>
#include <simulation/shot_trace.hpp>
#include <app/workers.hpp>
#include <external/json.hpp>
#include <fstream>

namespace simulation::trace_session {
namespace {
std::jthread sampler;
void save(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream file(path, std::ios::trunc);
    file << value.dump(2) << '\n';
}
}
void initialize() {
    if (!shot_trace::enabled()) return;
    wchar_t directory[32768]{};
    const auto size = GetEnvironmentVariableW(L"VESTA_SEED_TRACE_DIR", directory, std::size(directory));
    if (!size || size >= std::size(directory)) return;
    const auto root = std::filesystem::path(directory).parent_path();
    try {
        std::filesystem::create_directories(root);
        save(root / "config-at-launch.json", config::build_config_json());
        save(root / "schema.json", {
            {"bullet_services", SCHEMA("C_CSPlayerPawn", "m_pBulletServices"_id)},
            {"server_hits", SCHEMA("CCSPlayer_BulletServices", "m_totalHitsOnServer"_id)},
            {"action_services", SCHEMA("CCSPlayerController", "m_pActionTrackingServices"_id)},
            {"round_damage", SCHEMA("CCSPlayerController_ActionTrackingServices", "m_flTotalRoundDamageDealt"_id)},
            {"round_kills", SCHEMA("CCSPlayerController_ActionTrackingServices", "m_iNumRoundKills"_id)},
            {"health", SCHEMA("C_BaseEntity", "m_iHealth"_id)},
            {"shots_fired", SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_id)},
            {"simulation_tick", SCHEMA("C_BaseEntity", "m_nSimulationTick"_id)},
            {"view", SCHEMA("C_BasePlayerPawn", "v_angle"_id)},
            {"ping", SCHEMA("CCSPlayerController", "m_iPing"_id)}
        });
        sampler = std::jthread([root](std::stop_token stop) {
            LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
            std::ofstream file(root / "scene.jsonl", std::ios::app);
            while (!stop.stop_requested() && file) {
                try {
                    if (std::filesystem::exists(root / "STOP")) break;
                    const auto map = app::workers::current_map();
                    const auto config = config::get_runtime_snapshot();
                    const nlohmann::json row{
                        {"event", "scene"}, {"qpc", shot_trace::qpc_now()}, {"qpc_frequency", frequency.QuadPart},
                        {"pid", GetCurrentProcessId()}, {"game_pid", app::context().process.process_id()},
                        {"map", map ? *map : ""}, {"collision_ready", game::collision().valid()},
                        {"menu_open", app::context().menu.is_open()},
                        {"combat_input_ready", app::context().overlay.combat_input_ready()},
                        {"seed_configured", config->combat.seed_trigger_configured()},
                        {"trigger_key", config->combat.global.triggerbot_key},
                        {"activation_mode", static_cast<int>(config->combat.global.triggerbot_activation_mode)}
                    };
                    file << row.dump() << '\n';
                    file.flush();
                } catch (...) { break; }
                for (int i = 0; i < 10 && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    } catch (...) { /* The diagnostic file never changes a firing decision. */ }
}
void shutdown() noexcept {
    if (sampler.joinable()) { sampler.request_stop(); sampler.join(); }
}
}
