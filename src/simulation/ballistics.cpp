#include <stdafx.hpp>
#include <simulation/aim_parts.hpp>
#include <game/memory/remote_pointer.hpp>
#include <game/memory/bound_services.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/spread_sampler.hpp>
#include <simulation/accuracy_snapshot.hpp>
#include <game/memory/compatibility.hpp>
#include <foundation/math/ray_capsule.hpp>

namespace simulation
{

namespace detail
{

template<std::size_t capacity>
class remote_field_snapshot
{
public:
    remote_field_snapshot(std::uintptr_t object, std::int32_t first, std::int32_t end) noexcept
        : m_object(object)
        , m_first(first)
    {
        if (!object || first < 0 || end <= first)
        {
            return;
        }

        m_size = static_cast<std::size_t>(end - first);
        if (m_size > m_bytes.size())
        {
            m_size = 0;
            return;
        }

        m_valid =
            game::bound_memory().process.copy(object + static_cast<std::uintptr_t>(first), m_bytes.data(), m_size);
    }

    template<typename value_t>
    [[nodiscard]] value_t load(std::int32_t offset) const noexcept
    {
        if (m_valid && offset >= m_first)
        {
            const auto relative = static_cast<std::size_t>(offset - m_first);
            if (relative <= m_size && sizeof(value_t) <= m_size - relative)
            {
                value_t value{};
                std::memcpy(&value, m_bytes.data() + relative, sizeof(value));
                return value;
            }
        }

        return {};
    }

    [[nodiscard]] bool valid() const noexcept { return m_valid; }

private:
    std::array<std::byte, capacity> m_bytes{};
    std::uintptr_t m_object{};
    std::int32_t m_first{};
    std::size_t m_size{};
    bool m_valid{};
};

[[nodiscard]] static std::optional<shotgun_pattern> read_shotgun_pattern(int item)
{
    const auto map = game::compatibility::spread_patterns();
    if (!map)
        return std::nullopt;
    std::array<std::byte, 32> header{};
    if (!game::bound_memory().process.copy(map, header.data(), header.size()))
        return std::nullopt;
    int size{}, capacity{}, allocated{};
    std::uintptr_t nodes{};
    std::memcpy(&size, header.data() + 8, 4);
    std::memcpy(&capacity, header.data() + 12, 4);
    std::memcpy(&nodes, header.data() + 16, 8);
    std::memcpy(&allocated, header.data() + 28, 4);
    capacity &= 0x7fffffff;
    if (size <= 0 || capacity < size || capacity > 4096 || allocated <= 0 || allocated > capacity ||
        !game::is_user_pointer(nodes))
        return std::nullopt;
    // One snapshot replaces a process read for every tree node and every pellet.
    static thread_local std::vector<std::byte> entries;
    entries.resize(static_cast<std::size_t>(allocated) * 32);
    if (!game::bound_memory().process.copy(nodes, entries.data(), entries.size()))
        return std::nullopt;
    for (int i = 0; i < allocated; ++i)
    {
        std::uint16_t key{};
        std::uintptr_t definition{};
        std::memcpy(&key, entries.data() + i * 32 + 16, 2);
        if (key != item)
            continue;
        std::memcpy(&definition, entries.data() + i * 32 + 24, 8);
        if (!game::is_user_pointer(definition))
            return std::nullopt;
        std::array<std::byte, 0x201> data{};
        if (!game::bound_memory().process.copy(definition + 0x404, data.data(), data.size()))
            return std::nullopt;
        shotgun_pattern result{};
        static_assert(sizeof(result.angle_radius) == 0x200);
        std::memcpy(result.angle_radius.data(), data.data(), 0x200);
        result.random = data[0x200] != std::byte{};
        return result;
    }
    return std::nullopt;
}

} // namespace detail

ballistics_t::context ballistics_t::shot_ctx(float fire_time, std::uintptr_t weapon, float post_shot_time) const
{
    if (!std::isfinite(fire_time) || fire_time <= 0.0f || !weapon)
    {
        return {};
    }

    std::shared_lock lock(m_ctx_mutex);
    context best{};
    auto best_time = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < m_ctx_history_count; ++index)
    {
        const auto slot = (m_ctx_history_head + m_ctx_history.size() - 1 - index) % m_ctx_history.size();
        const auto& sample = m_ctx_history[slot];
        if (!sample.valid || sample.weapon != weapon || !std::isfinite(sample.current_time) ||
            sample.current_time > fire_time + 0.020f)
        {
            continue;
        }

        if (!std::isfinite(sample.last_fired_weapon_time) || sample.last_fired_weapon_time >= fire_time - 0.0001f)
        {
            continue;
        }

        if (std::isfinite(post_shot_time) && std::isfinite(sample.last_shot_time) &&
            sample.last_shot_time >= post_shot_time - 0.0001f)
        {
            continue;
        }

        if (sample.current_time > best_time)
        {
            best = sample;
            best_time = sample.current_time;
        }
    }

    return best;
}

void ballistics_t::tick()
{
    context ctx{};
    const auto local_pawn = game::local_player().pawn();
    const auto now = std::chrono::steady_clock::now();
    const auto invalidate = [this]()
    {
        std::unique_lock lock(m_ctx_mutex);
        m_ctx = {};
        m_ctx_pawn = {};
        m_ctx_weapon = {};
        m_ctx_weapon_handle = {};
        m_ctx_published_at = {};
    };
    const auto reject_transient =
        [this, local_pawn, now](const std::uintptr_t observed_weapon = 0, const std::uint32_t observed_handle = 0)
    {
        std::unique_lock lock(m_ctx_mutex);
        const auto same_identity = m_ctx.valid && m_ctx_pawn == local_pawn &&
                                   (!observed_weapon || m_ctx_weapon == observed_weapon) &&
                                   (!observed_handle || m_ctx_weapon_handle == observed_handle);
        const auto recent = m_ctx_published_at.time_since_epoch().count() != 0 &&
                            now - m_ctx_published_at <= std::chrono::milliseconds(100);
        if (same_identity && recent)
            return;
        m_ctx = {};
        m_ctx_pawn = {};
        m_ctx_weapon = {};
        m_ctx_weapon_handle = {};
        m_ctx_published_at = {};
    };

    if (!local_pawn)
    {
        invalidate();
        return;
    }

    std::uintptr_t weapon_services{};
    if (!game::bound_memory().process.copy(local_pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id),
                                           &weapon_services, sizeof(weapon_services)) ||
        !weapon_services)
    {
        reject_transient();
        return;
    }

    std::uint32_t weapon_handle{};
    if (!game::bound_memory().process.copy(weapon_services + SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_id),
                                           &weapon_handle, sizeof(weapon_handle)) ||
        !weapon_handle)
    {
        reject_transient();
        return;
    }

    ctx.weapon = game::entity_index().lookup(weapon_handle);
    if (!ctx.weapon)
    {
        reject_transient(0, weapon_handle);
        return;
    }
    {
        std::unique_lock lock(m_ctx_mutex);
        if (m_ctx.valid &&
            (m_ctx_pawn != local_pawn || m_ctx_weapon != ctx.weapon || m_ctx_weapon_handle != weapon_handle))
        {
            m_ctx = {};
            m_ctx_pawn = {};
            m_ctx_weapon = {};
            m_ctx_weapon_handle = {};
            m_ctx_published_at = {};
        }
    }

    if (!game::bound_memory().process.copy(ctx.weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8,
                                           &ctx.weapon_vdata, sizeof(ctx.weapon_vdata)) ||
        !ctx.weapon_vdata)
    {
        reject_transient(ctx.weapon, weapon_handle);
        return;
    }

    bool reads_valid{true};
    const auto read_required = [&](const std::uintptr_t address, auto& value)
    {
        if (!game::bound_memory().process.copy(address, &value, sizeof(value)))
        {
            value = {};
            reads_valid = false;
        }
    };

    read_required(ctx.weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_id), ctx.weapon_type);
    read_required(ctx.weapon + SCHEMA("C_EconEntity", "m_AttributeManager"_id) +
                      SCHEMA("C_AttributeContainer", "m_Item"_id) +
                      SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_id),
                  ctx.item_def_idx);
    read_required(ctx.weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_nNumBullets"_id), ctx.num_bullets);
    if (ctx.num_bullets > 1)
    {
        const auto offset = SCHEMA("CCSWeaponBaseVData", "m_nSpreadSeed"_id);
        if (offset <= 0)
            reads_valid = false;
        else
            read_required(ctx.weapon_vdata + offset, ctx.pattern_seed);
    }
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_weaponMode"_id), ctx.fire_mode);

    std::uintptr_t global_vars{};
    read_required(game::bound_addresses().global_vars, global_vars);
    float sample_time{}, sample_fraction{};
    int sample_tick{};
    if (global_vars)
    {
        read_required(global_vars + 0x30, sample_time);
        read_required(global_vars + 0x44, sample_tick);
        read_required(global_vars + 0x50, sample_fraction);
    }
    else
        reads_valid = false;

    foundation::vec3 view_angles{}, unused_origin{};
    if (!game::camera().sample(unused_origin, view_angles))
        reads_valid = false;
    if (!reads_valid)
    {
        reject_transient(ctx.weapon, weapon_handle);
        return;
    }
    ctx.inaccuracy = get_inaccuracy(local_pawn, ctx.weapon, ctx.weapon_vdata, ctx.debug);
    ctx.fire_mode = ctx.debug.fire_mode;
    ctx.spread = get_spread(ctx.weapon_vdata, ctx.fire_mode);
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_id), ctx.recoil_index);
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_bInReload"_id), ctx.is_reloading);

    ctx.current_time = sample_time;
    // CGlobalVarsBase::tickcount and the interpolation fraction used by
    // BacktrackLocalPlayer's firing timestamp (client+0xA56230).
    ctx.global_tick = sample_tick;
    ctx.global_tick_fraction = sample_fraction;

    read_required(ctx.weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flCycleTime"_id), ctx.cycle_time);
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_fLastShotTime"_id), ctx.last_shot_time);
    read_required(local_pawn + SCHEMA("C_CSPlayerPawn", "m_flLastFiredWeaponTime"_id), ctx.last_fired_weapon_time);
    read_required(ctx.weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_bIsFullAuto"_id), ctx.is_full_auto);
    read_required(ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_id),
                  ctx.next_primary_attack_tick);
    read_required(ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_id),
                  ctx.next_primary_attack_ratio);
    read_required(ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_id),
                  ctx.next_secondary_attack_tick);
    read_required(ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_flNextSecondaryAttackTickRatio"_id),
                  ctx.next_secondary_attack_ratio);
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_flWatTickOffset"_id), ctx.wat_tick_offset);

    // Pre-shot player state for accurate inaccuracy recalculation
    read_required(local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id), ctx.velocity);
    ctx.velocity_length = ctx.velocity.length_2d();
    read_required(local_pawn + SCHEMA("C_BaseEntity", "m_hGroundEntity"_id), ctx.ground_entity);
    ctx.on_ground = ctx.ground_entity != 0xffffffffu;
    read_required(local_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsWalking"_id), ctx.is_walking);

    const auto controller = game::local_player().controller();
    std::uint32_t current_tick_raw{};
    if (controller)
        read_required(controller + SCHEMA("CBasePlayerController", "m_nTickBase"_id), current_tick_raw);
    else
        reads_valid = false;
    const auto current_tick = static_cast<int>(current_tick_raw);
    ctx.player_tick = current_tick;
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_nPostponeFireReadyTicks"_id), ctx.postpone_fire_ready_tick);
    read_required(ctx.weapon + SCHEMA("C_CSWeaponBase", "m_flPostponeFireReadyFrac"_id),
                  ctx.postpone_fire_ready_fraction);
    float next_player_attack{};
    read_required(weapon_services + SCHEMA("CCSPlayer_WeaponServices", "m_flNextAttack"_id), next_player_attack);
    int clip{};
    read_required(ctx.weapon + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id), clip);
    ctx.clip = clip;
    const auto primary_ready =
        ctx.next_primary_attack_tick < current_tick ||
        (ctx.next_primary_attack_tick == current_tick && ctx.next_primary_attack_ratio <= 0.001f);

    ctx.weapon_ready = !ctx.is_reloading && clip != 0 && primary_ready && next_player_attack <= ctx.current_time;
    if (ctx.weapon_type == game::rules::precision)
        read_required(local_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_id), ctx.is_scoped);

    float final_time{};
    int final_tick{};
    read_required(global_vars + 0x30, final_time);
    read_required(global_vars + 0x44, final_tick);
    if (!reads_valid || final_time != sample_time || final_tick != sample_tick)
    {
        reject_transient(ctx.weapon, weapon_handle);
        return;
    }

    m_pen.prepare(ctx.weapon_vdata, ctx.weapon);
    const auto& weapon_data = m_pen.get_weapon_data();
    ctx.valid = std::isfinite(ctx.inaccuracy) && ctx.inaccuracy >= 0.0f && std::isfinite(ctx.spread) &&
                ctx.spread >= 0.0f && std::isfinite(ctx.recoil_index) && ctx.recoil_index >= 0.0f &&
                std::isfinite(ctx.current_time) && ctx.current_time > 0.0f && ctx.global_tick > 0 &&
                ctx.player_tick > 0 && ctx.item_def_idx > 0 && ctx.num_bullets > 0 && ctx.num_bullets <= 32 &&
                std::isfinite(ctx.cycle_time) && ctx.cycle_time > 0.0f && std::isfinite(ctx.velocity.x) &&
                std::isfinite(ctx.velocity.y) && std::isfinite(ctx.velocity.z) && weapon_data.damage > 0.0f &&
                weapon_data.penetration > 0.0f && weapon_data.range > 0.0f && weapon_data.range_modifier > 0.0f &&
                weapon_data.range_modifier <= 1.0f;
    if (!ctx.valid)
    {
        reject_transient(ctx.weapon, weapon_handle);
        return;
    }

    std::unique_lock lock(m_ctx_mutex);
    m_ctx = ctx;
    m_ctx_pawn = local_pawn;
    m_ctx_weapon = ctx.weapon;
    m_ctx_weapon_handle = weapon_handle;
    m_ctx_published_at = now;
    m_ctx_history[m_ctx_history_head] = ctx;
    m_ctx_history_head = (m_ctx_history_head + 1) % m_ctx_history.size();
    m_ctx_history_count = std::min(m_ctx_history_count + 1, m_ctx_history.size());
}


float ballistics_t::estimate_hit_probability(const foundation::vec3& eye_pos, const foundation::vec3& aim_angle,
                                             const game::player_snapshot& target,
                                             const game::skeleton_reader::data& bones, int hitbox_parts) const
{
    // A copy under the context lock: other threads (seed trigger, aim) publish a new context concurrently.
    const auto ctx = this->ctx();
    const auto total_spread = ctx.spread + ctx.inaccuracy;

    if (total_spread < 0.0001f)
    {
        return 1.0f;
    }

    const auto range = m_pen.get_weapon_data().range;
    if (range <= 0.0f)
    {
        return 0.0f;
    }

    struct capsule_t
    {
        foundation::vec3 start;
        foundation::vec3 end;
        float radius;
    };

    std::array<capsule_t, 32> capsules;
    auto capsule_count{0};

    for (const auto& hb : target.hitboxes)
    {
        if (hb.index < 0 || hb.bone < 0)
        {
            continue;
        }
        const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(hb.index);
        const auto part = aim_part_of(hb, hitgroup);
        if (!(hitbox_parts & part) || capsule_count >= static_cast<int>(capsules.size()))
        {
            continue;
        }

        // mins/maxs are the two capsule axis points in bone space.
        const auto placed = game::place_hitbox(bones.bones[hb.bone], hb);
        capsules[capsule_count++] = {placed.start, placed.end, placed.radius};
    }

    if (capsule_count == 0)
    {
        return 0.0f;
    }

    foundation::vec3 forward{}, right{}, up{};
    aim_angle.to_directions(&forward, &right, &up);

    constexpr auto samples{256};
    auto hits{0};

    for (int seed = 0; seed < samples; ++seed)
    {
        const auto spread = sample_spread_offset(seed, ctx.inaccuracy, ctx.spread, ctx.recoil_index, ctx.item_def_idx,
                                                 ctx.fire_mode, ctx.num_bullets, 0, ctx.pattern_seed);
        // See the note in legit.cpp: the horizontal term is added, not subtracted.
        // Verified against the client's own impact queue, not from a decompile.
        const auto direction = (forward + right * spread.x + up * spread.y).normalized();

        for (int i = 0; i < capsule_count; ++i)
        {
            if (ray_hits_capsule(eye_pos, direction, capsules[i].start, capsules[i].end, capsules[i].radius))
            {
                ++hits;
                break;
            }
        }

        const auto remaining = samples - (seed + 1);
        if (hits + remaining < samples / 4)
        {
            break;
        }
    }

    return static_cast<float>(hits) / static_cast<float>(samples);
}

std::uint32_t ballistics_t::derive_command_seed(const foundation::vec3& angles, int tick) const
{
    return shot_model::seed(angles, tick);
}

foundation::vec2 ballistics_t::sample_spread_offset(int seed, float inaccuracy, float spread, float recoil_index,
                                                    int item_def_idx, int weapon_mode, int num_bullets,
                                                    int bullet_index, std::optional<int> pattern_seed) const
{
    detail::spread_parameters parameters{seed,         inaccuracy,  spread,     recoil_index,
                                         item_def_idx, weapon_mode, num_bullets};
    parameters.patterns = game::variables().get<bool>(CONVAR("weapon_accuracy_shotgun_spread_patterns"_id));
    parameters.force_radius = game::variables().get<bool>(CONVAR("weapon_debug_max_inaccuracy"_id));
    parameters.force_angle = game::variables().get<bool>(CONVAR("weapon_debug_inaccuracy_only_up"_id));
    std::optional<detail::shotgun_pattern> pattern;
    if (parameters.patterns && num_bullets > 1)
    {
        if (pattern_seed)
        {
            struct cached_pattern
            {
                int seed{}, bullets{};
                detail::shotgun_pattern value{};
                bool valid{};
            };
            static thread_local cached_pattern cached;
            if (!cached.valid || cached.seed != *pattern_seed || cached.bullets != num_bullets)
                cached = {*pattern_seed, num_bullets, detail::make_shotgun_pattern(*pattern_seed, num_bullets), true};
            pattern = cached.value;
        }
        else
            pattern = detail::read_shotgun_pattern(item_def_idx);
    }
    const auto lookup = [&](int, int, float recoil, int bullets, int pellet, foundation::vec2& polar)
    {
        if (!pattern)
            return detail::pattern_result::unavailable;
        return pattern->sample(recoil, bullets, pellet, polar);
    };
    foundation::vec2 result{};
    if (!detail::sample_spread(parameters, bullet_index, lookup, result))
    {
        const auto invalid = std::numeric_limits<float>::quiet_NaN();
        return {invalid, invalid};
    }
    return result;
}

foundation::vec2 ballistics_t::sample_predicted_spread(int seed, float inaccuracy, float spread, float recoil_index,
                                                       int item_def_idx, int weapon_mode, int bullet_index,
                                                       int num_bullets, std::optional<int> pattern_seed) const
{
    return sample_spread_offset(seed, inaccuracy, spread, recoil_index, item_def_idx, weapon_mode, num_bullets,
                                bullet_index, pattern_seed);
}

float ballistics_t::get_spread(std::uintptr_t weapon_vdata, const int fire_mode) const
{
    std::pair<float, float> values{};
    if (!game::bound_memory().process.copy(weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flSpread"_id), &values,
                                           sizeof(values)))
        return std::numeric_limits<float>::quiet_NaN();
    return fire_mode == 1 ? values.second : values.first;
}

float ballistics_t::get_inaccuracy(std::uintptr_t pawn, std::uintptr_t weapon, std::uintptr_t weapon_vdata,
                                   inaccuracy_debug_data& dbg) const
{
    float forcespread{};
    if (!game::variables().try_get(CONVAR("weapon_accuracy_forcespread"_id), forcespread))
        return std::numeric_limits<float>::quiet_NaN();
    if (forcespread > 0.0f)
    {
        return std::fmin(forcespread, 1.0f);
    }

    const auto nospread = game::variables().get<bool>(CONVAR("weapon_accuracy_nospread"_id));
    if (nospread)
    {
        return 0.0f;
    }

    const auto vdata_first = SCHEMA("CCSWeaponBaseVData", "m_bIsRevolver"_id);
    const auto vdata_end =
        SCHEMA("CCSWeaponBaseVData", "m_nRecoveryTransitionEndBullet"_id) + static_cast<std::int32_t>(sizeof(int));
    const detail::remote_field_snapshot<512> vdata_snapshot(weapon_vdata, vdata_first, vdata_end);
    if (!vdata_snapshot.valid())
        return std::numeric_limits<float>::quiet_NaN();
    dbg.inaccuracy_crouch =
        vdata_snapshot.load<std::pair<float, float>>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyCrouch"_id));
    dbg.inaccuracy_stand =
        vdata_snapshot.load<std::pair<float, float>>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyStand"_id));
    dbg.inaccuracy_ladder =
        vdata_snapshot.load<std::pair<float, float>>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyLadder"_id));
    dbg.inaccuracy_move =
        vdata_snapshot.load<std::pair<float, float>>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyMove"_id));
    dbg.max_speed = vdata_snapshot.load<std::pair<float, float>>(SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_id));
    dbg.inaccuracy_jump_initial =
        vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_id));
    dbg.inaccuracy_jump_apex = vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_id));
    dbg.recovery_time_crouch = vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flRecoveryTimeCrouch"_id));
    dbg.recovery_time_stand = vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flRecoveryTimeStand"_id));
    dbg.recovery_time_crouch_final =
        vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flRecoveryTimeCrouchFinal"_id));
    dbg.recovery_time_stand_final =
        vdata_snapshot.load<float>(SCHEMA("CCSWeaponBaseVData", "m_flRecoveryTimeStandFinal"_id));
    dbg.recovery_transition_start =
        vdata_snapshot.load<int>(SCHEMA("CCSWeaponBaseVData", "m_nRecoveryTransitionStartBullet"_id));
    dbg.recovery_transition_end =
        vdata_snapshot.load<int>(SCHEMA("CCSWeaponBaseVData", "m_nRecoveryTransitionEndBullet"_id));
    dbg.num_bullets = vdata_snapshot.load<int>(SCHEMA("CCSWeaponBaseVData", "m_nNumBullets"_id));
    dbg.is_revolver = vdata_snapshot.load<bool>(SCHEMA("CCSWeaponBaseVData", "m_bIsRevolver"_id));

    const auto weapon_first = SCHEMA("C_CSWeaponBase", "m_weaponMode"_id);
    const auto weapon_end = SCHEMA("C_CSWeaponBase", "m_iRecoilIndex"_id) + static_cast<std::int32_t>(sizeof(int));
    const detail::remote_field_snapshot<128> weapon_snapshot(weapon, weapon_first, weapon_end);
    if (!weapon_snapshot.valid())
        return std::numeric_limits<float>::quiet_NaN();
    dbg.fire_mode = weapon_snapshot.load<int>(SCHEMA("C_CSWeaponBase", "m_weaponMode"_id));
    dbg.turning_inaccuracy = weapon_snapshot.load<float>(SCHEMA("C_CSWeaponBase", "m_flTurningInaccuracy"_id));
    dbg.accuracy_penalty = weapon_snapshot.load<float>(SCHEMA("C_CSWeaponBase", "m_fAccuracyPenalty"_id));
    dbg.recoil_index = weapon_snapshot.load<int>(SCHEMA("C_CSWeaponBase", "m_iRecoilIndex"_id));

    const auto pawn_first = SCHEMA("C_BaseEntity", "m_fFlags"_id);
    const auto pawn_end =
        SCHEMA("C_BaseEntity", "m_hGroundEntity"_id) + static_cast<std::int32_t>(sizeof(std::uint32_t));
    const detail::remote_field_snapshot<512> pawn_snapshot(pawn, pawn_first, pawn_end);
    if (!pawn_snapshot.valid())
        return std::numeric_limits<float>::quiet_NaN();
    const auto flags = pawn_snapshot.load<std::uint32_t>(SCHEMA("C_BaseEntity", "m_fFlags"_id));
    dbg.velocity = pawn_snapshot.load<foundation::vec3>(SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
    dbg.speed = dbg.velocity.length_2d();
    dbg.move_type = pawn_snapshot.load<std::uint8_t>(SCHEMA("C_BaseEntity", "m_MoveType"_id));
    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_CSPlayerPawn", "m_bIsWalking"_id), &dbg.is_walking,
                                           sizeof(dbg.is_walking)))
        return std::numeric_limits<float>::quiet_NaN();
    const auto ground_handle = pawn_snapshot.load<std::uint32_t>(SCHEMA("C_BaseEntity", "m_hGroundEntity"_id));
    dbg.on_ground = ground_handle != 0xffffffffu;
    dbg.crouching = (flags & 2) != 0;

    if (!game::bound_memory().process.copy(pawn + SCHEMA("C_BasePlayerPawn", "v_angle"_id), &dbg.eye_angles,
                                           sizeof(dbg.eye_angles)))
        return std::numeric_limits<float>::quiet_NaN();
    dbg.on_ground =
        ground_handle != 0xffffffffu && ground_handle != 0xfffffffeu && game::entity_index().lookup(ground_handle) != 0;
    return detail::accuracy_from_snapshot(dbg);
}

[[nodiscard]] float ballistics_t::get_inaccuracy_preshot(const foundation::vec3& preshot_velocity,
                                                         bool preshot_on_ground, float preshot_accuracy_penalty,
                                                         int preshot_recoil_index, bool preshot_is_walking,
                                                         inaccuracy_debug_data& dbg) const
{
    dbg.velocity = preshot_velocity;
    dbg.speed = preshot_velocity.length_2d();
    dbg.on_ground = preshot_on_ground;
    dbg.accuracy_penalty = preshot_accuracy_penalty;
    dbg.recoil_index = preshot_recoil_index;
    dbg.is_walking = preshot_is_walking;
    return detail::accuracy_from_snapshot(dbg);
}

bool ballistics_t::ray_hits_capsule(const foundation::vec3& origin, const foundation::vec3& ray,
                                    const foundation::vec3& start, const foundation::vec3& end, float radius) const
{
    float distance{};
    return foundation::ray_capsule_entry(origin, ray, start, end, radius, distance);
}

} // namespace simulation
