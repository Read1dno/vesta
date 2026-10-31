#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <config/combat.hpp>
#include <config/settings.hpp>
#include <game/state/snapshot.hpp>
#include <config/misc.hpp>
#include <game/state/entities.hpp>
#include <game/state/services.hpp>
#include <foundation/color.hpp>
#include <game/state/model_path.hpp>

namespace game
{
namespace
{

// Set once by world_sampler::bind; the free helpers below read the process through them.
[[nodiscard]] bool filtered_display_codepoint(const std::uint32_t value) noexcept
{
    return value <= 0x1Fu || (value >= 0x7Fu && value <= 0x9Fu) || value == 0x200Bu || value == 0x200Eu ||
           value == 0x200Fu || (value >= 0x202Au && value <= 0x202Eu) || (value >= 0x2066u && value <= 0x2069u) ||
           value == 0xFEFFu;
}

[[nodiscard]] std::string sanitized_display_name(std::string_view source)
{
    std::string result{};
    result.reserve(source.size());
    std::size_t offset{};
    std::size_t codepoints{};
    constexpr std::string_view replacement{"\xEF\xBF\xBD"};

    while (offset < source.size() && codepoints < 64)
    {
        const auto first = static_cast<std::uint8_t>(source[offset]);
        std::uint32_t value{};
        std::size_t length{};
        if (first < 0x80u)
        {
            value = first;
            length = 1;
        }
        else if (first >= 0xC2u && first <= 0xDFu)
        {
            value = first & 0x1Fu;
            length = 2;
        }
        else if (first >= 0xE0u && first <= 0xEFu)
        {
            value = first & 0x0Fu;
            length = 3;
        }
        else if (first >= 0xF0u && first <= 0xF4u)
        {
            value = first & 0x07u;
            length = 4;
        }

        bool valid = length != 0 && offset + length <= source.size();
        for (std::size_t index = 1; valid && index < length; ++index)
        {
            const auto continuation = static_cast<std::uint8_t>(source[offset + index]);
            valid = (continuation & 0xC0u) == 0x80u;
            value = (value << 6u) | (continuation & 0x3Fu);
        }

        const auto minimum = length == 2 ? 0x80u : length == 3 ? 0x800u : length == 4 ? 0x10000u : 0u;
        valid = valid && value >= minimum && value <= 0x10FFFFu && !(value >= 0xD800u && value <= 0xDFFFu);
        if (!valid)
        {
            result.append(replacement);
            ++offset;
            ++codepoints;
            continue;
        }

        if (!filtered_display_codepoint(value))
        {
            result.append(source.substr(offset, length));
            ++codepoints;
        }
        offset += length;
    }
    return result;
}

[[nodiscard]] bool line_through_active_smoke(const foundation::vec3& start, const foundation::vec3& end)
{
    const auto projectiles = game::world().projectiles();
    if (!projectiles)
        return false;
    for (const auto& projectile : *projectiles)
    {
        if (projectile.subtype != projectile_kind::smoke_grenade || !projectile.smoke_active)
        {
            continue;
        }
        if (smoke_zone_of(projectile).crosses(start, end))
        {
            return true;
        }
    }
    return false;
}

struct sampling_demand
{
    bool players{};
    bool items{};
    bool projectiles{};
    bool spectators{};
    bool directory{};
};

[[nodiscard]] sampling_demand current_sampling_demand(const bool menu_open)
{
    const auto key_down = [](const int key) { return key > 0 && (::GetAsyncKeyState(key) & 0x8000) != 0; };
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return {};
    const auto combat = snapshot->combat.get(game::local_player().weapon_type());
    const auto independent_seed = combat.triggerbot.seed_type != config::combat_profile::seed_mode::none;
    const auto combat_players =
        (combat.aimbot.enabled && combat.aimbot.draw_fov) ||
        (combat.aimbot.enabled &&
         config::combat_profile::activation_active(combat.aimbot.activation_mode, combat.aimbot.key)) ||
        (combat.triggerbot.enabled && !independent_seed &&
         config::combat_profile::activation_active(combat.triggerbot.activation_mode, combat.triggerbot.key)) ||
        (snapshot->combat.global.grenade_aim.enabled && key_down(snapshot->combat.global.grenade_aim.key));
    const auto& radar_cfg = snapshot->visual.m_radar;
    const auto radar_active = radar_cfg.active();

    const auto players = game::world().script_demand(game::script_data_demand::players) || menu_open ||
                         snapshot->visual.m_player.active() || snapshot->visual.m_chams.enabled || radar_active ||
                         snapshot->visual.m_sound.enabled || snapshot->general.m_bullet_tracers.enabled ||
                         snapshot->general.m_hitmarker.enabled || snapshot->general.m_hitsound.enabled ||
                         snapshot->general.m_hitsound.show_damage || combat_players;
    const auto items = game::world().script_demand(game::script_data_demand::items) || snapshot->visual.m_item.enabled;
    const auto projectiles =
        game::world().script_demand(game::script_data_demand::projectiles) || snapshot->visual.m_projectile.enabled ||
        snapshot->visual.m_no_smoke.enabled ||
        (radar_active && (snapshot->visual.m_radar.show_projectiles || snapshot->visual.m_radar.show_trajectories ||
                          snapshot->visual.m_radar.show_grenade_zones)) ||
        snapshot->general.m_grenades.enabled ||
        (snapshot->visual.m_player.active() && snapshot->visual.m_player.m_legit_sync.enabled &&
         snapshot->visual.m_player.m_legit_sync.direct_visible) ||
        (combat.aimbot.enabled && combat.aimbot.checks.smoke) ||
        (combat.triggerbot.enabled && combat.triggerbot.checks.smoke);
    const auto spectators = game::world().script_demand(game::script_data_demand::spectators) ||
                            snapshot->general.m_spectator_list.enabled || snapshot->visual.m_player.spectator_sync;
    const auto directory = players || items || projectiles || spectators || snapshot->visual.m_bomb.enabled;
    return {players, items, projectiles, spectators, directory};
}

class remote_span
{
public:
    remote_span(const platform::windows::process_session& process, std::uintptr_t object, std::uintptr_t first,
                std::uintptr_t last)
        : m_process(&process)
        , m_first(first)
        , m_size(last > first ? static_cast<std::size_t>(last - first) : 0)
    {
        m_valid =
            object && m_size > 0 && m_size <= m_bytes.size() && process.copy(object + first, m_bytes.data(), m_size);
    }

    template<typename T>
    [[nodiscard]] T get(std::uintptr_t object, std::uintptr_t offset) const
    {
        if (m_valid && offset >= m_first && offset - m_first + sizeof(T) <= m_size)
        {
            T value{};
            std::memcpy(&value, m_bytes.data() + (offset - m_first), sizeof(value));
            return value;
        }
        return m_process->load<T>(object + offset);
    }

private:
    const platform::windows::process_session* m_process{};
    std::array<std::byte, 0x1000> m_bytes;
    std::uintptr_t m_first{};
    std::size_t m_size{};
    bool m_valid{};
};

struct player_name_identity
{
    std::uintptr_t controller{};
    std::uintptr_t name_ptr{};
    std::uint64_t steamid{};
    std::uint32_t pawn_handle{};

    bool operator==(const player_name_identity&) const = default;
};

struct player_name_identity_hash
{
    [[nodiscard]] std::size_t operator()(const player_name_identity& value) const noexcept
    {
        auto hash = std::hash<std::uintptr_t>{}(value.controller);
        const auto combine = [&hash](const std::size_t part)
        { hash ^= part + 0x9e3779b9u + (hash << 6u) + (hash >> 2u); };
        combine(std::hash<std::uintptr_t>{}(value.name_ptr));
        combine(std::hash<std::uint64_t>{}(value.steamid));
        combine(std::hash<std::uint32_t>{}(value.pawn_handle));
        return hash;
    }
};

// A display name is accepted once two reads agree, then revalidated every second.
struct cached_name
{
    std::string confirmed{};
    std::string candidate{};
    std::uint8_t candidate_count{};
    std::chrono::steady_clock::time_point next_validation{};
};
using name_cache = std::unordered_map<player_name_identity, cached_name, player_name_identity_hash>;

std::string cached_player_name(const platform::windows::process_session& process, name_cache& cache,
                               const player_name_identity& identity)
{
    if (!identity.name_ptr)
    {
        return {};
    }

    if (cache.empty())
        cache.reserve(128);
    if (cache.size() > 256)
        cache.clear();
    auto& entry = cache[identity];
    const auto now = std::chrono::steady_clock::now();
    if (!entry.confirmed.empty() && now < entry.next_validation)
    {
        return entry.confirmed;
    }

    auto name = sanitized_display_name(process.load_text(identity.name_ptr, 128));
    if (name.empty())
        return entry.confirmed;
    if (name == entry.confirmed)
    {
        entry.candidate.clear();
        entry.candidate_count = 0;
        entry.next_validation = now + std::chrono::seconds(1);
        return entry.confirmed;
    }
    if (name != entry.candidate)
    {
        entry.candidate = std::move(name);
        entry.candidate_count = 1;
        return entry.confirmed;
    }
    if (++entry.candidate_count >= 2)
    {
        entry.confirmed = std::move(entry.candidate);
        entry.candidate_count = 0;
        entry.next_validation = now + std::chrono::seconds(1);
    }
    return entry.confirmed;
}

struct cached_model_path
{
    std::uintptr_t identity{};
    std::string path{};
};
using model_path_cache = std::unordered_map<std::uintptr_t, cached_model_path>;

std::string cached_player_model_path(const platform::windows::process_session& process, model_path_cache& cache,
                                     std::uintptr_t game_scene_node)
{
    if (!game_scene_node)
        return {};
    const auto source = read_model_name_source(process, game_scene_node);
    const auto identity = source.identity();
    if (const auto it = cache.find(game_scene_node); it != cache.end() && identity && it->second.identity == identity)
        return it->second.path;

    auto path = read_model_path(process, source);
    if (identity && !path.empty())
    {
        if (cache.size() > 256)
            cache.clear();
        cache[game_scene_node] = {identity, path};
    }
    return path;
}

struct weapon_metadata
{
    std::uint16_t item_definition{};
    int max_ammo{};
    std::string name{};
    int candidate_max_ammo{};
    std::string candidate_name{};
    std::uintptr_t candidate_name_ptr{};
    std::uint8_t max_ammo_confirmations{};
    std::uint8_t name_confirmations{};
    bool has_max_ammo{};
    bool has_name{};
};

struct weapon_observation
{
    std::uint32_t handle{};
    std::uintptr_t weapon{};
    std::uintptr_t vdata{};
    std::uint16_t item_definition{};
    std::uint8_t consecutive{};
};

// Entries of a cache that must not outlive the game process they were read from.
template<typename map_t>
struct per_process
{
    map_t entries{};
    std::uint32_t process_id{};

    map_t& for_process(const std::uint32_t current) noexcept
    {
        if (process_id != current)
        {
            entries.clear();
            process_id = current;
        }
        return entries;
    }
};
using weapon_observations = per_process<std::unordered_map<std::uint32_t, weapon_observation>>;

// A weapon change is trusted once two consecutive passes see the same handle, entity, vdata and definition.
[[nodiscard]] bool confirm_weapon_observation(const platform::windows::process_session& process,
                                              weapon_observations& cache, const std::uint32_t pawn_handle,
                                              const std::uint32_t handle, const std::uintptr_t weapon,
                                              const std::uintptr_t vdata, const std::uint16_t item_definition)
{
    auto& observations = cache.for_process(process.process_id());
    if (observations.empty())
        observations.reserve(64);
    if (observations.size() > 128)
        observations.clear();
    auto& current = observations[pawn_handle];
    if (current.handle != handle || current.weapon != weapon || current.vdata != vdata ||
        current.item_definition != item_definition)
    {
        current = {handle, weapon, vdata, item_definition, 1};
        return false;
    }
    current.consecutive = static_cast<std::uint8_t>(std::min<int>(current.consecutive + 1, 2));
    return current.consecutive >= 2;
}

using weapon_metadata_cache = per_process<std::unordered_map<std::uintptr_t, weapon_metadata>>;

weapon_metadata& cached_weapon_metadata(const platform::windows::process_session& process,
                                        weapon_metadata_cache& metadata_cache, std::uintptr_t weapon_vdata,
                                        bool need_name, std::uint16_t item_definition = 0)
{
    auto& cache = metadata_cache.for_process(process.process_id());
    if (cache.empty())
    {
        cache.reserve(128);
    }

    auto [it, inserted] = cache.try_emplace(weapon_vdata);
    auto& metadata = it->second;
    // A vdata address can be recycled after a map/client lifetime transition.
    // Never return metadata proven for a different item definition.
    if (item_definition && metadata.item_definition && metadata.item_definition != item_definition)
    {
        metadata = {};
    }
    if (item_definition)
        metadata.item_definition = item_definition;
    if (inserted || !metadata.has_max_ammo)
    {
        std::int32_t max_ammo{};
        if (process.copy(weapon_vdata + SCHEMA("CBasePlayerWeaponVData", "m_iMaxClip1"_id), &max_ammo,
                         sizeof(max_ammo)) &&
            max_ammo >= -1 && max_ammo <= 500)
        {
            if (metadata.candidate_max_ammo != max_ammo)
            {
                metadata.candidate_max_ammo = max_ammo;
                metadata.max_ammo_confirmations = 1;
            }
            else if (++metadata.max_ammo_confirmations >= 2)
            {
                metadata.max_ammo = max_ammo;
                metadata.has_max_ammo = true;
            }
        }
    }

    if (need_name && !metadata.has_name)
    {
        const auto weapon_name_ptr =
            process.load<std::uintptr_t>(weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_szName"_id));
        if (weapon_name_ptr)
        {
            auto name = process.load_text(weapon_name_ptr, 64);
            if (name.starts_with("weapon_"))
            {
                name.erase(0, 7);
            }
            const auto plausible = !name.empty() && name.size() <= 48 &&
                                   std::ranges::all_of(name, [](const unsigned char value)
                                                       { return std::isalnum(value) || value == '_'; });
            if (plausible)
            {
                if (metadata.candidate_name_ptr != weapon_name_ptr || metadata.candidate_name != name)
                {
                    metadata.candidate_name_ptr = weapon_name_ptr;
                    metadata.candidate_name = std::move(name);
                    metadata.name_confirmations = 1;
                }
                else if (++metadata.name_confirmations >= 2)
                {
                    metadata.name = std::move(metadata.candidate_name);
                    metadata.has_name = true;
                }
            }
        }
    }

    return metadata;
}
// Weapons a player owns, re-read four times a second.
struct cached_loadout
{
    std::vector<std::string> items{};
    std::chrono::steady_clock::time_point refresh{};
};

struct projectile_lifecycle
{
    projectile_kind subtype{projectile_kind::unknown};
    std::chrono::steady_clock::time_point first_seen{};
    std::chrono::steady_clock::time_point last_seen{};
    foundation::vec3 last_origin{};
    foundation::vec3 launch_position{};
    foundation::vec3 launch_velocity{};
    float spawn_time{};
    bool launch_valid{};
    bool launch_from_network{};
};
} // namespace

struct world_sampler::caches
{
    name_cache names{};
    model_path_cache model_paths{};
    weapon_observations weapon_changes{};
    weapon_metadata_cache weapons{};
    std::unordered_map<std::uint32_t, cached_loadout> loadouts{};
    std::unordered_map<std::uintptr_t, projectile_lifecycle> projectile_lifecycles{};
    std::chrono::steady_clock::time_point next_spectator_update{};
    // Scratch lists reused by every pass.
    std::vector<std::uintptr_t> live_pawns{};
    std::vector<std::uintptr_t> live_projectiles{};
};

world_sampler::world_sampler()
    : m_caches(std::make_unique<caches>())
{
}

world_sampler::~world_sampler() = default;

void world_sampler::bind(const memory_services& services, const address_catalog& addresses) noexcept
{
    m_services = &services;
    m_addresses = &addresses;
}

void world_sampler::run(const bool menu_open)
{
    if (!m_services || !m_addresses)
        return;
    const auto demand = current_sampling_demand(menu_open);

    auto& next_spectator_update = m_caches->next_spectator_update;
    const auto now = std::chrono::steady_clock::now();
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto spectator_sync = snapshot->visual.m_player.spectator_sync;
    const auto spectators_requested = demand.spectators && (spectator_sync || now >= next_spectator_update);
    if (!demand.players && !demand.items && !demand.projectiles && !spectators_requested)
    {
        return;
    }

    const auto raw = game::entity_index().all();

    if (spectators_requested)
    {
        next_spectator_update = now + (spectator_sync ? std::chrono::milliseconds(15) : std::chrono::milliseconds(200));
        collect_spectators(*raw);
    }

    if (demand.projectiles)
        collect_projectiles(*raw);
    if (demand.players)
        collect_players(*raw);
    if (demand.items)
        collect_items(*raw);
}

std::shared_ptr<const std::vector<player_snapshot>> world_sampler::players() const
{
    return m_players.load(std::memory_order_acquire);
}

bool world_sampler::entity_directory_requested(const bool menu_open) const
{
    return current_sampling_demand(menu_open).directory;
}

std::vector<player_snapshot> world_sampler::seed_players(std::uintptr_t local_pawn, std::uintptr_t local_controller,
                                                         int local_team, bool free_for_all) const
{
    std::vector<player_snapshot> fresh{};
    seed_players_into(fresh, local_pawn, local_controller, local_team, free_for_all);
    return fresh;
}


std::shared_ptr<const std::vector<world_item_snapshot>> world_sampler::items() const
{
    return m_items.load(std::memory_order_acquire);
}

std::shared_ptr<const std::vector<projectile_snapshot>> world_sampler::projectiles() const
{
    return m_projectiles.load(std::memory_order_acquire);
}

std::shared_ptr<const std::vector<spectator_snapshot>> world_sampler::spectators() const
{
    return m_spectators.load(std::memory_order_acquire);
}

namespace
{
// Schema offsets of everything a player pass reads, resolved on the first pass.
struct player_offsets
{
    std::ptrdiff_t controller_pawn;
    std::ptrdiff_t controller_alive;
    std::ptrdiff_t controller_ping;
    std::ptrdiff_t controller_name;
    std::ptrdiff_t controller_rank;
    std::ptrdiff_t controller_wins;
    std::ptrdiff_t controller_rank_type;
    std::ptrdiff_t controller_money;
    std::ptrdiff_t controller_steamid;
    std::ptrdiff_t pawn_health;
    std::ptrdiff_t pawn_team;
    std::ptrdiff_t pawn_scene_node;
    std::ptrdiff_t pawn_collision;
    std::ptrdiff_t collision_mins;
    std::ptrdiff_t collision_maxs;
    std::ptrdiff_t pawn_emit_sound;
    std::ptrdiff_t pawn_last_fired;
    std::ptrdiff_t pawn_spotted_state;
    std::ptrdiff_t spotted;
    std::ptrdiff_t spotted_by_mask;
    std::ptrdiff_t pawn_immunity;
    std::ptrdiff_t pawn_armor;
    std::ptrdiff_t pawn_scoped;
    std::ptrdiff_t pawn_defusing;
    std::ptrdiff_t pawn_flash_time;
    std::ptrdiff_t pawn_eye_angles;
    std::ptrdiff_t pawn_item_services;
    std::ptrdiff_t pawn_weapon_services;
    std::ptrdiff_t pawn_movement_services;
    std::ptrdiff_t pawn_velocity;
    std::ptrdiff_t pawn_simulation_tick;
    std::ptrdiff_t pawn_simulation_time;
    std::ptrdiff_t movement_ducked;
    std::ptrdiff_t scene_abs_rotation;
    std::ptrdiff_t scene_abs_origin;
    std::ptrdiff_t scene_model_state;
    std::ptrdiff_t item_helmet;
    std::ptrdiff_t item_defuser;
    std::ptrdiff_t active_weapon;
    std::ptrdiff_t owned_weapons;
    std::ptrdiff_t weapon_subclass;
    std::ptrdiff_t weapon_ammo;
    std::ptrdiff_t weapon_item_definition;
    std::ptrdiff_t grenade_pin_pulled;
    std::ptrdiff_t money_account;
};

const player_offsets& player_schema()
{
    static const player_offsets offsets{
        SCHEMA("CCSPlayerController", "m_hPlayerPawn"_id),
        SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_id),
        SCHEMA("CCSPlayerController", "m_iPing"_id),
        SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_id),
        SCHEMA("CCSPlayerController", "m_iCompetitiveRanking"_id),
        SCHEMA("CCSPlayerController", "m_iCompetitiveWins"_id),
        SCHEMA("CCSPlayerController", "m_iCompetitiveRankType"_id),
        SCHEMA("CCSPlayerController", "m_pInGameMoneyServices"_id),
        SCHEMA("CBasePlayerController", "m_steamID"_id),
        SCHEMA("C_BaseEntity", "m_iHealth"_id),
        SCHEMA("C_BaseEntity", "m_iTeamNum"_id),
        SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id),
        SCHEMA("C_BaseModelEntity", "m_Collision"_id),
        SCHEMA("CCollisionProperty", "m_vecMins"_id),
        SCHEMA("CCollisionProperty", "m_vecMaxs"_id),
        SCHEMA("C_CSPlayerPawn", "m_flEmitSoundTime"_id),
        SCHEMA("C_CSPlayerPawn", "m_flLastFiredWeaponTime"_id),
        SCHEMA("C_CSPlayerPawn", "m_entitySpottedState"_id),
        SCHEMA("EntitySpottedState_t", "m_bSpotted"_id),
        SCHEMA("EntitySpottedState_t", "m_bSpottedByMask"_id),
        SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_id),
        SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_id),
        SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_id),
        SCHEMA("C_CSPlayerPawn", "m_bIsDefusing"_id),
        SCHEMA("C_CSPlayerPawnBase", "m_flFlashBangTime"_id),
        SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_id),
        SCHEMA("C_BasePlayerPawn", "m_pItemServices"_id),
        SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_id),
        SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_id),
        SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id),
        SCHEMA("C_BaseEntity", "m_nSimulationTick"_id),
        SCHEMA("C_BaseEntity", "m_flSimulationTime"_id),
        SCHEMA("CCSPlayer_MovementServices", "m_bDucked"_id),
        SCHEMA("CGameSceneNode", "m_angAbsRotation"_id),
        SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id),
        SCHEMA("CSkeletonInstance", "m_modelState"_id),
        SCHEMA("CCSPlayer_ItemServices", "m_bHasHelmet"_id),
        SCHEMA("CCSPlayer_ItemServices", "m_bHasDefuser"_id),
        SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_id),
        SCHEMA("CPlayer_WeaponServices", "m_hMyWeapons"_id),
        // The weapon's vdata pointer sits right after the subclass id.
        SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8,
        SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id),
        SCHEMA("C_EconEntity", "m_AttributeManager"_id) + SCHEMA("C_AttributeContainer", "m_Item"_id) +
            SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_id),
        SCHEMA("C_BaseCSGrenade", "m_bPinPulled"_id),
        SCHEMA("CCSPlayerController_InGameMoneyServices", "m_iAccount"_id),
    };
    return offsets;
}

// Animgraph 2: the model state owns the live bone cache at this offset.
constexpr std::ptrdiff_t k_model_state_bone_cache = 0x80;
// A CUtlVector's element pointer follows its count.
constexpr std::ptrdiff_t k_utl_vector_data = 0x8;
constexpr std::uint32_t k_invalid_handle = 0xFFFFFFFFu;
constexpr std::uint32_t k_max_owned_weapons = 32;
// Bones that fail to read for a moment (the cache is being rebuilt) are bridged with the last good set this long.
constexpr auto k_bone_bridge = std::chrono::milliseconds(100);
constexpr auto k_loadout_refresh = std::chrono::milliseconds(250);
constexpr int k_head_bone = 7;

[[nodiscard]] bool is_grenade(const std::string_view name)
{
    return name == "flashbang" || name == "hegrenade" || name == "smokegrenade" || name == "molotov" ||
           name == "incgrenade" || name == "decoy";
}
} // namespace

struct world_sampler::player_pass
{
    std::shared_ptr<const config::runtime_snapshot> settings;
    foundation::vec3 view_origin{};
    std::chrono::steady_clock::time_point now{};
};

void world_sampler::collect_players(const std::vector<entity_directory::cached>& raw)
{
    player_pass pass{.settings = config::get_runtime_snapshot(),
                     .view_origin = game::camera().origin(),
                     .now = std::chrono::steady_clock::now()};
    auto unused_angles = foundation::vec3{};
    static_cast<void>(game::camera().sample(pass.view_origin, unused_angles));

    auto& live_pawns = m_caches->live_pawns;
    live_pawns.clear();
    if (live_pawns.capacity() < 64)
        live_pawns.reserve(64);
    std::int32_t local_controller_index{-1};
    for (const auto& entry : raw)
    {
        if (entry.ptr == game::local_player().controller())
        {
            local_controller_index = entry.index;
            break;
        }
    }

    std::vector<player_snapshot> fresh{};
    fresh.reserve(64);
    for (const auto& entry : raw)
    {
        if (entry.type != entity_directory::type::player)
            continue;
        player_snapshot player{};
        if (read_player(pass, entry, player))
            fresh.push_back(std::move(player));
    }

    update_legit_visibility(pass, fresh, local_controller_index);

    std::ranges::sort(fresh, [&pass](const player_snapshot& a, const player_snapshot& b)
                      { return pass.view_origin.distance_sqr(a.origin) > pass.view_origin.distance_sqr(b.origin); });
    forget_departed_players();

    auto snapshot = std::make_shared<const std::vector<player_snapshot>>(std::move(fresh));
    m_players.store(std::move(snapshot), std::memory_order_release);
}

// Reads one controller and its pawn; false when it is not a living, in-team player other than the local one or its
// pawn cannot be read. Fields are read in blocks (remote_span) grouped by the object they live in.
bool world_sampler::read_player(const player_pass& pass, const entity_directory::cached& entry, player_snapshot& p)
{
    const auto& o = player_schema();
    const auto& process = m_services->process;

    const auto controller_first =
        std::min({o.controller_steamid, o.controller_money, o.controller_ping, o.controller_name, o.controller_rank,
                  o.controller_wins, o.controller_rank_type, o.controller_pawn, o.controller_alive});
    const auto controller_last =
        std::max({o.controller_steamid + sizeof(std::uint64_t), o.controller_money + sizeof(std::uintptr_t),
                  o.controller_ping + sizeof(std::int32_t), o.controller_name + sizeof(std::uintptr_t),
                  o.controller_rank + sizeof(std::int32_t), o.controller_wins + sizeof(std::int32_t),
                  o.controller_rank_type + sizeof(std::int32_t), o.controller_pawn + sizeof(std::uint32_t),
                  o.controller_alive + sizeof(bool)});
    const remote_span controller_fields(process, entry.ptr, controller_first, controller_last);

    const auto player_pawn_handle = controller_fields.get<std::uint32_t>(entry.ptr, o.controller_pawn);
    if (!player_pawn_handle || !controller_fields.get<bool>(entry.ptr, o.controller_alive))
        return false;

    const auto player_pawn = game::entity_index().lookup(player_pawn_handle);
    // Neither the player's own body nor the one it spectates: the camera sits in that head, and its box and chams
    // would cover the whole screen.
    if (!player_pawn || player_pawn == game::local_player().view_pawn() ||
        player_pawn == game::local_player().spectated_pawn())
        return false;

    const auto collision_mins_offset = o.pawn_collision + o.collision_mins;
    const auto collision_maxs_offset = o.pawn_collision + o.collision_maxs;
    const auto pawn_base_first =
        std::min({o.pawn_health, o.pawn_team, o.pawn_scene_node, collision_mins_offset, collision_maxs_offset});
    const auto pawn_base_last =
        std::max({o.pawn_health + sizeof(std::int32_t), o.pawn_team + sizeof(std::int32_t),
                  o.pawn_scene_node + sizeof(std::uintptr_t), collision_mins_offset + sizeof(foundation::vec3),
                  collision_maxs_offset + sizeof(foundation::vec3)});
    const remote_span pawn_base(process, player_pawn, pawn_base_first, pawn_base_last);

    const auto health = pawn_base.get<std::int32_t>(player_pawn, o.pawn_health);
    // Custom servers raise health above 100 (armor games, bonus health): only a dead pawn is skipped.
    if (health <= 0)
        return false;

    p.controller = entry.ptr;
    p.controller_index = entry.index;
    p.pawn_handle = player_pawn_handle;
    p.pawn = player_pawn;
    p.health = health;
    p.team = pawn_base.get<std::int32_t>(player_pawn, o.pawn_team);
    // Teams 0 (unassigned) and 1 (spectators) are not players; anything else is, whatever the mode calls it.
    if (p.team < 2)
        return false;

    const auto spotted_offset = o.pawn_spotted_state + o.spotted;
    const auto spotted_mask_offset = o.pawn_spotted_state + o.spotted_by_mask;
    const auto status_first = std::min({o.pawn_emit_sound, o.pawn_armor, o.pawn_scoped, o.pawn_defusing,
                                        o.pawn_flash_time, o.pawn_last_fired, spotted_offset, spotted_mask_offset});
    const auto status_last =
        std::max({o.pawn_emit_sound + sizeof(float), o.pawn_last_fired + sizeof(float), spotted_offset + sizeof(bool),
                  spotted_mask_offset + sizeof(std::uint64_t), o.pawn_armor + sizeof(std::int32_t),
                  o.pawn_scoped + sizeof(bool), o.pawn_defusing + sizeof(bool), o.pawn_flash_time + sizeof(float)});
    const remote_span pawn_status(process, player_pawn, status_first, status_last);
    p.emit_sound_time = pawn_status.get<float>(player_pawn, o.pawn_emit_sound);
    p.last_fired_time = pawn_status.get<float>(player_pawn, o.pawn_last_fired);
    p.is_spotted = pawn_status.get<bool>(player_pawn, spotted_offset);
    p.spotted_by_mask = pawn_status.get<std::uint64_t>(player_pawn, spotted_mask_offset);
    p.armor = pawn_status.get<std::int32_t>(player_pawn, o.pawn_armor);
    p.is_scoped = pawn_status.get<bool>(player_pawn, o.pawn_scoped);
    p.is_defusing = pawn_status.get<bool>(player_pawn, o.pawn_defusing);
    p.is_flashed = pawn_status.get<float>(player_pawn, o.pawn_flash_time) > 0.0f;

    const auto aim_first = std::min(o.pawn_immunity, o.pawn_eye_angles);
    const auto aim_last = std::max(o.pawn_immunity + sizeof(bool), o.pawn_eye_angles + sizeof(foundation::vec3));
    const remote_span pawn_aim(process, player_pawn, aim_first, aim_last);
    p.invulnerable = pawn_aim.get<bool>(player_pawn, o.pawn_immunity);
    p.eye_angles = pawn_aim.get<foundation::vec3>(player_pawn, o.pawn_eye_angles);
    p.ping = controller_fields.get<std::int32_t>(entry.ptr, o.controller_ping);

    const auto game_scene_node = pawn_base.get<std::uintptr_t>(player_pawn, o.pawn_scene_node);
    if (!game_scene_node)
        return false;

    const auto bone_cache_offset = o.scene_model_state + k_model_state_bone_cache;
    const auto scene_first = std::min({o.scene_abs_rotation, o.scene_abs_origin, bone_cache_offset});
    const auto scene_last =
        std::max({o.scene_abs_rotation + sizeof(foundation::vec3), o.scene_abs_origin + sizeof(foundation::vec3),
                  bone_cache_offset + sizeof(std::uintptr_t)});
    const remote_span scene_fields(process, game_scene_node, scene_first, scene_last);
    // Pawns that have not replicated eye angles yet face along their scene rotation.
    if (p.eye_angles.x == 0.0f && p.eye_angles.y == 0.0f)
        p.eye_angles = scene_fields.get<foundation::vec3>(game_scene_node, o.scene_abs_rotation);

    p.game_scene_node = game_scene_node;
    if (pass.settings->visual.m_chams.enabled)
        p.model_path = cached_player_model_path(process, m_caches->model_paths, game_scene_node);

    p.bone_cache = scene_fields.get<std::uintptr_t>(game_scene_node, bone_cache_offset);
    p.origin = scene_fields.get<foundation::vec3>(game_scene_node, o.scene_abs_origin);
    p.velocity = process.load<foundation::vec3>(player_pawn + o.pawn_velocity);
    p.simulation_tick = process.load<std::int32_t>(player_pawn + o.pawn_simulation_tick);
    p.simulation_time = process.load<float>(player_pawn + o.pawn_simulation_time);
    if (!std::isfinite(p.origin.x) || !std::isfinite(p.origin.y) || !std::isfinite(p.origin.z))
        return false;

    // Native bomb prediction samples WorldSpaceCenter(): abs origin plus the
    // midpoint of the current standing/crouched collision hull.
    const auto collision_mins = pawn_base.get<foundation::vec3>(player_pawn, collision_mins_offset);
    const auto collision_maxs = pawn_base.get<foundation::vec3>(player_pawn, collision_maxs_offset);
    p.collision_center = p.origin + (collision_mins + collision_maxs) * 0.5f;

    const auto services_first = std::min({o.pawn_item_services, o.pawn_weapon_services, o.pawn_movement_services});
    const auto services_last =
        std::max({o.pawn_item_services + sizeof(std::uintptr_t), o.pawn_weapon_services + sizeof(std::uintptr_t),
                  o.pawn_movement_services + sizeof(std::uintptr_t)});
    const remote_span services(process, player_pawn, services_first, services_last);
    const auto movement_services = services.get<std::uintptr_t>(player_pawn, o.pawn_movement_services);
    p.is_ducked = movement_services && process.load<bool>(movement_services + o.movement_ducked);

    read_bones(pass, p);
    p.is_visible = p.bones.is_valid() && clear_line(pass.view_origin, p.bones.get_position(k_head_bone));
    track_emit_sound(pass, p);
    m_caches->live_pawns.push_back(p.pawn);
    p.hitboxes = game::hitbox_data().query(game_scene_node, &m_hitbox_cache);
    game::hitbox_data().remember(p.hitboxes);

    if (const auto item_services = services.get<std::uintptr_t>(player_pawn, o.pawn_item_services))
    {
        const auto item_first = std::min(o.item_helmet, o.item_defuser);
        const auto item_last = std::max(o.item_helmet + sizeof(bool), o.item_defuser + sizeof(bool));
        const remote_span item_fields(process, item_services, item_first, item_last);
        p.has_helmet = item_fields.get<bool>(item_services, o.item_helmet);
        p.has_defuser = item_fields.get<bool>(item_services, o.item_defuser);
    }

    if (const auto weapon_services = services.get<std::uintptr_t>(player_pawn, o.pawn_weapon_services))
    {
        read_active_weapon(weapon_services, player_pawn_handle, p.weapon);
        p.loadout = read_loadout(pass, weapon_services, player_pawn_handle);
    }

    p.steamid = controller_fields.get<std::uint64_t>(entry.ptr, o.controller_steamid);
    const auto name_ptr = controller_fields.get<std::uintptr_t>(entry.ptr, o.controller_name);
    p.display_name = cached_player_name(process, m_caches->names, {entry.ptr, name_ptr, p.steamid, player_pawn_handle});
    p.comp_rank = controller_fields.get<std::int32_t>(entry.ptr, o.controller_rank);
    p.comp_wins = controller_fields.get<std::int32_t>(entry.ptr, o.controller_wins);
    p.comp_rank_type = controller_fields.get<std::int32_t>(entry.ptr, o.controller_rank_type);

    if (const auto money_services = controller_fields.get<std::uintptr_t>(entry.ptr, o.controller_money))
        p.money = process.load<std::int32_t>(money_services + o.money_account);
    return true;
}

void world_sampler::read_bones(const player_pass& pass, player_snapshot& p)
{
    p.bones = game::skeletons().get(p.bone_cache);
    if (p.bones.is_valid())
    {
        m_last_valid_bones[p.pawn] = {p.bone_cache, p.bones, pass.now};
    }
    else if (const auto cached = m_last_valid_bones.find(p.pawn); cached != m_last_valid_bones.end() &&
                                                                  cached->second.bone_cache == p.bone_cache &&
                                                                  pass.now - cached->second.timestamp <= k_bone_bridge)
    {
        p.bones = cached->second.bones;
    }
}

// A rising emit-sound time within the configured distance counts as the player being heard.
void world_sampler::track_emit_sound(const player_pass& pass, const player_snapshot& p)
{
    const auto& legit = pass.settings->visual.m_player.m_legit_sync;
    const auto emit = m_last_emit_sound.find(p.pawn);
    if (emit == m_last_emit_sound.end())
    {
        m_last_emit_sound.emplace(p.pawn, p.emit_sound_time);
        return;
    }
    if (p.emit_sound_time > emit->second + 0.001f &&
        pass.view_origin.distance_sqr(p.origin) <= legit.sound_distance * legit.sound_distance)
        m_last_sound_heard[p.pawn] = pass.now;
    emit->second = p.emit_sound_time;
}

// The active weapon is kept only if the owner's handle, the weapon's vdata and its item definition read the same
// before and after its details: a weapon switch in the middle of the read would otherwise mix two weapons.
void world_sampler::read_active_weapon(const std::uintptr_t weapon_services, const std::uint32_t pawn_handle,
                                       equipped_weapon_snapshot& weapon)
{
    const auto& o = player_schema();
    const auto& process = m_services->process;
    const auto active_weapon_handle = process.load<std::uint32_t>(weapon_services + o.active_weapon);
    if (!active_weapon_handle || active_weapon_handle == k_invalid_handle)
        return;
    weapon.ptr = game::entity_index().lookup(active_weapon_handle);
    if (!weapon.ptr)
        return;

    weapon.handle = active_weapon_handle;
    weapon.vdata = process.load<std::uintptr_t>(weapon.ptr + o.weapon_subclass);
    weapon.item_definition = process.load<std::uint16_t>(weapon.ptr + o.weapon_item_definition);
    const auto stable_before_metadata =
        weapon.vdata && weapon.item_definition > 0 &&
        process.load<std::uint32_t>(weapon_services + o.active_weapon) == active_weapon_handle &&
        game::entity_index().lookup(active_weapon_handle) == weapon.ptr;
    if (!stable_before_metadata ||
        !confirm_weapon_observation(process, m_caches->weapon_changes, pawn_handle, active_weapon_handle, weapon.ptr,
                                    weapon.vdata, weapon.item_definition))
        return;

    weapon.ammo = process.load<std::int32_t>(weapon.ptr + o.weapon_ammo);
    const auto& metadata =
        cached_weapon_metadata(process, m_caches->weapons, weapon.vdata, true, weapon.item_definition);
    weapon.max_ammo = metadata.max_ammo;
    weapon.name = metadata.name;
    if (is_grenade(weapon.name))
        weapon.pin_pulled = process.load<bool>(weapon.ptr + o.grenade_pin_pulled);

    const auto final_handle = process.load<std::uint32_t>(weapon_services + o.active_weapon);
    const auto final_vdata = process.load<std::uintptr_t>(weapon.ptr + o.weapon_subclass);
    const auto final_definition = process.load<std::uint16_t>(weapon.ptr + o.weapon_item_definition);
    if (final_handle != active_weapon_handle || final_vdata != weapon.vdata ||
        final_definition != weapon.item_definition)
        weapon = {};
}

// Names of every weapon the player owns; refreshed a few times per second per pawn, served from the cache between.
std::vector<std::string> world_sampler::read_loadout(const player_pass& pass, const std::uintptr_t weapon_services,
                                                     const std::uint32_t pawn_handle)
{
    const auto& o = player_schema();
    const auto& process = m_services->process;
    auto& loadouts = m_caches->loadouts;
    if (loadouts.empty())
        loadouts.reserve(64);
    if (loadouts.size() > 128)
        loadouts.clear();
    auto& loadout = loadouts[pawn_handle];
    if (pass.now < loadout.refresh)
        return loadout.items;
    loadout.refresh = pass.now + k_loadout_refresh;

    const auto owned_count = process.load<std::uint32_t>(weapon_services + o.owned_weapons);
    const auto owned_data = process.load<std::uintptr_t>(weapon_services + o.owned_weapons + k_utl_vector_data);
    if (!owned_data || owned_count == 0 || owned_count > k_max_owned_weapons)
        return loadout.items;
    std::array<std::uint32_t, k_max_owned_weapons> handles{};
    if (!process.copy(owned_data, handles.data(), owned_count * sizeof(handles.front())))
        return loadout.items;

    std::vector<std::string> next{};
    next.reserve(owned_count);
    for (std::uint32_t index = 0; index < owned_count; ++index)
    {
        const auto handle = handles[index];
        if (!handle || handle == k_invalid_handle)
            continue;
        const auto weapon = game::entity_index().lookup(handle);
        if (!weapon)
            continue;
        const auto vdata = process.load<std::uintptr_t>(weapon + o.weapon_subclass);
        const auto definition = process.load<std::uint16_t>(weapon + o.weapon_item_definition);
        if (!vdata || !definition)
            continue;
        const auto& metadata = cached_weapon_metadata(process, m_caches->weapons, vdata, true, definition);
        if (!metadata.name.empty())
            next.push_back(metadata.name);
    }
    loadout.items = std::move(next);
    return loadout.items;
}

// Legit visibility: an enemy is shown when the local view sees it directly, or pulses while only the team knows of it
// (a fresh radar spot by a teammate, a teammate looking at it, or a recent sound), and fades out otherwise.
void world_sampler::update_legit_visibility(const player_pass& pass, std::vector<player_snapshot>& players,
                                            const std::int32_t local_controller_index)
{
    std::uint64_t friendly_controller_mask{};
    const auto add_controller_bit = [&](const std::int32_t index)
    {
        if (index >= 0 && index < 64)
            friendly_controller_mask |= 1ull << index;
    };
    add_controller_bit(local_controller_index);
    for (const auto& player : players)
        if (!game::local_player().is_enemy(player.team))
            add_controller_bit(player.controller_index);

    const auto& legit = pass.settings->visual.m_player.m_legit_sync;
    const auto recently = [&](const auto& events, const std::uintptr_t pawn, const float seconds)
    {
        const auto found = events.find(pawn);
        return found != events.end() && std::chrono::duration<float>(pass.now - found->second).count() <= seconds;
    };
    for (auto& player : players)
    {
        const auto relevant_mask = player.spotted_by_mask & friendly_controller_mask;
        auto& previous_mask = m_last_spotted_mask[player.pawn];
        const auto fresh_radar_edge = (relevant_mask & ~previous_mask) != 0;
        previous_mask = relevant_mask;

        const auto teammate_spotted = legit.enabled && legit.radar && game::local_player().is_enemy(player.team) &&
                                      seen_by_teammate(player, players);
        player.is_spotted = fresh_radar_edge || teammate_spotted;
        if (player.is_spotted)
            m_last_radar_seen[player.pawn] = pass.now;
        const auto spectator_blocked = pass.settings->visual.m_player.spectator_sync && local_spectated();
        const auto direct_signal = !spectator_blocked && legit.direct_visible && player.is_visible &&
                                   !line_through_active_smoke(pass.view_origin, player.bones.get_position(k_head_bone));
        const auto indirect_signal =
            !spectator_blocked && ((legit.radar && recently(m_last_radar_seen, player.pawn, legit.radar_hold)) ||
                                   (legit.sound && recently(m_last_sound_heard, player.pawn, legit.sound_hold)));

        if (!legit.enabled || spectator_blocked)
        {
            player.legit_visible = !legit.enabled && !spectator_blocked;
            player.legit_opacity = player.legit_visible ? 1.0f : 0.0f;
            m_legit_fades.erase(player.pawn);
            continue;
        }
        player.legit_opacity = advance_legit_fade(m_legit_fades[player.pawn], pass, direct_signal, indirect_signal);
        player.legit_visible = player.legit_opacity > 0.0f;
    }
}

// Whether a teammate faces the enemy's head (within about 56 degrees) with a clear line to it.
bool world_sampler::seen_by_teammate(const player_snapshot& enemy, const std::vector<player_snapshot>& players) const
{
    constexpr auto view_cone_cos = 0.55f;
    const auto target = enemy.bones.get_position(k_head_bone);
    for (const auto& teammate : players)
    {
        if (teammate.pawn == enemy.pawn || game::local_player().is_enemy(teammate.team))
            continue;
        const auto observer = teammate.bones.get_position(k_head_bone);
        auto direction = target - observer;
        if (direction.length_sqr() < 1.0f)
            continue;
        direction.normalize();
        foundation::vec3 forward{};
        teammate.eye_angles.to_directions(&forward, nullptr, nullptr);
        forward.normalize();
        if (forward.dot(direction) < view_cone_cos)
            continue;
        if (clear_line(observer, target))
            return true;
    }
    return false;
}

// Direct sight shows the player fully; indirect knowledge pulses between the configured opacities; without either the
// opacity decays. Returns the opacity for this pass.
float world_sampler::advance_legit_fade(legit_fade_state& fade, const player_pass& pass, const bool direct_signal,
                                        const bool indirect_signal)
{
    const auto& legit = pass.settings->visual.m_player.m_legit_sync;
    if (fade.updated.time_since_epoch().count() == 0)
    {
        fade.updated = pass.now;
        fade.pulse_started = pass.now;
    }
    const std::uint8_t signal_mode = direct_signal ? 2u : indirect_signal ? 1u : 0u;
    if (signal_mode == 1u && fade.signal_mode != 1u)
        fade.pulse_started = pass.now;

    const auto delta = std::clamp(std::chrono::duration<float>(pass.now - fade.updated).count(), 0.0f, 0.1f);
    if (direct_signal)
    {
        fade.opacity = 1.0f;
    }
    else
    {
        float target{};
        if (indirect_signal)
        {
            const auto elapsed = std::chrono::duration<float>(pass.now - fade.pulse_started).count();
            const auto phase = 0.5f + 0.5f * std::cos(elapsed * 2.0f * std::numbers::pi_v<float> /
                                                      std::max(legit.pulse_period, 0.01f));
            const auto smooth_phase = phase * phase * (3.0f - 2.0f * phase);
            target = std::lerp(legit.pulse_min_opacity, legit.pulse_max_opacity, smooth_phase);
        }
        const auto response = indirect_signal ? 0.12f : 0.28f;
        const auto blend = delta > 0.0f ? 1.0f - std::exp(-delta / response) : 0.0f;
        fade.opacity = std::lerp(fade.opacity, target, blend);
    }
    if (signal_mode == 0u && fade.opacity < 0.01f)
        fade.opacity = 0.0f;
    fade.signal_mode = signal_mode;
    fade.updated = pass.now;
    return std::clamp(fade.opacity, 0.0f, 1.0f);
}

// Drops the per-pawn history of players that were not in this pass.
void world_sampler::forget_departed_players()
{
    auto& live_pawns = m_caches->live_pawns;
    std::ranges::sort(live_pawns);
    live_pawns.erase(std::unique(live_pawns.begin(), live_pawns.end()), live_pawns.end());
    const auto prune = [&](auto& values)
    { std::erase_if(values, [&](const auto& item) { return !std::ranges::binary_search(live_pawns, item.first); }); };
    prune(m_last_emit_sound);
    prune(m_last_radar_seen);
    prune(m_last_spotted_mask);
    prune(m_last_sound_heard);
    prune(m_legit_fades);
    prune(m_last_valid_bones);
}

void world_sampler::collect_items(const std::vector<entity_directory::cached>& raw)
{
    std::vector<world_item_snapshot> fresh{};
    fresh.reserve(64);

    for (const auto& entry : raw)
    {
        if (entry.type != entity_directory::type::item)
        {
            continue;
        }

        const auto subtype = classify_item(entry.schema_id);
        if (subtype == world_item_kind::unknown)
        {
            continue;
        }

        const auto owner_handle =
            m_services->process.load<std::uint32_t>(entry.ptr + SCHEMA("C_BaseEntity", "m_hOwnerEntity"_id));
        if (owner_handle && owner_handle != 0xffffffff)
        {
            continue;
        }

        const auto game_scene_node =
            m_services->process.load<std::uintptr_t>(entry.ptr + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id));
        if (!game_scene_node)
        {
            continue;
        }

        world_item_snapshot i{};
        i.entity = entry.ptr;
        i.game_scene_node = game_scene_node;
        i.subtype = subtype;
        i.origin =
            m_services->process.load<foundation::vec3>(game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id));
        i.ammo = m_services->process.load<std::int32_t>(entry.ptr + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id));

        const auto weapon_vdata =
            m_services->process.load<std::uintptr_t>(entry.ptr + SCHEMA("C_BaseEntity", "m_nSubclassID"_id) + 0x8);
        if (weapon_vdata)
        {
            i.max_ammo = cached_weapon_metadata(m_services->process, m_caches->weapons, weapon_vdata, false).max_ammo;
        }

        fresh.push_back(std::move(i));
    }

    {
        const auto view_origin = game::camera().origin();
        std::ranges::sort(fresh, [&view_origin](const world_item_snapshot& a, const world_item_snapshot& b)
                          { return view_origin.distance_sqr(a.origin) > view_origin.distance_sqr(b.origin); });
    }

    auto snapshot = std::make_shared<const std::vector<world_item_snapshot>>(std::move(fresh));
    m_items.store(std::move(snapshot), std::memory_order_release);
}

void world_sampler::collect_projectiles(const std::vector<entity_directory::cached>& raw)
{
    auto& lifecycles = m_caches->projectile_lifecycles;
    auto& live_projectiles = m_caches->live_projectiles;
    const auto observed_now = std::chrono::steady_clock::now();
    live_projectiles.clear();
    live_projectiles.reserve(32);

    std::vector<projectile_snapshot> fresh{};
    fresh.reserve(32);

    const auto current_time =
        m_services->process.load<float>(m_services->process.load<std::uintptr_t>((*m_addresses).global_vars) + 0x30);

    for (const auto& entry : raw)
    {
        if (entry.type != entity_directory::type::projectile)
        {
            continue;
        }

        const auto subtype = classify_projectile(entry.schema_id);
        if (subtype == projectile_kind::unknown)
        {
            continue;
        }

        if (subtype == projectile_kind::molotov_fire)
        {
            const auto fire_count = m_services->process.load<int>(entry.ptr + SCHEMA("C_Inferno", "m_fireCount"_id));
            if (fire_count <= 0)
            {
                continue;
            }

            const auto bounded_count = static_cast<std::size_t>(std::min(fire_count, 64));
            std::array<std::uint8_t, 64> active{};
            std::array<foundation::vec3, 64> positions{};
            const auto active_base = entry.ptr + SCHEMA("C_Inferno", "m_bFireIsBurning"_id);
            const auto positions_base = entry.ptr + SCHEMA("C_Inferno", "m_firePositions"_id);

            if (!m_services->process.copy(active_base, active.data(), bounded_count) ||
                !m_services->process.copy(positions_base, positions.data(), bounded_count * sizeof(foundation::vec3)))
            {
                continue;
            }

            std::vector<foundation::vec3> fire_points{};
            fire_points.reserve(bounded_count);
            for (std::size_t i = 0; i < bounded_count; ++i)
            {
                if (active[i] && std::isfinite(positions[i].x) && std::isfinite(positions[i].y) &&
                    std::isfinite(positions[i].z))
                {
                    fire_points.push_back(positions[i]);
                }
            }

            if (fire_points.empty())
            {
                continue;
            }

            auto center = foundation::vec3{};
            for (const auto& point : fire_points)
            {
                center = center + point;
            }
            center = center * (1.0f / static_cast<float>(fire_points.size()));

            const auto effect_tick =
                m_services->process.load<std::int32_t>(entry.ptr + SCHEMA("C_Inferno", "m_nFireEffectTickBegin"_id));
            const auto start_time = static_cast<float>(effect_tick) * (1.0f / 64.0f);
            constexpr auto inferno_duration = 7.0f;

            projectile_snapshot p{};
            p.entity = entry.ptr;
            p.subtype = subtype;
            p.origin = center;
            p.fire_points = std::move(fire_points);
            p.expire_time = start_time + inferno_duration;

            fresh.push_back(std::move(p));
            continue;
        }

        const auto game_scene_node =
            m_services->process.load<std::uintptr_t>(entry.ptr + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_id));
        if (!game_scene_node)
        {
            continue;
        }

        projectile_snapshot p{};
        p.entity = entry.ptr;
        p.game_scene_node = game_scene_node;
        p.subtype = subtype;
        p.origin =
            m_services->process.load<foundation::vec3>(game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_id));
        p.velocity =
            m_services->process.load<foundation::vec3>(entry.ptr + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_id));
        p.initial_position = m_services->process.load<foundation::vec3>(
            entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialPosition"_id));
        p.initial_velocity = m_services->process.load<foundation::vec3>(
            entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_id));
        p.thrower_handle =
            m_services->process.load<std::uint32_t>(entry.ptr + SCHEMA("C_BaseGrenade", "m_hThrower"_id));
        p.bounces =
            m_services->process.load<std::int32_t>(entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile", "m_nBounces"_id));
        p.spawn_time =
            m_services->process.load<float>(entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile", "m_flSpawnTime"_id));
        p.detonate_time = m_services->process.load<float>(entry.ptr + SCHEMA("C_BaseGrenade", "m_flDetonateTime"_id));
        live_projectiles.push_back(entry.ptr);
        auto [lifecycle_it, inserted] = lifecycles.try_emplace(entry.ptr);
        auto& lifecycle = lifecycle_it->second;
        const auto recycled =
            !inserted && (lifecycle.subtype != subtype || (p.spawn_time > 0.0f && lifecycle.spawn_time > 0.0f &&
                                                           std::abs(p.spawn_time - lifecycle.spawn_time) > 0.001f));
        if (inserted || recycled)
        {
            lifecycle = {};
            lifecycle.subtype = subtype;
            lifecycle.first_seen = observed_now;
            lifecycle.last_seen = observed_now;
            lifecycle.last_origin = p.origin;
            lifecycle.spawn_time = p.spawn_time;
        }

        const auto finite = [](const foundation::vec3& value)
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
        const auto network_launch_valid = finite(p.initial_position) && finite(p.initial_velocity) &&
                                          p.initial_position.length_sqr() > 1.0f &&
                                          p.initial_velocity.length_sqr() > 25.0f;
        if (network_launch_valid && !lifecycle.launch_from_network)
        {
            lifecycle.launch_position = p.initial_position;
            lifecycle.launch_velocity = p.initial_velocity;
            lifecycle.launch_valid = true;
            lifecycle.launch_from_network = true;
        }
        auto observed_velocity = p.velocity;
        const auto sample_seconds = std::chrono::duration<float>(observed_now - lifecycle.last_seen).count();
        if ((!finite(observed_velocity) || observed_velocity.length_sqr() <= 25.0f) && sample_seconds >= 0.001f &&
            sample_seconds <= 0.1f && finite(p.origin) && finite(lifecycle.last_origin))
        {
            const auto derived = (p.origin - lifecycle.last_origin) * (1.0f / sample_seconds);
            if (finite(derived) && derived.length_sqr() > 25.0f)
                observed_velocity = derived;
        }
        if (finite(observed_velocity) && observed_velocity.length_sqr() > 25.0f)
        {
            p.velocity = observed_velocity;
            if (!lifecycle.launch_valid)
            {
                lifecycle.launch_position = lifecycle.last_origin;
                lifecycle.launch_velocity = observed_velocity;
                lifecycle.launch_valid = true;
            }
        }
        if (lifecycle.spawn_time <= 0.0f && p.spawn_time > 0.0f)
            lifecycle.spawn_time = p.spawn_time;
        lifecycle.last_origin = p.origin;
        lifecycle.last_seen = observed_now;
        p.launch_valid = lifecycle.launch_valid;
        if (lifecycle.launch_valid)
        {
            p.initial_position = lifecycle.launch_position;
            p.initial_velocity = lifecycle.launch_velocity;
        }

        p.remaining_lifetime = -1.0f;
        if (subtype == projectile_kind::he_grenade || subtype == projectile_kind::flashbang)
        {
            constexpr auto fuse_seconds = 1.640625f;
            constexpr auto simulation_step = 1.0f / 64.0f;
            const auto observed_age =
                std::chrono::duration<float>(observed_now - lifecycle_it->second.first_seen).count();
            auto remaining = fuse_seconds - std::max(0.0f, observed_age);

            if (std::isfinite(current_time) && std::isfinite(p.detonate_time))
            {
                const auto direct = p.detonate_time - current_time;

                if (direct > 0.0f && direct <= fuse_seconds + 0.25f && std::abs(direct - remaining) <= 0.25f)
                    remaining = direct;
            }

            p.remaining_lifetime = std::max(simulation_step, remaining);
        }

        if (subtype == projectile_kind::he_grenade || subtype == projectile_kind::flashbang)
        {
            const auto detonate_tick = m_services->process.load<std::int32_t>(
                entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin"_id));
            p.detonated = detonate_tick > 0;
        }
        else if (subtype == projectile_kind::smoke_grenade)
        {
            p.effect_tick_begin = m_services->process.load<std::int32_t>(
                entry.ptr + SCHEMA("C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin"_id));
            p.smoke_active =
                m_services->process.load<bool>(entry.ptr + SCHEMA("C_SmokeGrenadeProjectile", "m_bDidSmokeEffect"_id));
            p.smoke_detonation_pos = m_services->process.load<foundation::vec3>(
                entry.ptr + SCHEMA("C_SmokeGrenadeProjectile", "m_vSmokeDetonationPos"_id));
            p.smoke_voxel_size = m_services->process.load<std::int32_t>(
                entry.ptr + SCHEMA("C_SmokeGrenadeProjectile", "m_nVoxelFrameDataSize"_id));
            p.smoke_volume_received = m_services->process.load<bool>(
                entry.ptr + SCHEMA("C_SmokeGrenadeProjectile", "m_bSmokeVolumeDataReceived"_id));
            if (p.smoke_active && p.effect_tick_begin > 0)
                p.expire_time = static_cast<float>(p.effect_tick_begin) * (1.0f / 64.0f) + 20.0f;
        }
        else if (subtype == projectile_kind::decoy)
        {
            p.effect_tick_begin =
                m_services->process.load<std::int32_t>(entry.ptr + SCHEMA("C_DecoyProjectile", "m_nDecoyShotTick"_id));
        }
        p.in_flight = p.launch_valid && finite(p.velocity) && p.velocity.length_sqr() > 25.0f && !p.detonated &&
                      subtype != projectile_kind::molotov_fire;

        fresh.push_back(std::move(p));
    }

    std::ranges::sort(live_projectiles);
    live_projectiles.erase(std::unique(live_projectiles.begin(), live_projectiles.end()), live_projectiles.end());
    std::erase_if(lifecycles,
                  [&](const auto& item) { return !std::ranges::binary_search(live_projectiles, item.first); });

    {
        const auto view_origin = game::camera().origin();
        std::ranges::sort(fresh, [&view_origin](const projectile_snapshot& a, const projectile_snapshot& b)
                          { return view_origin.distance_sqr(a.origin) > view_origin.distance_sqr(b.origin); });
    }

    auto snapshot = std::make_shared<const std::vector<projectile_snapshot>>(std::move(fresh));
    m_projectiles.store(std::move(snapshot), std::memory_order_release);
}

void world_sampler::collect_spectators(const std::vector<entity_directory::cached>& raw)
{
    static const auto observer_pawn_offset = SCHEMA("CCSPlayerController", "m_hObserverPawn"_id);
    static const auto name_offset = SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_id);
    static const auto steamid_offset = SCHEMA("CBasePlayerController", "m_steamID"_id);
    static const auto alive_offset = SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_id);
    static const auto observer_services_offset = SCHEMA("C_BasePlayerPawn", "m_pObserverServices"_id);
    static const auto observer_mode_offset = SCHEMA("CPlayer_ObserverServices", "m_iObserverMode"_id);
    static const auto observer_target_offset = SCHEMA("CPlayer_ObserverServices", "m_hObserverTarget"_id);

    std::vector<spectator_snapshot> fresh{};
    fresh.reserve(16);
    const auto local_pawn = game::local_player().pawn();
    if (local_pawn)
    {
        const auto controller_first = std::min({observer_pawn_offset, name_offset, steamid_offset, alive_offset});
        const auto controller_last =
            std::max({observer_pawn_offset + sizeof(std::uint32_t), name_offset + sizeof(std::uintptr_t),
                      steamid_offset + sizeof(std::uint64_t), alive_offset + sizeof(bool)});

        for (const auto& entry : raw)
        {
            if (entry.type != entity_directory::type::player)
            {
                continue;
            }

            const remote_span controller(m_services->process, entry.ptr, controller_first, controller_last);
            // Observer handles survive a round transition briefly. Once the controller
            // owns a live pawn it is no longer a spectator, regardless of stale fields.
            if (controller.get<bool>(entry.ptr, alive_offset))
                continue;
            const auto observer_pawn_handle = controller.get<std::uint32_t>(entry.ptr, observer_pawn_offset);
            if (!observer_pawn_handle || observer_pawn_handle == 0xffffffffu)
            {
                continue;
            }
            const auto observer_pawn = game::entity_index().lookup(observer_pawn_handle);
            if (!observer_pawn)
            {
                continue;
            }

            const auto observer_services =
                m_services->process.load<std::uintptr_t>(observer_pawn + observer_services_offset);
            if (!observer_services)
            {
                continue;
            }
            const auto observer_first = std::min(observer_mode_offset, observer_target_offset);
            const auto observer_last =
                std::max(observer_mode_offset + sizeof(std::int32_t), observer_target_offset + sizeof(std::uint32_t));
            const remote_span observer(m_services->process, observer_services, observer_first, observer_last);
            const auto mode = observer.get<std::int32_t>(observer_services, observer_mode_offset);
            if (mode == 0)
            {
                continue;
            }
            const auto target_handle = observer.get<std::uint32_t>(observer_services, observer_target_offset);
            if (!target_handle || target_handle == 0xffffffffu ||
                game::entity_index().lookup(target_handle) != local_pawn)
            {
                continue;
            }

            const auto name_ptr = controller.get<std::uintptr_t>(entry.ptr, name_offset);
            auto name = cached_player_name(m_services->process, m_caches->names,
                                           {entry.ptr, name_ptr, 0, observer_pawn_handle});
            if (name.empty())
                name = "unknown";
            const auto steamid = controller.get<std::uint64_t>(entry.ptr, steamid_offset);
            fresh.push_back({std::move(name), steamid, mode});
        }
    }

    auto snapshot = std::make_shared<const std::vector<spectator_snapshot>>(std::move(fresh));
    m_local_spectated.store(!snapshot->empty(), std::memory_order_release);
    m_spectators.store(std::move(snapshot), std::memory_order_release);
}

world_item_kind world_sampler::classify_item(std::uint32_t schema_id)
{
    using subtype = world_item_kind;
    static constexpr auto catalog =
        std::to_array<std::pair<std::uint32_t, subtype>>({{"C_AK47"_id, subtype::ak47},
                                                          {"C_WeaponM4A1"_id, subtype::m4a4},
                                                          {"C_WeaponM4A1Silencer"_id, subtype::m4a1s},
                                                          {"C_WeaponAWP"_id, subtype::awp},
                                                          {"C_WeaponAug"_id, subtype::aug},
                                                          {"C_WeaponFamas"_id, subtype::famas},
                                                          {"C_WeaponGalilAR"_id, subtype::galil_ar},
                                                          {"C_WeaponSG556"_id, subtype::sg553},
                                                          {"C_WeaponG3SG1"_id, subtype::g3sg1},
                                                          {"C_WeaponSCAR20"_id, subtype::scar20},
                                                          {"C_WeaponSSG08"_id, subtype::ssg08},
                                                          {"C_WeaponMAC10"_id, subtype::mac10},
                                                          {"C_WeaponMP5SD"_id, subtype::mp5sd},
                                                          {"C_WeaponMP7"_id, subtype::mp7},
                                                          {"C_WeaponMP9"_id, subtype::mp9},
                                                          {"C_WeaponBizon"_id, subtype::pp_bizon},
                                                          {"C_WeaponP90"_id, subtype::p90},
                                                          {"C_WeaponUMP45"_id, subtype::ump45},
                                                          {"C_WeaponNOVA"_id, subtype::nova},
                                                          {"C_WeaponSawedoff"_id, subtype::sawed_off},
                                                          {"C_WeaponXM1014"_id, subtype::xm1014},
                                                          {"C_WeaponMag7"_id, subtype::mag7},
                                                          {"C_WeaponM249"_id, subtype::m249},
                                                          {"C_WeaponNegev"_id, subtype::negev},
                                                          {"C_DEagle"_id, subtype::deagle},
                                                          {"C_WeaponElite"_id, subtype::dual_berettas},
                                                          {"C_WeaponFiveSeven"_id, subtype::five_seven},
                                                          {"C_WeaponGlock"_id, subtype::glock},
                                                          {"C_WeaponHKP2000"_id, subtype::p2000},
                                                          {"C_WeaponUSPSilencer"_id, subtype::usps},
                                                          {"C_WeaponP250"_id, subtype::p250},
                                                          {"C_WeaponCZ75a"_id, subtype::cz75},
                                                          {"C_WeaponTec9"_id, subtype::tec9},
                                                          {"C_WeaponRevolver"_id, subtype::r8_revolver},
                                                          {"C_WeaponTaser"_id, subtype::taser},
                                                          {"C_Knife"_id, subtype::knife},
                                                          {"C_C4"_id, subtype::c4},
                                                          {"C_Item_Healthshot"_id, subtype::healthshot},
                                                          {"C_HEGrenade"_id, subtype::he_grenade},
                                                          {"C_Flashbang"_id, subtype::flashbang},
                                                          {"C_SmokeGrenade"_id, subtype::smoke_grenade},
                                                          {"C_MolotovGrenade"_id, subtype::molotov},
                                                          {"C_IncendiaryGrenade"_id, subtype::incendiary},
                                                          {"C_DecoyGrenade"_id, subtype::decoy}});

    const auto match = std::ranges::find(catalog, schema_id, [](const auto& entry) { return entry.first; });
    return match == catalog.end() ? subtype::unknown : match->second;
}

projectile_kind world_sampler::classify_projectile(std::uint32_t schema_id)
{
    using subtype = projectile_kind;
    static constexpr auto catalog =
        std::to_array<std::pair<std::uint32_t, subtype>>({{"C_HEGrenadeProjectile"_id, subtype::he_grenade},
                                                          {"C_FlashbangProjectile"_id, subtype::flashbang},
                                                          {"C_SmokeGrenadeProjectile"_id, subtype::smoke_grenade},
                                                          {"C_MolotovProjectile"_id, subtype::molotov},
                                                          {"C_Inferno"_id, subtype::molotov_fire},
                                                          {"C_DecoyProjectile"_id, subtype::decoy}});

    const auto match = std::ranges::find(catalog, schema_id, [](const auto& entry) { return entry.first; });
    return match == catalog.end() ? subtype::unknown : match->second;
}

} // namespace game
