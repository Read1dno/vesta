#include <stdafx.hpp>
#include <simulation/seed_diagnostics.hpp>
#include <simulation/shot_state.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/punch_snapshot.hpp>
#include <fstream>
#include <set>

namespace simulation::seed_diagnostics {
int report(const char* path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) return 3;
    out << std::unitbuf << "seed-snapshot-report v2\n";
    auto& process = app::context().process;
    if (!process.attach(L"cs2.exe") || !app::context().modules.discover(process)
        || !game::fields().initialize() || !app::context().addresses.initialize()) {
        out << "initialization=FAIL\n";
        return 2;
    }
    config::publish_runtime_snapshot();
    game::entity_index().refresh();
    game::local_player().update();
    const auto controller = process.load<std::uintptr_t>(app::context().addresses.local_player_controller);
    const auto binding = game::resolve_local_pawn(controller);
    if (!binding || binding.health <= 0) {
        out << "local_player=not-ready\n";
        return 2;
    }
    unsigned eligible{}, absent_prediction_tick{};
    for (std::uint32_t index = 1; index <= 64; ++index) {
        const auto other = game::entity_index().lookup_index(index);
        if (!other || other == controller) continue;
        const auto pawn = game::entity_index().lookup(process.load<std::uint32_t>(
            other + SCHEMA("CCSPlayerController","m_hPlayerPawn"_id)));
        if (!pawn || pawn == binding.pawn) continue;
        const auto health = process.load<int>(pawn + SCHEMA("C_BaseEntity","m_iHealth"_id));
        const auto team = process.load<int>(pawn + SCHEMA("C_BaseEntity","m_iTeamNum"_id));
        if (health <= 0 || health > 100 || (team != 2 && team != 3) || team == binding.team
            || process.load<bool>(pawn + SCHEMA("C_CSPlayerPawn","m_bGunGameImmunity"_id))) continue;
        int tick{}; float time{};
        if (!process.copy(pawn + SCHEMA("C_BaseEntity","m_nSimulationTick"_id), &tick, sizeof(tick))
            || !process.copy(pawn + SCHEMA("C_BaseEntity","m_flSimulationTime"_id), &time, sizeof(time))) continue;
        ++eligible;
        absent_prediction_tick += tick == -1;
        out << "candidate tick=" << tick << " simulation_time=" << time << " health=" << health << '\n';
    }
    out << "eligible_enemies=" << eligible << " absent_prediction_tick=" << absent_prediction_tick << '\n';

    std::vector<game::player_snapshot> targets;
    unsigned nonempty{}; std::size_t maximum{}; std::set<std::uintptr_t> unique;
    constexpr unsigned samples = 256;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < samples; ++i) {
        game::world().seed_players_into(targets, binding.pawn, controller, binding.team, false);
        nonempty += !targets.empty();
        maximum = std::max(maximum, targets.size());
        for (const auto& target : targets) unique.insert(target.pawn);
        ::Sleep(2);
    }
    simulation::ballistics_t ballistics;
    simulation::ballistics_t::context weapon{};
    foundation::vec3 velocity{};
    const bool weapon_valid = process.copy(binding.pawn + SCHEMA("C_BaseEntity","m_vecAbsVelocity"_id),
        &velocity,sizeof(velocity)) && ballistics.seed_weapon(binding.pawn,controller,weapon);
    out << "weapon.valid=" << weapon_valid << " ready=" << weapon.weapon_ready << " item=" << weapon.item_def_idx << '\n';

    const auto services = process.load<std::uintptr_t>(binding.pawn + SCHEMA("C_BasePlayerPawn","m_pWeaponServices"_id));
    const auto handle = process.load<std::uint32_t>(services + SCHEMA("CPlayer_WeaponServices","m_hActiveWeapon"_id));
    const auto weapon_address = game::entity_index().lookup(handle);
    const auto weapon_data = process.load<std::uintptr_t>(weapon_address + SCHEMA("C_BaseEntity","m_nSubclassID"_id) + 8);
    out << "weapon.services=" << services << " handle=" << handle << " address=" << weapon_address
        << " raw_slot=" << game::entity_index().lookup_index(handle & 0x7fff) << " vdata=" << weapon_data << '\n';
    if (weapon_data) {
        simulation::ballistics_t::inaccuracy_debug_data debug{};
        foundation::vec3 angles{};
        (void)process.copy(binding.pawn+SCHEMA("C_BasePlayerPawn","v_angle"_id),&angles,sizeof(angles));
        const auto accuracy = ballistics.get_inaccuracy(binding.pawn,weapon_address,weapon_data,0,angles,debug);
        const auto& data = ballistics.pen().get_weapon_data();
        out << "accuracy=" << accuracy << " grounded=" << debug.on_ground
            << " speed=" << debug.speed << " max_speed=" << debug.max_speed.first
            << " damage=" << data.damage << " penetration=" << data.penetration
            << " range=" << data.range << " range_modifier=" << data.range_modifier << '\n';
    }
    for (const auto name : {"weapon_accuracy_forcespread","weapon_air_spread_scale","sv_jump_impulse",
        "sv_strafing_inaccuracy_bias","sv_strafing_inaccuracy_scale"}) {
        float value{};
        const bool valid=game::variables().try_get(game::variables().find(identity::of(name)),value);
        out << name << " valid=" << valid << " value=" << value << '\n';
    }
    for (const auto name : {"weapon_accuracy_nospread","sv_strafing_inaccuracy_enabled"}) {
        bool value{};
        const bool valid=game::variables().try_get(game::variables().find(identity::of(name)),value);
        out << name << " valid=" << valid << " value=" << value << '\n';
    }
    const auto punch = simulation::read_cached_punch(process,binding.pawn,
        SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_id));
    out << "punch_snapshot.valid=" << punch.has_value();
    if (punch) out << " value=" << punch->x << ',' << punch->y << ',' << punch->z;
    out << '\n';
    std::uint32_t eflags{};
    foundation::vec3 network_velocity{};
    const bool velocity_metadata_valid = process.copy(binding.pawn + SCHEMA("C_BaseEntity","m_iEFlags"_id),
        &eflags,sizeof(eflags)) && process.copy(binding.pawn + SCHEMA("C_BaseEntity","m_vecVelocity"_id),
        &network_velocity,sizeof(network_velocity));
    out << "velocity_metadata.valid=" << velocity_metadata_valid << " abs_cache_dirty=" << bool(eflags & 0x1000)
        << " absolute=" << velocity.x << ',' << velocity.y << ',' << velocity.z
        << " network_raw=" << network_velocity.x << ',' << network_velocity.y << ',' << network_velocity.z << '\n';
    out << "seed_tick=" << simulation::read_seed_tick(controller,binding.pawn) << '\n';
    out << "samples=" << samples << " nonempty=" << nonempty << " max_targets=" << maximum
        << " unique_targets=" << unique.size() << '\n';
    out << "elapsed_ms=" << std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count() << '\n';
    out << "input=not-connected overlay=not-started\n";
    out << "target_reader=" << (nonempty ? "PASS" : "NO_TARGETS") << '\n';
    out << "shot_alignment=NOT_MEASURED measured_shots=0\n";
    const bool valid_snapshot = nonempty && weapon_valid && punch.has_value();
    out << "result=" << (valid_snapshot ? "SNAPSHOT_VALID" : "INCOMPLETE_SNAPSHOT") << '\n';
    return valid_snapshot ? 0 : 2;
}
}
