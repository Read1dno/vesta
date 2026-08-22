#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <core/math/vector.hpp>
#include <simulation/target.hpp>

namespace game {

enum class world_item_kind : std::uint8_t
{
	unknown = 0,
	ak47, m4a4, m4a1s, awp, aug, famas, galil_ar, sg553,
	g3sg1, scar20, ssg08,
	mac10, mp5sd, mp7, mp9, pp_bizon, p90, ump45,
	nova, sawed_off, xm1014, mag7,
	m249, negev,
	deagle, dual_berettas, five_seven, glock, p2000, usps, p250, cz75, tec9, r8_revolver,
	taser, knife, c4, healthshot,
	he_grenade, flashbang, smoke_grenade, molotov, incendiary, decoy
};

enum class projectile_kind : std::uint8_t
{
	unknown = 0,
	he_grenade, flashbang, smoke_grenade, molotov, molotov_fire, decoy
};

struct equipped_weapon_snapshot
{
	std::uint32_t handle{};
	std::uint16_t item_definition{};
	std::uintptr_t ptr{};
	std::uintptr_t vdata{};
	std::string name{};
	std::int32_t ammo{};
	std::int32_t max_ammo{};
	bool pin_pulled{};
};

struct player_snapshot
{
	std::uintptr_t controller{};
	std::uintptr_t pawn{};
	std::uint32_t pawn_handle{};
	std::uintptr_t game_scene_node{};
	std::uintptr_t bone_cache{};
	foundation::vec3 origin{};
	foundation::vec3 velocity{};
	foundation::vec3 collision_center{};
	foundation::vec3 eye_angles{};
	float emit_sound_time{};
	float last_fired_time{};
	std::string display_name{};
	std::string model_path{};
	equipped_weapon_snapshot weapon{};

	std::vector<std::string> loadout{};
	std::uint64_t steamid{};
	std::int32_t comp_rank{};
	std::int32_t comp_wins{};
	std::int32_t comp_rank_type{};
	std::int32_t health{};
	std::int32_t team{};
	std::int32_t money{};
	std::int32_t ping{};
	std::int32_t armor{};
	std::int32_t simulation_tick{};
	std::int32_t controller_index{};
	float simulation_time{};
	std::uint64_t spotted_by_mask{};
	bool invulnerable{};
	bool has_helmet{};
	bool has_defuser{};
	bool is_scoped{};
	bool is_defusing{};
	bool is_ducked{};
	bool is_flashed{};
	bool is_visible{};
	bool is_spotted{};
	bool legit_visible{ true };
	float legit_opacity{ 1.0f };
	hitbox_catalog::set hitboxes{};
	skeleton_reader::data bones{};
};

struct world_item_snapshot
{
	std::uintptr_t entity{};
	std::uintptr_t game_scene_node{};
	foundation::vec3 origin{};
	world_item_kind subtype{ world_item_kind::unknown };
	std::int32_t ammo{};
	std::int32_t max_ammo{};
};

struct projectile_snapshot
{
	std::uintptr_t entity{};
	std::uintptr_t game_scene_node{};
	foundation::vec3 origin{};
	foundation::vec3 velocity{};
	foundation::vec3 initial_position{};
	foundation::vec3 initial_velocity{};
	projectile_kind subtype{ projectile_kind::unknown };
	std::uint32_t thrower_handle{};
	std::int32_t bounces{};
	std::int32_t effect_tick_begin{};
	float spawn_time{};
	float detonate_time{};
	float remaining_lifetime{ -1.0f };
	bool launch_valid{};
	bool in_flight{};
	bool detonated{};
	bool smoke_active{};
	foundation::vec3 smoke_detonation_pos{};
	std::int32_t smoke_voxel_size{};
	bool smoke_volume_received{};
	std::vector<foundation::vec3> fire_points{};
	float expire_time{};
};

struct spectator_snapshot
{
	std::string name{};
	std::uint64_t steamid{};
	int mode{};
};

}
