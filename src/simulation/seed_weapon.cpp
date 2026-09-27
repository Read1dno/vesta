#include <stdafx.hpp>
#include <system/performance.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/accuracy_snapshot.hpp>
#include <simulation/native_velocity.hpp>
#include <simulation/seed_timing.hpp>
#include <simulation/shot_state.hpp>

namespace simulation {
namespace {
struct snapshot_field {
    std::ptrdiff_t offset{};
    void* destination{};
    std::size_t size{};
};
template<class Value>
snapshot_field field(std::ptrdiff_t offset, Value& value) noexcept
{
    static_assert(std::is_trivially_copyable_v<Value>);
    return {offset, &value, sizeof(value)};
}
template<std::size_t Count>
bool read_fields(std::uintptr_t object, std::array<snapshot_field, Count> fields)
{
    if (!object) return false;
    constexpr std::size_t capacity = 384;
    constexpr std::ptrdiff_t maximum_gap = 64;
    for (const auto& value : fields)
        if (value.offset <= 0 || value.offset > 0x10000 || !value.destination
            || !value.size || value.size > capacity) return false;
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) {
        return a.offset < b.offset;
    });
    std::array<std::byte, capacity> bytes{};
    for (std::size_t first = 0; first < fields.size();) {
        const auto begin = fields[first].offset;
        auto end = begin + static_cast<std::ptrdiff_t>(fields[first].size);
        auto next = first + 1;
        while (next < fields.size()) {
            const auto extended = std::max(end, fields[next].offset
                + static_cast<std::ptrdiff_t>(fields[next].size));
            if (fields[next].offset - end > maximum_gap
                || extended - begin > static_cast<std::ptrdiff_t>(capacity)) break;
            end = extended;
            ++next;
        }
        if (!app::context().process.copy(object + begin, bytes.data(),
            static_cast<std::size_t>(end - begin))) return false;
        for (auto index = first; index < next; ++index)
            std::memcpy(fields[index].destination, bytes.data() + fields[index].offset - begin,
                fields[index].size);
        first = next;
    }
    return true;
}
}

bool ballistics_t::seed_weapon(std::uintptr_t pawn, std::uintptr_t controller,
    const foundation::vec3& velocity, context& output)
{
    VESTA_PERF_SCOPE(seed_weapon_read);
    (void)velocity; // API compatibility; accuracy uses velocity captured inside this snapshot.
    output = {};
    if (!pawn || !controller) return false;
    context ctx{};
    auto& process = app::context().process;
    const auto services_offset = SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id);
    const auto handle_offset = SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_id);
    const auto vdata_offset = SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8;
    const auto mode_offset = SCHEMA("C_CSWeaponBase", "m_weaponMode"_id);
    const auto clip_offset = SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id);
    const auto simulation_offset = SCHEMA("C_BaseEntity", "m_nSimulationTick"_id);
    const auto tick_base_offset = SCHEMA("CBasePlayerController", "m_nTickBase"_id);
    if (services_offset <= 0 || handle_offset <= 0 || mode_offset <= 0 || clip_offset <= 0
        || simulation_offset <= 0 || tick_base_offset <= 0) return false;
    std::uintptr_t services{};
    std::uint32_t handle{};
    if (!process.copy(pawn + services_offset, &services, sizeof(services)) || !services
        || !process.copy(services + handle_offset, &handle, sizeof(handle))) return false;
    ctx.weapon = game::entity_index().lookup(handle);
    if (!ctx.weapon || !process.copy(ctx.weapon + vdata_offset, &ctx.weapon_vdata,
        sizeof(ctx.weapon_vdata)) || !ctx.weapon_vdata) return false;
    if (!process.copy(ctx.weapon + SCHEMA("C_EconEntity", "m_AttributeManager"_id)
        + SCHEMA("C_AttributeContainer", "m_Item"_id)
        + SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_id), &ctx.item_def_idx,
        sizeof(ctx.item_def_idx))) return false;

    // Read immutable weapon data before opening the dynamic player/weapon epoch.
    std::array<float, 2> spread_values{}, inaccuracy_move{}, max_speed{};
    float jump_initial{}, jump_apex{};
    if (!read_fields(ctx.weapon_vdata, std::array{
        field(SCHEMA("CCSWeaponBaseVData", "m_nNumBullets"_id), ctx.num_bullets),
        field(SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id), ctx.weapon_type),
        field(SCHEMA("CCSWeaponBaseVData", "m_flSpread"_id), spread_values),
        field(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyMove"_id), inaccuracy_move),
        field(SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_id), max_speed),
        field(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_id), jump_initial),
        field(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_id), jump_apex)})) return false;
    if (ctx.num_bullets <= 0 || ctx.num_bullets > 32) return false;
    if (ctx.num_bullets > 1) {
        const auto offset = SCHEMA("CCSWeaponBaseVData", "m_nSpreadSeed"_id);
        if (offset <= 0 || !process.copy(ctx.weapon_vdata + offset,
            &ctx.pattern_seed, sizeof(ctx.pattern_seed))) return false;
    }
    m_pen.prepare(ctx.weapon_vdata, ctx.weapon);

    ctx.seed_snapshot_begin = std::chrono::steady_clock::now();
    if (!process.copy(pawn + simulation_offset, &ctx.seed_simulation_tick,
            sizeof(ctx.seed_simulation_tick))
        || !process.copy(controller + tick_base_offset, &ctx.player_tick, sizeof(ctx.player_tick))
        || ctx.seed_simulation_tick < 0 || ctx.player_tick <= 0) return false;

    int weapon_mode{};
    float turning{}, accuracy_penalty{};
    if (!read_fields(ctx.weapon, std::array{
        field(mode_offset, weapon_mode),
        field(SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_id), ctx.recoil_index),
        field(SCHEMA("C_CSWeaponBase", "m_flTurningInaccuracy"_id), turning),
        field(SCHEMA("C_CSWeaponBase", "m_fAccuracyPenalty"_id), accuracy_penalty),
        field(SCHEMA("C_CSWeaponBase", "m_bInReload"_id), ctx.is_reloading),
        field(clip_offset, ctx.clip),
        field(SCHEMA("C_CSWeaponBase", "m_nPostponeFireReadyTicks"_id), ctx.postpone_fire_ready_tick),
        field(SCHEMA("C_CSWeaponBase", "m_flPostponeFireReadyFrac"_id), ctx.postpone_fire_ready_fraction),
        field(SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_id), ctx.next_primary_attack_tick),
        field(SCHEMA("C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_id), ctx.next_primary_attack_ratio)})) return false;

    foundation::vec3 accuracy_view{};
    if (!read_fields(pawn, std::array{
        field(SCHEMA("C_CSPlayerPawn", "m_bIsWalking"_id), ctx.is_walking),
        field(SCHEMA("C_BaseEntity", "m_hGroundEntity"_id), ctx.ground_entity),
        field(SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_id), accuracy_view),
        field(SCHEMA("C_BasePlayerPawn", "v_angle"_id), ctx.seed_view_angles)})) return false;
    const native_velocity::offsets velocity_offsets{
        SCHEMA("C_BaseEntity", "m_iEFlags"_id),
        SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id),
        SCHEMA("C_BaseEntity", "m_vecVelocity"_id),
        SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id),
        SCHEMA("CGameSceneNode", "m_pParent"_id)};
    if (!native_velocity::read(process, pawn, velocity_offsets, ctx.velocity)) return false;
    const auto recoil = read_recoil_state(pawn);
    if (!recoil) return false;
    ctx.seed_recoil = *recoil;

    std::uintptr_t confirmed_services{}, confirmed_vdata{};
    std::uint32_t confirmed_handle{};
    int confirmed_mode{}, confirmed_clip{}, confirmed_tick_base{}, confirmed_simulation_tick{};
    if (!process.copy(pawn + services_offset, &confirmed_services, sizeof(confirmed_services))
        || confirmed_services != services
        || !process.copy(services + handle_offset, &confirmed_handle, sizeof(confirmed_handle))
        || confirmed_handle != handle
        || !process.copy(ctx.weapon + vdata_offset, &confirmed_vdata, sizeof(confirmed_vdata))
        || confirmed_vdata != ctx.weapon_vdata
        || !read_fields(ctx.weapon, std::array{field(mode_offset, confirmed_mode), field(clip_offset, confirmed_clip)})
        || confirmed_mode != weapon_mode || confirmed_clip != ctx.clip
        || !process.copy(controller + tick_base_offset, &confirmed_tick_base, sizeof(confirmed_tick_base))
        || !process.copy(pawn + simulation_offset, &confirmed_simulation_tick, sizeof(confirmed_simulation_tick))) return false;
    ctx.seed_snapshot_end = std::chrono::steady_clock::now();
    // The two clocks need only remain unchanged; their numerical values need not agree.
    if (confirmed_tick_base != ctx.player_tick || confirmed_simulation_tick != ctx.seed_simulation_tick
        || !shot_model::finite(accuracy_view) || !shot_model::finite(ctx.seed_view_angles)
        || std::abs(ctx.seed_view_angles.x) > 89.0f || std::abs(ctx.seed_view_angles.y) > 360.0f
        || !std::isfinite(ctx.recoil_index) || ctx.recoil_index < 0.0f) return false;

    constexpr std::uint16_t revolver_id{64};
    ctx.fire_mode = ctx.item_def_idx == revolver_id ? 0 : weapon_mode;
    ctx.spread = spread_values[ctx.fire_mode == 1 ? 1 : 0];
    ctx.velocity_length = ctx.velocity.length_2d();
    ctx.on_ground = ctx.ground_entity != 0xffffffffu && ctx.ground_entity != 0xfffffffeu
        && game::entity_index().lookup(ctx.ground_entity) != 0;
    inaccuracy_debug_data accuracy{};
    accuracy.fire_mode = ctx.fire_mode;
    accuracy.velocity = ctx.velocity;
    accuracy.is_walking = ctx.is_walking;
    accuracy.on_ground = ctx.on_ground;
    accuracy.inaccuracy_move = {inaccuracy_move[0], inaccuracy_move[1]};
    accuracy.max_speed = {max_speed[0], max_speed[1]};
    accuracy.inaccuracy_jump_initial = jump_initial;
    accuracy.inaccuracy_jump_apex = jump_apex;
    accuracy.turning_inaccuracy = turning;
    accuracy.accuracy_penalty = accuracy_penalty;
    accuracy.eye_angles = accuracy_view;
    ctx.inaccuracy = detail::accuracy_from_snapshot(accuracy);
    ctx.debug = accuracy;
    ctx.weapon_ready = !ctx.is_reloading && ctx.clip != 0
        && seed_timing::primary_ready(ctx.next_primary_attack_tick,
            ctx.next_primary_attack_ratio, ctx.player_tick);
    const auto& weapon_data = m_pen.get_weapon_data();
    ctx.valid = std::isfinite(ctx.spread) && ctx.spread >= 0.0f
        && std::isfinite(ctx.inaccuracy) && ctx.inaccuracy >= 0.0f
        && ctx.item_def_idx > 0 && weapon_data.damage > 0.0f
        && weapon_data.penetration > 0.0f && weapon_data.range > 0.0f
        && weapon_data.range_modifier > 0.0f && weapon_data.range_modifier <= 1.0f;
    if (!ctx.valid) return false;
    ctx.seed_snapshot_valid = true;
    output = ctx;
    return true;
}

bool ballistics_t::seed_weapon(std::uintptr_t pawn, std::uintptr_t controller, context& output)
{
    return seed_weapon(pawn, controller, foundation::vec3{}, output);
}
}
