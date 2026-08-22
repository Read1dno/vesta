#include <stdafx.hpp>

namespace game {
namespace {
	[[nodiscard]] bool filtered_display_codepoint( const std::uint32_t value ) noexcept
	{
		return value <= 0x1Fu || ( value >= 0x7Fu && value <= 0x9Fu )
			|| value == 0x200Bu || value == 0x200Eu || value == 0x200Fu
			|| ( value >= 0x202Au && value <= 0x202Eu )
			|| ( value >= 0x2066u && value <= 0x2069u )
			|| value == 0xFEFFu;
	}

	[[nodiscard]] std::string sanitized_display_name( std::string_view source )
	{
		std::string result{};
		result.reserve( source.size( ) );
		std::size_t offset{};
		std::size_t codepoints{};
		constexpr std::string_view replacement{ "\xEF\xBF\xBD" };

		while ( offset < source.size( ) && codepoints < 64 )
		{
			const auto first = static_cast<std::uint8_t>( source[ offset ] );
			std::uint32_t value{};
			std::size_t length{};
			if ( first < 0x80u )
			{
				value = first;
				length = 1;
			}
			else if ( first >= 0xC2u && first <= 0xDFu )
			{
				value = first & 0x1Fu;
				length = 2;
			}
			else if ( first >= 0xE0u && first <= 0xEFu )
			{
				value = first & 0x0Fu;
				length = 3;
			}
			else if ( first >= 0xF0u && first <= 0xF4u )
			{
				value = first & 0x07u;
				length = 4;
			}

			bool valid = length != 0 && offset + length <= source.size( );
			for ( std::size_t index = 1; valid && index < length; ++index )
			{
				const auto continuation = static_cast<std::uint8_t>( source[ offset + index ] );
				valid = ( continuation & 0xC0u ) == 0x80u;
				value = ( value << 6u ) | ( continuation & 0x3Fu );
			}

			const auto minimum = length == 2 ? 0x80u : length == 3 ? 0x800u
				: length == 4 ? 0x10000u : 0u;
			valid = valid && value >= minimum && value <= 0x10FFFFu
				&& !( value >= 0xD800u && value <= 0xDFFFu );
			if ( !valid )
			{
				result.append( replacement );
				++offset;
				++codepoints;
				continue;
			}

			if ( !filtered_display_codepoint( value ) )
			{
				result.append( source.substr( offset, length ) );
				++codepoints;
			}
			offset += length;
		}
		return result;
	}

	[[nodiscard]] bool segment_intersects_sphere(
		const foundation::vec3& start, const foundation::vec3& end,
		const foundation::vec3& center, const float radius )
	{
		const auto segment = end - start;
		const auto length_sqr = segment.length_sqr( );
		if ( length_sqr <= 0.0001f )
		{
			return ( start - center ).length_sqr( ) <= radius * radius;
		}
		const auto t = std::clamp(
			( center - start ).dot( segment ) / length_sqr, 0.0f, 1.0f );
		return ( start + segment * t - center ).length_sqr( )
			<= radius * radius;
	}

	[[nodiscard]] bool line_through_active_smoke(
		const foundation::vec3& start, const foundation::vec3& end )
	{
		const auto projectiles = game::world().projectiles( );
		if ( !projectiles ) return false;
		for ( const auto& projectile : *projectiles )
		{
			if ( projectile.subtype != projectile_kind::smoke_grenade
				|| !projectile.smoke_active )
			{
				continue;
			}
			auto center = projectile.smoke_detonation_pos;
			if ( !std::isfinite( center.x ) || !std::isfinite( center.y )
				|| !std::isfinite( center.z ) || center.length_sqr( ) < 1.0f )
			{
				center = projectile.origin;
			}
			center.z += 45.0f;
			const auto radius = projectile.smoke_volume_received
				&& projectile.smoke_voxel_size > 0 ? 145.0f : 160.0f;
			if ( segment_intersects_sphere( start, end, center, radius ) )
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

	[[nodiscard]] sampling_demand current_sampling_demand( )
	{
		const auto key_down = []( const int key )
		{
			return key > 0 && ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0;
		};
		const auto combat = config::combat_settings.get(
			game::local_player().weapon_type( ) );
		const auto independent_seed = combat.triggerbot.seed_type
			!= config::combat_profile::seed_mode::none;
		const auto combat_players =
			combat.aimbot.draw_fov
			|| ( combat.aimbot.enabled && config::combat_profile::activation_active(
				combat.aimbot.activation_mode, combat.aimbot.key ) )
			|| ( combat.triggerbot.enabled && !independent_seed
				&& config::combat_profile::activation_active(
					combat.triggerbot.activation_mode, combat.triggerbot.key ) )
			|| ( config::combat_settings.global.grenade_aim.enabled
				&& key_down( config::combat_settings.global.grenade_aim.key ) );
		const auto& radar_cfg = config::visual_settings.m_radar;
		const auto radar_active = radar_cfg.active( );

		const auto players = game::world().script_demand(
			game::script_data_demand::players ) || app::context().menu.is_open( )
			|| config::visual_settings.m_player.active( )
			|| config::visual_settings.m_chams.enabled
			|| radar_active
			|| config::visual_settings.m_sound.enabled
			|| config::general_settings.m_bullet_tracers.enabled
			|| config::general_settings.m_hitmarker.enabled
			|| config::general_settings.m_hitsound.enabled
			|| config::general_settings.m_hitsound.show_damage
			|| combat_players;
		const auto items = game::world().script_demand(
			game::script_data_demand::items ) || config::visual_settings.m_item.enabled;
		const auto projectiles = game::world().script_demand(
			game::script_data_demand::projectiles ) || config::visual_settings.m_projectile.enabled
			|| config::visual_settings.m_no_smoke.enabled
			|| ( radar_active
				&& ( config::visual_settings.m_radar.show_projectiles
					|| config::visual_settings.m_radar.show_trajectories
					|| config::visual_settings.m_radar.show_grenade_zones ) )
			|| config::general_settings.m_grenades.enabled
			|| ( config::visual_settings.m_player.active( )
				&& config::visual_settings.m_player.m_legit_sync.enabled
				&& config::visual_settings.m_player.m_legit_sync.direct_visible )
			|| ( combat.aimbot.enabled && combat.aimbot.checks.smoke )
			|| ( combat.triggerbot.enabled && combat.triggerbot.checks.smoke );
		const auto spectators = game::world().script_demand(
			game::script_data_demand::spectators ) ||
			config::general_settings.m_spectator_list.enabled
			|| config::visual_settings.m_player.spectator_sync;
		const auto directory = players || items || projectiles || spectators
			|| config::visual_settings.m_bomb.enabled;
		return { players, items, projectiles, spectators, directory };
	}

	class remote_span
	{
	public:
		remote_span( std::uintptr_t object, std::uintptr_t first, std::uintptr_t last )
			: m_first( first ), m_size( last > first ? static_cast<std::size_t>( last - first ) : 0 )
		{
			m_valid = object && m_size > 0 && m_size <= m_bytes.size( )
				&& app::context().process.copy( object + first, m_bytes.data( ), m_size );
		}

		template <typename T>
		[[nodiscard]] T get( std::uintptr_t object, std::uintptr_t offset ) const
		{
			if ( m_valid && offset >= m_first
				&& offset - m_first + sizeof( T ) <= m_size )
			{
				T value{};
				std::memcpy( &value, m_bytes.data( ) + ( offset - m_first ), sizeof( value ) );
				return value;
			}
			return app::context().process.load<T>( object + offset );
		}

	private:
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

		bool operator==( const player_name_identity& ) const = default;
	};

	struct player_name_identity_hash
	{
		[[nodiscard]] std::size_t operator()( const player_name_identity& value ) const noexcept
		{
			auto hash = std::hash<std::uintptr_t>{}( value.controller );
			const auto combine = [ &hash ]( const std::size_t part )
			{
				hash ^= part + 0x9e3779b9u + ( hash << 6u ) + ( hash >> 2u );
			};
			combine( std::hash<std::uintptr_t>{}( value.name_ptr ) );
			combine( std::hash<std::uint64_t>{}( value.steamid ) );
			combine( std::hash<std::uint32_t>{}( value.pawn_handle ) );
			return hash;
		}
	};

	std::string cached_player_name( const player_name_identity& identity )
	{
		if ( !identity.name_ptr )
		{
			return {};
		}

		struct cached_name
		{
			std::string confirmed{};
			std::string candidate{};
			std::uint8_t candidate_count{};
			std::chrono::steady_clock::time_point next_validation{};
		};
		static std::unordered_map<player_name_identity, cached_name,
			player_name_identity_hash> cache{};
		if ( cache.empty( ) ) cache.reserve( 128 );
		if ( cache.size( ) > 256 ) cache.clear( );
		auto& entry = cache[ identity ];
		const auto now = std::chrono::steady_clock::now( );
		if ( !entry.confirmed.empty( ) && now < entry.next_validation )
		{
			return entry.confirmed;
		}

		auto name = sanitized_display_name(
			app::context().process.load_text( identity.name_ptr, 128 ) );
		if ( name.empty( ) )
			return entry.confirmed;
		if ( name == entry.confirmed )
		{
			entry.candidate.clear( );
			entry.candidate_count = 0;
			entry.next_validation = now + std::chrono::seconds( 1 );
			return entry.confirmed;
		}
		if ( name != entry.candidate )
		{
			entry.candidate = std::move( name );
			entry.candidate_count = 1;
			return entry.confirmed;
		}
		if ( ++entry.candidate_count >= 2 )
		{
			entry.confirmed = std::move( entry.candidate );
			entry.candidate_count = 0;
			entry.next_validation = now + std::chrono::seconds( 1 );
		}
		return entry.confirmed;
	}

	std::string cached_player_model_path( std::uintptr_t game_scene_node )
	{
		if ( !game_scene_node )
		{
			return {};
		}
		struct entry
		{
			std::uintptr_t identity{};
			std::string path{};
		};
		static std::unordered_map<std::uintptr_t, entry> cache{};
		const auto model_state = game_scene_node + 0x160;
		const auto direct_name = app::context().process.load<std::uintptr_t>( model_state + 0x88 );
		auto h_model = std::uintptr_t{};
		auto identity = direct_name;
		if ( !identity )
		{
			h_model = app::context().process.load<std::uintptr_t>( model_state + 0x80 );
			identity = h_model;
		}
		if ( const auto it = cache.find( game_scene_node );
			it != cache.end( ) && identity && it->second.identity == identity )
		{
			return it->second.path;
		}

		auto path = direct_name ? app::context().process.load_text( direct_name, 256 ) : std::string{};
		if ( path.empty( ) )
		{
			if ( !h_model ) h_model = app::context().process.load<std::uintptr_t>( model_state + 0x80 );
			const auto cmodel = h_model ? app::context().process.load<std::uintptr_t>( h_model ) : 0;
			const auto name_ptr = cmodel ? app::context().process.load<std::uintptr_t>( cmodel + 0x08 ) : 0;
			if ( name_ptr ) path = app::context().process.load_text( name_ptr, 256 );
		}
		if ( path.ends_with( ".vmdl" ) ) path += "_c";
		if ( identity && !path.empty( ) )
		{
			if ( cache.size( ) > 256 ) cache.clear( );
			cache[ game_scene_node ] = { identity, path };
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

	[[nodiscard]] bool confirm_weapon_observation( const std::uint32_t pawn_handle,
		const std::uint32_t handle, const std::uintptr_t weapon,
		const std::uintptr_t vdata, const std::uint16_t item_definition )
	{
		static std::unordered_map<std::uint32_t, weapon_observation> observations{};
		static std::uint32_t process_id{};
		const auto current_process = app::context().process.process_id( );
		if ( process_id != current_process )
		{
			observations.clear( );
			process_id = current_process;
		}
		if ( observations.empty( ) ) observations.reserve( 64 );
		if ( observations.size( ) > 128 ) observations.clear( );
		auto& current = observations[ pawn_handle ];
		if ( current.handle != handle || current.weapon != weapon
			|| current.vdata != vdata
			|| current.item_definition != item_definition )
		{
			current = { handle, weapon, vdata, item_definition, 1 };
			return false;
		}
		current.consecutive = static_cast<std::uint8_t>(
			std::min<int>( current.consecutive + 1, 2 ) );
		return current.consecutive >= 2;
	}

	weapon_metadata& cached_weapon_metadata( std::uintptr_t weapon_vdata, bool need_name,
		std::uint16_t item_definition = 0 )
	{

		static std::unordered_map<std::uintptr_t, weapon_metadata> cache{};
		static std::uint32_t process_id{};
		const auto current_process = app::context().process.process_id( );
		if ( process_id != current_process )
		{
			cache.clear( );
			process_id = current_process;
		}
		if ( cache.empty( ) )
		{
			cache.reserve( 128 );
		}

		auto [ it, inserted ] = cache.try_emplace( weapon_vdata );
		auto& metadata = it->second;

		if ( item_definition && metadata.item_definition
			&& metadata.item_definition != item_definition )
		{
			metadata = {};
		}
		if ( item_definition ) metadata.item_definition = item_definition;
		if ( inserted || !metadata.has_max_ammo )
		{
			std::int32_t max_ammo{};
			if ( app::context().process.copy(
				weapon_vdata + SCHEMA( "CBasePlayerWeaponVData", "m_iMaxClip1"_id ),
				&max_ammo, sizeof( max_ammo ) )
				&& max_ammo >= -1 && max_ammo <= 500 )
			{
				if ( metadata.candidate_max_ammo != max_ammo )
				{
					metadata.candidate_max_ammo = max_ammo;
					metadata.max_ammo_confirmations = 1;
				}
				else if ( ++metadata.max_ammo_confirmations >= 2 )
				{
					metadata.max_ammo = max_ammo;
					metadata.has_max_ammo = true;
				}
			}
		}

		if ( need_name && !metadata.has_name )
		{
			const auto weapon_name_ptr = app::context().process.load<std::uintptr_t>(
				weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
			if ( weapon_name_ptr )
			{
				auto name = app::context().process.load_text( weapon_name_ptr, 64 );
				if ( name.starts_with( "weapon_" ) )
				{
					name.erase( 0, 7 );
				}
				const auto plausible = !name.empty( ) && name.size( ) <= 48
					&& std::ranges::all_of( name, []( const unsigned char value )
					{
						return std::isalnum( value ) || value == '_';
					} );
				if ( plausible )
				{
					if ( metadata.candidate_name_ptr != weapon_name_ptr
						|| metadata.candidate_name != name )
					{
						metadata.candidate_name_ptr = weapon_name_ptr;
						metadata.candidate_name = std::move( name );
						metadata.name_confirmations = 1;
					}
					else if ( ++metadata.name_confirmations >= 2 )
					{
						metadata.name = std::move( metadata.candidate_name );
						metadata.has_name = true;
					}
				}
			}
		}

		return metadata;
	}
}

void world_sampler::run( )
{
	const auto demand = current_sampling_demand( );

	static auto next_spectator_update = std::chrono::steady_clock::time_point{};
	const auto now = std::chrono::steady_clock::now( );
	const auto spectator_sync = config::visual_settings.m_player.spectator_sync;
	const auto spectators_requested =
		demand.spectators
		&& ( spectator_sync || now >= next_spectator_update );
	if ( !demand.players && !demand.items && !demand.projectiles
		&& !spectators_requested )
	{
		return;
	}

	const auto raw = game::entity_index().all( );

	if ( spectators_requested )
	{
		next_spectator_update = now + ( spectator_sync
			? std::chrono::milliseconds( 15 ) : std::chrono::milliseconds( 200 ) );
		this->collect_spectators( *raw );
	}

	if ( demand.projectiles ) this->collect_projectiles( *raw );
	if ( demand.players ) this->collect_players( *raw );
	if ( demand.items ) this->collect_items( *raw );
}

std::shared_ptr<const std::vector<player_snapshot>> world_sampler::players( ) const
{
	return this->m_players.load( std::memory_order_acquire );
}

bool world_sampler::entity_directory_requested( ) const
{
	return current_sampling_demand( ).directory;
}

std::vector<player_snapshot> world_sampler::seed_players(
	std::uintptr_t local_pawn, std::uintptr_t local_controller,
	int local_team, bool free_for_all ) const
{
	std::vector<player_snapshot> fresh{};
	this->seed_players_into( fresh, local_pawn, local_controller,
		local_team, free_for_all );
	return fresh;
}

void world_sampler::seed_players_into(
	std::vector<player_snapshot>& fresh,
	std::uintptr_t local_pawn, std::uintptr_t local_controller,
	int local_team, bool free_for_all, std::uintptr_t only_pawn ) const
{

	static const auto controller_pawn =
		SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_id );
	static const auto controller_helmet =
		SCHEMA( "CCSPlayerController", "m_bPawnHasHelmet"_id );
	static const auto pawn_health =
		SCHEMA( "C_BaseEntity", "m_iHealth"_id );
	static const auto pawn_team =
		SCHEMA( "C_BaseEntity", "m_iTeamNum"_id );
	static const auto pawn_scene =
		SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id );
	static const auto pawn_immunity =
		SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_id );
	static const auto pawn_armor =
		SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_id );
	static const auto scene_origin =
		SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id );
	static const auto scene_model_state =
		SCHEMA( "CSkeletonInstance", "m_modelState"_id );

	fresh.clear( );
	if ( fresh.capacity( ) < 64 ) fresh.reserve( 64 );

	for ( std::uint32_t index = 1; index <= 64; ++index )
	{

		const auto controller = game::entity_index().lookup_index( index );
		if ( !controller || controller == local_controller )
		{
			continue;
		}

		const auto pawn_handle =
			app::context().process.load<std::uint32_t>( controller + controller_pawn );
		const auto pawn = game::entity_index().lookup( pawn_handle );
		if ( !pawn || pawn == local_pawn )
		{
			continue;
		}
		if ( only_pawn && pawn != only_pawn )
		{
			continue;
		}
		player_snapshot value{};
		value.controller = controller;
		value.pawn = pawn;
		value.health = app::context().process.load<std::int32_t>( pawn + pawn_health );
		value.team = app::context().process.load<std::int32_t>( pawn + pawn_team );
		value.invulnerable = app::context().process.load<bool>( pawn + pawn_immunity );
		if ( value.health <= 0 || value.health > 100
			|| ( value.team != 2 && value.team != 3 )
			|| ( !free_for_all && value.team == local_team )
			|| value.invulnerable )
		{
			continue;
		}

		value.armor = app::context().process.load<std::int32_t>( pawn + pawn_armor );
		value.game_scene_node =
			app::context().process.load<std::uintptr_t>( pawn + pawn_scene );
		if ( !value.game_scene_node )
		{
			continue;
		}

		value.origin = app::context().process.load<foundation::vec3>(
			value.game_scene_node + scene_origin );
		value.velocity = app::context().process.load<foundation::vec3>( pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		value.simulation_tick = app::context().process.load<std::int32_t>( pawn
			+ SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id ) );
		value.simulation_time = app::context().process.load<float>( pawn
			+ SCHEMA( "C_BaseEntity", "m_flSimulationTime"_id ) );
		if ( !std::isfinite( value.origin.x ) || !std::isfinite( value.origin.y )
			|| !std::isfinite( value.origin.z ) )
		{
			continue;
		}

		value.bone_cache = app::context().process.load<std::uintptr_t>(
			value.game_scene_node + scene_model_state + 0x80 );
		if ( !value.bone_cache )
		{
			continue;
		}
		value.bones = game::skeletons().get( value.bone_cache );
		if ( !value.bones.is_valid( ) )
		{
			continue;
		}

		value.has_helmet =
			app::context().process.load<bool>( controller + controller_helmet );
		value.hitboxes = game::hitbox_data().query(
			value.game_scene_node, false );
		if ( value.hitboxes.count < 3 )
		{
			continue;
		}
		fresh.push_back( std::move( value ) );
		if ( only_pawn ) break;
	}
}

std::shared_ptr<const std::vector<world_item_snapshot>> world_sampler::items( ) const
{
	return this->m_items.load( std::memory_order_acquire );
}

std::shared_ptr<const std::vector<projectile_snapshot>> world_sampler::projectiles( ) const
{
	return this->m_projectiles.load( std::memory_order_acquire );
}

std::shared_ptr<const std::vector<spectator_snapshot>> world_sampler::spectators( ) const
{
	return this->m_spectators.load( std::memory_order_acquire );
}

void world_sampler::collect_players( const std::vector<entity_directory::cached>& raw )
{
	auto view_origin = game::camera().origin( );
	auto unused_angles = foundation::vec3{};
	static_cast<void>( game::camera().sample( view_origin, unused_angles ) );

	struct offsets
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
		std::ptrdiff_t money_account;
	};
	static const offsets o{
		SCHEMA( "CCSPlayerController", "m_hPlayerPawn"_id ),
		SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_id ),
		SCHEMA( "CCSPlayerController", "m_iPing"_id ),
		SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_id ),
		SCHEMA( "CCSPlayerController", "m_iCompetitiveRanking"_id ),
		SCHEMA( "CCSPlayerController", "m_iCompetitiveWins"_id ),
		SCHEMA( "CCSPlayerController", "m_iCompetitiveRankType"_id ),
		SCHEMA( "CCSPlayerController", "m_pInGameMoneyServices"_id ),
		SCHEMA( "CBasePlayerController", "m_steamID"_id ),
		SCHEMA( "C_BaseEntity", "m_iHealth"_id ),
		SCHEMA( "C_BaseEntity", "m_iTeamNum"_id ),
		SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ),
		SCHEMA( "C_BaseModelEntity", "m_Collision"_id ),
		SCHEMA( "CCollisionProperty", "m_vecMins"_id ),
		SCHEMA( "CCollisionProperty", "m_vecMaxs"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_flEmitSoundTime"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_flLastFiredWeaponTime"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_entitySpottedState"_id ),
		SCHEMA( "EntitySpottedState_t", "m_bSpotted"_id ),
		SCHEMA( "EntitySpottedState_t", "m_bSpottedByMask"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_bIsDefusing"_id ),
		SCHEMA( "C_CSPlayerPawnBase", "m_flFlashBangTime"_id ),
		SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_id ),
		SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_id ),
		SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_id ),
		SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_id ),
		SCHEMA( "CCSPlayer_MovementServices", "m_bDucked"_id ),
		SCHEMA( "CGameSceneNode", "m_angAbsRotation"_id ),
		SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ),
		SCHEMA( "CSkeletonInstance", "m_modelState"_id ),
		SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_id ),
		SCHEMA( "CCSPlayer_ItemServices", "m_bHasDefuser"_id ),
		SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_id ),
		SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_id ),
		SCHEMA( "C_BaseEntity", "m_nSubclassID"_id ) + 0x8,
		SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_id ),
		SCHEMA( "CCSPlayerController_InGameMoneyServices", "m_iAccount"_id ),
	};

	std::vector<player_snapshot> fresh{};
	fresh.reserve( 64 );
	const auto sync_now = std::chrono::steady_clock::now( );
	static thread_local std::vector<std::uintptr_t> live_pawns{};
	live_pawns.clear( );
	if ( live_pawns.capacity( ) < 64 ) live_pawns.reserve( 64 );
	std::int32_t local_controller_index{ -1 };
	for ( const auto& entry : raw )
	{
		if ( entry.ptr == game::local_player().controller( ) )
		{
			local_controller_index = entry.index;
			break;
		}
	}

	for ( const auto& entry : raw )
	{
		if ( entry.type != entity_directory::type::player )
		{
			continue;
		}

		const auto controller_first = std::min( {
			o.controller_steamid, o.controller_money, o.controller_ping,
			o.controller_name, o.controller_rank, o.controller_wins,
			o.controller_rank_type, o.controller_pawn, o.controller_alive } );
		const auto controller_last = std::max( {
			o.controller_steamid + sizeof( std::uint64_t ),
			o.controller_money + sizeof( std::uintptr_t ),
			o.controller_ping + sizeof( std::int32_t ),
			o.controller_name + sizeof( std::uintptr_t ),
			o.controller_rank + sizeof( std::int32_t ),
			o.controller_wins + sizeof( std::int32_t ),
			o.controller_rank_type + sizeof( std::int32_t ),
			o.controller_pawn + sizeof( std::uint32_t ),
			o.controller_alive + sizeof( bool ) } );
		const remote_span controller_fields( entry.ptr, controller_first, controller_last );

		const auto player_pawn_handle = controller_fields.get<std::uint32_t>( entry.ptr, o.controller_pawn );
		if ( !player_pawn_handle || !controller_fields.get<bool>( entry.ptr, o.controller_alive ) )
		{
			continue;
		}

		const auto player_pawn = game::entity_index().lookup( player_pawn_handle );
		if ( !player_pawn || player_pawn == game::local_player().view_pawn( ) )
		{
			continue;
		}

		const auto collision_mins_offset = o.pawn_collision + o.collision_mins;
		const auto collision_maxs_offset = o.pawn_collision + o.collision_maxs;
		const auto pawn_base_first = std::min( {
			o.pawn_health, o.pawn_team, o.pawn_scene_node,
			collision_mins_offset, collision_maxs_offset } );
		const auto pawn_base_last = std::max( {
			o.pawn_health + sizeof( std::int32_t ),
			o.pawn_team + sizeof( std::int32_t ),
			o.pawn_scene_node + sizeof( std::uintptr_t ),
			collision_mins_offset + sizeof( foundation::vec3 ),
			collision_maxs_offset + sizeof( foundation::vec3 ) } );
		const remote_span pawn_base( player_pawn, pawn_base_first, pawn_base_last );

		const auto health = pawn_base.get<std::int32_t>( player_pawn, o.pawn_health );
		if ( health <= 0 || health > 100 )
		{
			continue;
		}

		player_snapshot p{};
		p.controller = entry.ptr;
		p.controller_index = entry.index;
		p.pawn_handle = player_pawn_handle;
		p.pawn = player_pawn;
		p.health = health;
		p.team = pawn_base.get<std::int32_t>( player_pawn, o.pawn_team );
		if ( p.team != 2 && p.team != 3 )
		{
			continue;
		}

		const auto spotted_offset = o.pawn_spotted_state + o.spotted;
		const auto spotted_mask_offset = o.pawn_spotted_state + o.spotted_by_mask;
		const auto status_first = std::min( {
			o.pawn_emit_sound, o.pawn_armor, o.pawn_scoped,
			o.pawn_defusing, o.pawn_flash_time, o.pawn_last_fired,
			spotted_offset, spotted_mask_offset } );
		const auto status_last = std::max( {
			o.pawn_emit_sound + sizeof( float ),
			o.pawn_last_fired + sizeof( float ),
			spotted_offset + sizeof( bool ),
			spotted_mask_offset + sizeof( std::uint64_t ),
			o.pawn_armor + sizeof( std::int32_t ),
			o.pawn_scoped + sizeof( bool ),
			o.pawn_defusing + sizeof( bool ),
			o.pawn_flash_time + sizeof( float ) } );
		const remote_span pawn_status( player_pawn, status_first, status_last );
		p.emit_sound_time = pawn_status.get<float>( player_pawn, o.pawn_emit_sound );
		p.last_fired_time = pawn_status.get<float>( player_pawn, o.pawn_last_fired );
		p.is_spotted = pawn_status.get<bool>( player_pawn, spotted_offset );
		p.spotted_by_mask = pawn_status.get<std::uint64_t>(
			player_pawn, spotted_mask_offset );
		p.armor = pawn_status.get<std::int32_t>( player_pawn, o.pawn_armor );
		p.is_scoped = pawn_status.get<bool>( player_pawn, o.pawn_scoped );
		p.is_defusing = pawn_status.get<bool>( player_pawn, o.pawn_defusing );
		p.is_flashed = pawn_status.get<float>( player_pawn, o.pawn_flash_time ) > 0.0f;

		const auto aim_first = std::min( o.pawn_immunity, o.pawn_eye_angles );
		const auto aim_last = std::max(
			o.pawn_immunity + sizeof( bool ),
			o.pawn_eye_angles + sizeof( foundation::vec3 ) );
		const remote_span pawn_aim( player_pawn, aim_first, aim_last );
		p.invulnerable = pawn_aim.get<bool>( player_pawn, o.pawn_immunity );
		p.eye_angles = pawn_aim.get<foundation::vec3>( player_pawn, o.pawn_eye_angles );
		p.ping = controller_fields.get<std::int32_t>( entry.ptr, o.controller_ping );

		const auto game_scene_node = pawn_base.get<std::uintptr_t>( player_pawn, o.pawn_scene_node );
		if ( !game_scene_node )
		{
			continue;
		}

		const auto bone_cache_offset = o.scene_model_state + 0x80;
		const auto scene_first = std::min( { o.scene_abs_rotation, o.scene_abs_origin, bone_cache_offset } );
		const auto scene_last = std::max( {
			o.scene_abs_rotation + sizeof( foundation::vec3 ),
			o.scene_abs_origin + sizeof( foundation::vec3 ),
			bone_cache_offset + sizeof( std::uintptr_t ) } );
		const remote_span scene_fields( game_scene_node, scene_first, scene_last );
		const auto abs_rot = scene_fields.get<foundation::vec3>( game_scene_node, o.scene_abs_rotation );
		if ( p.eye_angles.x == 0.0f && p.eye_angles.y == 0.0f )
		{
			p.eye_angles = abs_rot;
		}

		p.game_scene_node = game_scene_node;
		if ( config::visual_settings.m_chams.enabled )
		{
			p.model_path = cached_player_model_path( game_scene_node );
		}

		p.bone_cache = scene_fields.get<std::uintptr_t>( game_scene_node, bone_cache_offset );
		p.origin = scene_fields.get<foundation::vec3>( game_scene_node, o.scene_abs_origin );
		p.velocity = app::context().process.load<foundation::vec3>( player_pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		p.simulation_tick = app::context().process.load<std::int32_t>( player_pawn
			+ SCHEMA( "C_BaseEntity", "m_nSimulationTick"_id ) );
		p.simulation_time = app::context().process.load<float>( player_pawn
			+ SCHEMA( "C_BaseEntity", "m_flSimulationTime"_id ) );
		if ( !std::isfinite( p.origin.x ) || !std::isfinite( p.origin.y ) || !std::isfinite( p.origin.z ) )
		{
			continue;
		}

		const auto collision_mins = pawn_base.get<foundation::vec3>( player_pawn, collision_mins_offset );
		const auto collision_maxs = pawn_base.get<foundation::vec3>( player_pawn, collision_maxs_offset );
		p.collision_center = p.origin + ( collision_mins + collision_maxs ) * 0.5f;

		const auto services_first = std::min( {
			o.pawn_item_services, o.pawn_weapon_services, o.pawn_movement_services } );
		const auto services_last = std::max( {
			o.pawn_item_services + sizeof( std::uintptr_t ),
			o.pawn_weapon_services + sizeof( std::uintptr_t ),
			o.pawn_movement_services + sizeof( std::uintptr_t ) } );
		const remote_span services( player_pawn, services_first, services_last );
		const auto movement_services = services.get<std::uintptr_t>( player_pawn, o.pawn_movement_services );
		p.is_ducked = movement_services && app::context().process.load<bool>(
			movement_services + o.movement_ducked );

		p.bones = game::skeletons().get( p.bone_cache );
		if ( p.bones.is_valid( ) )
		{
			this->m_last_valid_bones[ p.pawn ] = {
				p.bone_cache, p.bones, sync_now };
		}
		else if ( const auto cached = this->m_last_valid_bones.find( p.pawn );
			cached != this->m_last_valid_bones.end( )
			&& cached->second.bone_cache == p.bone_cache
			&& sync_now - cached->second.timestamp <= std::chrono::milliseconds( 100 ) )
		{
			p.bones = cached->second.bones;
		}
		const auto head = p.bones.get_position( 7 );
		p.is_visible = p.bones.is_valid( ) && game::collision().valid( )
			&& !game::collision().trace_ray( view_origin, head ).hit;
		const auto& legit = config::visual_settings.m_player.m_legit_sync;
		const auto emit = this->m_last_emit_sound.find( p.pawn );
		if ( emit == this->m_last_emit_sound.end( ) )
		{
			this->m_last_emit_sound.emplace( p.pawn, p.emit_sound_time );
		}
		else
		{
			if ( p.emit_sound_time > emit->second + 0.001f
				&& view_origin.distance_sqr( p.origin )
					<= legit.sound_distance * legit.sound_distance )
			{
				this->m_last_sound_heard[ p.pawn ] = sync_now;
			}
			emit->second = p.emit_sound_time;
		}
		live_pawns.push_back( p.pawn );
		p.hitboxes = game::hitbox_data().query( game_scene_node );

		game::hitbox_data().remember( p.hitboxes );

		const auto item_services = services.get<std::uintptr_t>( player_pawn, o.pawn_item_services );
		if ( item_services )
		{
			const auto item_first = std::min( o.item_helmet, o.item_defuser );
			const auto item_last = std::max(
				o.item_helmet + sizeof( bool ), o.item_defuser + sizeof( bool ) );
			const remote_span item_fields( item_services, item_first, item_last );
			p.has_helmet = item_fields.get<bool>( item_services, o.item_helmet );
			p.has_defuser = item_fields.get<bool>( item_services, o.item_defuser );
		}

		const auto weapon_services = services.get<std::uintptr_t>( player_pawn, o.pawn_weapon_services );
		if ( weapon_services )
		{
			const auto active_weapon_handle = app::context().process.load<std::uint32_t>( weapon_services + o.active_weapon );
			if ( active_weapon_handle && active_weapon_handle != 0xFFFFFFFF )
			{
				p.weapon.ptr = game::entity_index().lookup( active_weapon_handle );
				if ( p.weapon.ptr )
				{
					p.weapon.handle = active_weapon_handle;

					p.weapon.vdata = app::context().process.load<std::uintptr_t>(
						p.weapon.ptr + o.weapon_subclass );
					p.weapon.item_definition = app::context().process.load<std::uint16_t>(
						p.weapon.ptr + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
						+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
						+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ) );

					const auto stable_before_metadata = p.weapon.vdata
						&& p.weapon.item_definition > 0
						&& app::context().process.load<std::uint32_t>(
							weapon_services + o.active_weapon ) == active_weapon_handle
						&& game::entity_index().lookup( active_weapon_handle ) == p.weapon.ptr;
					if ( stable_before_metadata && confirm_weapon_observation( player_pawn_handle,
						active_weapon_handle, p.weapon.ptr, p.weapon.vdata,
						p.weapon.item_definition ) )
					{
						p.weapon.ammo = app::context().process.load<std::int32_t>( p.weapon.ptr + o.weapon_ammo );
						const auto& metadata = cached_weapon_metadata(
							p.weapon.vdata, true, p.weapon.item_definition );
						p.weapon.max_ammo = metadata.max_ammo;
						p.weapon.name = metadata.name;
						const auto is_grenade = p.weapon.name == "flashbang"
							|| p.weapon.name == "hegrenade"
							|| p.weapon.name == "smokegrenade"
							|| p.weapon.name == "molotov"
							|| p.weapon.name == "incgrenade"
							|| p.weapon.name == "decoy";
						if ( is_grenade )
						{
							p.weapon.pin_pulled = app::context().process.load<bool>(
								p.weapon.ptr + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
						}
						const auto final_handle = app::context().process.load<std::uint32_t>(
							weapon_services + o.active_weapon );
						const auto final_vdata = app::context().process.load<std::uintptr_t>(
							p.weapon.ptr + o.weapon_subclass );
						const auto final_definition = app::context().process.load<std::uint16_t>(
							p.weapon.ptr + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
							+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
							+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ) );
						if ( final_handle != active_weapon_handle || final_vdata != p.weapon.vdata
							|| final_definition != p.weapon.item_definition )
						{
							p.weapon = {};
						}
					}
				}
			}

			struct cached_loadout
			{
				std::vector<std::string> items{};
				std::chrono::steady_clock::time_point refresh{};
			};
			static std::unordered_map<std::uint32_t, cached_loadout> loadouts{};
			if ( loadouts.empty( ) ) loadouts.reserve( 64 );
			if ( loadouts.size( ) > 128 ) loadouts.clear( );
			auto& loadout = loadouts[ player_pawn_handle ];
			if ( sync_now >= loadout.refresh )
			{
				loadout.refresh = sync_now + std::chrono::milliseconds( 250 );
				const auto owned_count = app::context().process.load<std::uint32_t>(
					weapon_services + o.owned_weapons );
				const auto owned_data = app::context().process.load<std::uintptr_t>(
					weapon_services + o.owned_weapons + 0x8 );
				if ( owned_data && owned_count > 0 && owned_count <= 32 )
				{
					std::array<std::uint32_t, 32> handles{};
					if ( app::context().process.copy( owned_data, handles.data( ),
						owned_count * sizeof( handles.front( ) ) ) )
					{
						std::vector<std::string> next{};
						next.reserve( owned_count );
						for ( std::uint32_t index = 0; index < owned_count; ++index )
						{
							const auto handle = handles[ index ];
							if ( !handle || handle == 0xffffffffu ) continue;
							const auto weapon = game::entity_index().lookup( handle );
							if ( !weapon ) continue;
							const auto vdata = app::context().process.load<std::uintptr_t>( weapon + o.weapon_subclass );
							const auto definition = app::context().process.load<std::uint16_t>(
								weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
								+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
								+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ) );
							if ( !vdata || !definition ) continue;
							const auto& metadata = cached_weapon_metadata( vdata, true, definition );
							if ( !metadata.name.empty( ) ) next.push_back( metadata.name );
						}
						loadout.items = std::move( next );
					}
				}
			}
			p.loadout = loadout.items;
		}

		p.steamid = controller_fields.get<std::uint64_t>( entry.ptr, o.controller_steamid );
		const auto name_ptr = controller_fields.get<std::uintptr_t>( entry.ptr, o.controller_name );
		p.display_name = cached_player_name( {
			entry.ptr, name_ptr, p.steamid, player_pawn_handle } );
		p.comp_rank = controller_fields.get<std::int32_t>( entry.ptr, o.controller_rank );
		p.comp_wins = controller_fields.get<std::int32_t>( entry.ptr, o.controller_wins );
		p.comp_rank_type = controller_fields.get<std::int32_t>( entry.ptr, o.controller_rank_type );

		const auto money_services = controller_fields.get<std::uintptr_t>( entry.ptr, o.controller_money );
		if ( money_services )
		{
			p.money = app::context().process.load<std::int32_t>( money_services + o.money_account );
		}

		fresh.push_back( std::move( p ) );
	}

	std::uint64_t friendly_controller_mask{};
	const auto add_controller_bit = [ & ]( const std::int32_t index )
	{

		if ( index >= 0 && index < 64 ) friendly_controller_mask |= 1ull << index;
	};
	add_controller_bit( local_controller_index );
	for ( const auto& player : fresh )
		if ( !game::local_player().is_enemy( player.team ) )
			add_controller_bit( player.controller_index );

	const auto& legit = config::visual_settings.m_player.m_legit_sync;
	const auto recently = [ & ]( const auto& events, const std::uintptr_t pawn,
		const float seconds )
	{
		const auto found = events.find( pawn );
		return found != events.end( )
			&& std::chrono::duration<float>( sync_now - found->second ).count( ) <= seconds;
	};
	for ( auto& player : fresh )
	{
		const auto relevant_mask = player.spotted_by_mask & friendly_controller_mask;
		auto& previous_mask = this->m_last_spotted_mask[ player.pawn ];
		const auto fresh_radar_edge = ( relevant_mask & ~previous_mask ) != 0;
		previous_mask = relevant_mask;

		bool teammate_spotted{};
		if ( legit.enabled && legit.radar
			&& game::local_player().is_enemy( player.team ) )
		{
			const auto target = player.bones.get_position( 7 );
			for ( const auto& teammate : fresh )
			{
				if ( teammate.pawn == player.pawn
					|| game::local_player().is_enemy( teammate.team ) ) continue;
				const auto observer = teammate.bones.get_position( 7 );
				auto direction = target - observer;
				if ( direction.length_sqr( ) < 1.0f ) continue;
				direction.normalize( );
				foundation::vec3 forward{};
				teammate.eye_angles.to_directions( &forward, nullptr, nullptr );
				forward.normalize( );
				if ( forward.dot( direction ) < 0.55f ) continue;
			if ( game::collision().valid( )
				&& !game::collision().trace_ray( observer, target ).hit )
				{
					teammate_spotted = true;
					break;
				}
			}
		}
		player.is_spotted = fresh_radar_edge || teammate_spotted;
		if ( player.is_spotted ) this->m_last_radar_seen[ player.pawn ] = sync_now;
		const auto spectator_blocked = config::visual_settings.m_player.spectator_sync
			&& this->local_spectated( );
		const auto direct_signal = !spectator_blocked && legit.direct_visible
			&& player.is_visible && !line_through_active_smoke(
				view_origin, player.bones.get_position( 7 ) );
		const auto indirect_signal = !spectator_blocked && (
			( legit.radar && recently( this->m_last_radar_seen,
				player.pawn, legit.radar_hold ) )
			|| ( legit.sound && recently( this->m_last_sound_heard,
				player.pawn, legit.sound_hold ) ) );

		if ( !legit.enabled )
		{
			player.legit_visible = !spectator_blocked;
			player.legit_opacity = player.legit_visible ? 1.0f : 0.0f;
			this->m_legit_fades.erase( player.pawn );
		}
		else if ( spectator_blocked )
		{
			player.legit_visible = false;
			player.legit_opacity = 0.0f;
			this->m_legit_fades.erase( player.pawn );
		}
		else
		{
			auto& fade = this->m_legit_fades[ player.pawn ];
			if ( fade.updated.time_since_epoch( ).count( ) == 0 )
			{
				fade.updated = sync_now;
				fade.pulse_started = sync_now;
			}
			const std::uint8_t signal_mode = direct_signal ? 2u
				: indirect_signal ? 1u : 0u;
			if ( signal_mode == 1u && fade.signal_mode != 1u )
				fade.pulse_started = sync_now;

			const auto delta = std::clamp(
				std::chrono::duration<float>( sync_now - fade.updated ).count( ),
				0.0f, 0.1f );
			if ( direct_signal )
			{
				fade.opacity = 1.0f;
			}
			else
			{
				float target{};
				if ( indirect_signal )
				{
					const auto elapsed = std::chrono::duration<float>(
						sync_now - fade.pulse_started ).count( );
					const auto phase = 0.5f + 0.5f * std::cos(
						elapsed * 2.0f * std::numbers::pi_v<float>
							/ std::max( legit.pulse_period, 0.01f ) );
					const auto smooth_phase = phase * phase * ( 3.0f - 2.0f * phase );
					target = std::lerp( legit.pulse_min_opacity,
						legit.pulse_max_opacity, smooth_phase );
				}
				const auto response = indirect_signal ? 0.12f : 0.28f;
				const auto blend = delta > 0.0f
					? 1.0f - std::exp( -delta / response ) : 0.0f;
				fade.opacity = std::lerp( fade.opacity, target, blend );
			}
			if ( signal_mode == 0u && fade.opacity < 0.01f ) fade.opacity = 0.0f;
			fade.signal_mode = signal_mode;
			fade.updated = sync_now;

			player.legit_opacity = std::clamp( fade.opacity, 0.0f, 1.0f );
			player.legit_visible = player.legit_opacity > 0.0f;
		}
	}

	std::ranges::sort( fresh,[ &view_origin ]( const player_snapshot& a, const player_snapshot& b ) {
		return view_origin.distance_sqr( a.origin ) > view_origin.distance_sqr( b.origin );
	} );
	std::ranges::sort( live_pawns );
	live_pawns.erase( std::unique( live_pawns.begin( ), live_pawns.end( ) ),
		live_pawns.end( ) );
	const auto prune = [ & ]( auto& values )
	{
		std::erase_if( values, [ & ]( const auto& item )
			{ return !std::ranges::binary_search( live_pawns, item.first ); } );
	};
	prune( this->m_last_emit_sound );
	prune( this->m_last_radar_seen );
	prune( this->m_last_spotted_mask );
	prune( this->m_last_sound_heard );
	prune( this->m_legit_fades );
	prune( this->m_last_valid_bones );

	auto snapshot = std::make_shared<const std::vector<player_snapshot>>( std::move( fresh ) );
	this->m_players.store( std::move( snapshot ), std::memory_order_release );
}

void world_sampler::collect_items( const std::vector<entity_directory::cached>& raw )
{
	std::vector<world_item_snapshot> fresh{};
	fresh.reserve( 64 );

	for ( const auto& entry : raw )
	{
		if ( entry.type != entity_directory::type::item )
		{
			continue;
		}

		const auto subtype = classify_item( entry.schema_id );
		if ( subtype == world_item_kind::unknown )
		{
			continue;
		}

		const auto owner_handle = app::context().process.load<std::uint32_t>( entry.ptr + SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_id ) );
		if ( owner_handle && owner_handle != 0xffffffff )
		{
			continue;
		}

		const auto game_scene_node = app::context().process.load<std::uintptr_t>( entry.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
		if ( !game_scene_node )
		{
			continue;
		}

		world_item_snapshot i{};
		i.entity = entry.ptr;
		i.game_scene_node = game_scene_node;
		i.subtype = subtype;
		i.origin = app::context().process.load<foundation::vec3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
		i.ammo = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_id ) );

		const auto weapon_vdata = app::context().process.load<std::uintptr_t>( entry.ptr + SCHEMA( "C_BaseEntity", "m_nSubclassID"_id ) + 0x8 );
		if ( weapon_vdata )
		{
			i.max_ammo = cached_weapon_metadata( weapon_vdata, false ).max_ammo;
		}

		fresh.push_back( std::move( i ) );
	}

	{
		const auto view_origin = game::camera().origin( );
		std::ranges::sort( fresh, [ &view_origin ]( const world_item_snapshot& a, const world_item_snapshot& b ) {
			return view_origin.distance_sqr( a.origin ) > view_origin.distance_sqr( b.origin );
		} );
	}

	auto snapshot = std::make_shared<const std::vector<world_item_snapshot>>( std::move( fresh ) );
	this->m_items.store( std::move( snapshot ), std::memory_order_release );
}

void world_sampler::collect_projectiles( const std::vector<entity_directory::cached>& raw )
{
	struct projectile_lifecycle
	{
		projectile_kind subtype{ projectile_kind::unknown };
		std::chrono::steady_clock::time_point first_seen{};
		std::chrono::steady_clock::time_point last_seen{};
		foundation::vec3 last_origin{};
		foundation::vec3 launch_position{};
		foundation::vec3 launch_velocity{};
		float spawn_time{};
		bool launch_valid{};
		bool launch_from_network{};
	};
	static std::unordered_map<std::uintptr_t, projectile_lifecycle> lifecycles{};
	static thread_local std::vector<std::uintptr_t> live_projectiles{};
	const auto observed_now = std::chrono::steady_clock::now( );
	live_projectiles.clear( );
	live_projectiles.reserve( 32 );

	std::vector<projectile_snapshot> fresh{};
	fresh.reserve( 32 );

	const auto current_time = app::context().process.load<float>( app::context().process.load<std::uintptr_t>( app::context().addresses.global_vars ) + 0x30 );

	for ( const auto& entry : raw )
	{
		if ( entry.type != entity_directory::type::projectile )
		{
			continue;
		}

		const auto subtype = classify_projectile( entry.schema_id );
		if ( subtype == projectile_kind::unknown )
		{
			continue;
		}

		if ( subtype == projectile_kind::molotov_fire )
		{
			const auto fire_count = app::context().process.load<int>( entry.ptr + SCHEMA( "C_Inferno", "m_fireCount"_id ) );
			if ( fire_count <= 0 )
			{
				continue;
			}

			const auto bounded_count = static_cast<std::size_t>( std::min( fire_count, 64 ) );
			std::array<std::uint8_t, 64> active{};
			std::array<foundation::vec3, 64> positions{};
			const auto active_base = entry.ptr + SCHEMA( "C_Inferno", "m_bFireIsBurning"_id );
			const auto positions_base = entry.ptr + SCHEMA( "C_Inferno", "m_firePositions"_id );

			if ( !app::context().process.copy( active_base, active.data( ), bounded_count ) ||
				 !app::context().process.copy( positions_base, positions.data( ), bounded_count * sizeof( foundation::vec3 ) ) )
			{
				continue;
			}

			std::vector<foundation::vec3> fire_points{};
			fire_points.reserve( bounded_count );
			for ( std::size_t i = 0; i < bounded_count; ++i )
			{
				if ( active[ i ] && std::isfinite( positions[ i ].x ) && std::isfinite( positions[ i ].y ) && std::isfinite( positions[ i ].z ) )
				{
					fire_points.push_back( positions[ i ] );
				}
			}

			if ( fire_points.empty( ) )
			{
				continue;
			}

			auto center = foundation::vec3{};
			for ( const auto& point : fire_points )
			{
				center = center + point;
			}
			center = center * ( 1.0f / static_cast<float>( fire_points.size( ) ) );

			const auto effect_tick = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_Inferno", "m_nFireEffectTickBegin"_id ) );
			const auto start_time = static_cast< float >( effect_tick ) * ( 1.0f / 64.0f );
			constexpr auto inferno_duration = 7.0f;

			projectile_snapshot p{};
			p.entity = entry.ptr;
			p.subtype = subtype;
			p.origin = center;
			p.fire_points = std::move( fire_points );
			p.expire_time = start_time + inferno_duration;

			fresh.push_back( std::move( p ) );
			continue;
		}

		const auto game_scene_node = app::context().process.load<std::uintptr_t>( entry.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_id ) );
		if ( !game_scene_node )
		{
			continue;
		}

		projectile_snapshot p{};
		p.entity = entry.ptr;
		p.game_scene_node = game_scene_node;
		p.subtype = subtype;
		p.origin = app::context().process.load<foundation::vec3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_id ) );
		p.velocity = app::context().process.load<foundation::vec3>( entry.ptr + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		p.initial_position = app::context().process.load<foundation::vec3>( entry.ptr
			+ SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_id ) );
		p.initial_velocity = app::context().process.load<foundation::vec3>( entry.ptr
			+ SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_id ) );
		p.thrower_handle = app::context().process.load<std::uint32_t>( entry.ptr + SCHEMA( "C_BaseGrenade", "m_hThrower"_id ) );
		p.bounces = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_nBounces"_id ) );
		p.spawn_time = app::context().process.load<float>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_flSpawnTime"_id ) );
		p.detonate_time = app::context().process.load<float>( entry.ptr + SCHEMA( "C_BaseGrenade", "m_flDetonateTime"_id ) );
		live_projectiles.push_back( entry.ptr );
		auto [ lifecycle_it, inserted ] = lifecycles.try_emplace( entry.ptr );
		auto& lifecycle = lifecycle_it->second;
		const auto recycled = !inserted && ( lifecycle.subtype != subtype
			|| ( p.spawn_time > 0.0f && lifecycle.spawn_time > 0.0f
				&& std::abs( p.spawn_time - lifecycle.spawn_time ) > 0.001f ) );
		if ( inserted || recycled )
		{
			lifecycle = {};
			lifecycle.subtype = subtype;
			lifecycle.first_seen = observed_now;
			lifecycle.last_seen = observed_now;
			lifecycle.last_origin = p.origin;
			lifecycle.spawn_time = p.spawn_time;
		}

		const auto finite = []( const foundation::vec3& value )
		{
			return std::isfinite( value.x ) && std::isfinite( value.y )
				&& std::isfinite( value.z );
		};
		const auto network_launch_valid = finite( p.initial_position )
			&& finite( p.initial_velocity )
			&& p.initial_position.length_sqr( ) > 1.0f
			&& p.initial_velocity.length_sqr( ) > 25.0f;
		if ( network_launch_valid && !lifecycle.launch_from_network )
		{
			lifecycle.launch_position = p.initial_position;
			lifecycle.launch_velocity = p.initial_velocity;
			lifecycle.launch_valid = true;
			lifecycle.launch_from_network = true;
		}
		auto observed_velocity = p.velocity;
		const auto sample_seconds = std::chrono::duration<float>(
			observed_now - lifecycle.last_seen ).count( );
		if ( ( !finite( observed_velocity ) || observed_velocity.length_sqr( ) <= 25.0f )
			&& sample_seconds >= 0.001f && sample_seconds <= 0.1f
			&& finite( p.origin ) && finite( lifecycle.last_origin ) )
		{
			const auto derived = ( p.origin - lifecycle.last_origin )
				* ( 1.0f / sample_seconds );
			if ( finite( derived ) && derived.length_sqr( ) > 25.0f )
				observed_velocity = derived;
		}
		if ( finite( observed_velocity ) && observed_velocity.length_sqr( ) > 25.0f )
		{
			p.velocity = observed_velocity;
			if ( !lifecycle.launch_valid )
			{
				lifecycle.launch_position = lifecycle.last_origin;
				lifecycle.launch_velocity = observed_velocity;
				lifecycle.launch_valid = true;
			}
		}
		if ( lifecycle.spawn_time <= 0.0f && p.spawn_time > 0.0f )
			lifecycle.spawn_time = p.spawn_time;
		lifecycle.last_origin = p.origin;
		lifecycle.last_seen = observed_now;
		p.launch_valid = lifecycle.launch_valid;
		if ( lifecycle.launch_valid )
		{
			p.initial_position = lifecycle.launch_position;
			p.initial_velocity = lifecycle.launch_velocity;
		}

		p.remaining_lifetime = -1.0f;
		if ( subtype == projectile_kind::he_grenade
			|| subtype == projectile_kind::flashbang )
		{
			constexpr auto fuse_seconds = 1.640625f;
			constexpr auto simulation_step = 1.0f / 64.0f;
			const auto observed_age = std::chrono::duration<float>(
				observed_now - lifecycle_it->second.first_seen ).count( );
			auto remaining = fuse_seconds - std::max( 0.0f, observed_age );

			if ( std::isfinite( current_time ) && std::isfinite( p.detonate_time ) )
			{
				const auto direct = p.detonate_time - current_time;

				if ( direct > 0.0f && direct <= fuse_seconds + 0.25f
					&& std::abs( direct - remaining ) <= 0.25f )
					remaining = direct;
			}

			p.remaining_lifetime = std::max( simulation_step, remaining );
		}

		if ( subtype == projectile_kind::he_grenade || subtype == projectile_kind::flashbang )
		{
			const auto detonate_tick = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin"_id ) );
			p.detonated = detonate_tick > 0;
		}
		else if ( subtype == projectile_kind::smoke_grenade )
		{
			p.effect_tick_begin = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin"_id ) );
			p.smoke_active = app::context().process.load<bool>( entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_bDidSmokeEffect"_id ) );
			p.smoke_detonation_pos = app::context().process.load<foundation::vec3>(
				entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_vSmokeDetonationPos"_id ) );
			p.smoke_voxel_size = app::context().process.load<std::int32_t>(
				entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_nVoxelFrameDataSize"_id ) );
			p.smoke_volume_received = app::context().process.load<bool>(
				entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_bSmokeVolumeDataReceived"_id ) );
			if ( p.smoke_active && p.effect_tick_begin > 0 )
				p.expire_time = static_cast<float>( p.effect_tick_begin )
					* ( 1.0f / 64.0f ) + 20.0f;
		}
		else if ( subtype == projectile_kind::decoy )
		{
			p.effect_tick_begin = app::context().process.load<std::int32_t>( entry.ptr + SCHEMA( "C_DecoyProjectile", "m_nDecoyShotTick"_id ) );
		}
		p.in_flight = p.launch_valid && finite( p.velocity )
			&& p.velocity.length_sqr( ) > 25.0f && !p.detonated
			&& subtype != projectile_kind::molotov_fire;

		fresh.push_back( std::move( p ) );
	}

	std::ranges::sort( live_projectiles );
	live_projectiles.erase( std::unique( live_projectiles.begin( ), live_projectiles.end( ) ),
		live_projectiles.end( ) );
	std::erase_if( lifecycles, [ & ]( const auto& item )
		{ return !std::ranges::binary_search( live_projectiles, item.first ); } );

	{
		const auto view_origin = game::camera().origin( );
		std::ranges::sort( fresh, [ &view_origin ]( const projectile_snapshot& a, const projectile_snapshot& b ) {
			return view_origin.distance_sqr( a.origin ) > view_origin.distance_sqr( b.origin );
		} );
	}

	auto snapshot = std::make_shared<const std::vector<projectile_snapshot>>( std::move( fresh ) );
	this->m_projectiles.store( std::move( snapshot ), std::memory_order_release );
}

void world_sampler::collect_spectators( const std::vector<entity_directory::cached>& raw )
{
	static const auto observer_pawn_offset =
		SCHEMA( "CCSPlayerController", "m_hObserverPawn"_id );
	static const auto name_offset = SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_id );
	static const auto steamid_offset = SCHEMA( "CBasePlayerController", "m_steamID"_id );
	static const auto alive_offset = SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_id );
	static const auto observer_services_offset =
		SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_id );
	static const auto observer_mode_offset =
		SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_id );
	static const auto observer_target_offset =
		SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_id );

	std::vector<spectator_snapshot> fresh{};
	fresh.reserve( 16 );
	const auto local_pawn = game::local_player().pawn( );
	if ( local_pawn )
	{
		const auto controller_first = std::min(
			{ observer_pawn_offset, name_offset, steamid_offset, alive_offset } );
		const auto controller_last = std::max(
			{ observer_pawn_offset + sizeof( std::uint32_t ),
			name_offset + sizeof( std::uintptr_t ),
			steamid_offset + sizeof( std::uint64_t ),
			alive_offset + sizeof( bool ) } );

		for ( const auto& entry : raw )
		{
			if ( entry.type != entity_directory::type::player )
			{
				continue;
			}

			const remote_span controller( entry.ptr, controller_first, controller_last );

			if ( controller.get<bool>( entry.ptr, alive_offset ) ) continue;
			const auto observer_pawn_handle = controller.get<std::uint32_t>(
				entry.ptr, observer_pawn_offset );
			if ( !observer_pawn_handle || observer_pawn_handle == 0xffffffffu )
			{
				continue;
			}
			const auto observer_pawn = game::entity_index().lookup( observer_pawn_handle );
			if ( !observer_pawn )
			{
				continue;
			}

			const auto observer_services = app::context().process.load<std::uintptr_t>(
				observer_pawn + observer_services_offset );
			if ( !observer_services )
			{
				continue;
			}
			const auto observer_first = std::min( observer_mode_offset, observer_target_offset );
			const auto observer_last = std::max(
				observer_mode_offset + sizeof( std::int32_t ),
				observer_target_offset + sizeof( std::uint32_t ) );
			const remote_span observer( observer_services, observer_first, observer_last );
			const auto mode = observer.get<std::int32_t>( observer_services, observer_mode_offset );
			if ( mode == 0 )
			{
				continue;
			}
			const auto target_handle = observer.get<std::uint32_t>(
				observer_services, observer_target_offset );
			if ( !target_handle || target_handle == 0xffffffffu
				|| game::entity_index().lookup( target_handle ) != local_pawn )
			{
				continue;
			}

			const auto name_ptr = controller.get<std::uintptr_t>( entry.ptr, name_offset );
			auto name = cached_player_name( {
				entry.ptr, name_ptr, 0, observer_pawn_handle } );
			if ( name.empty( ) ) name = "unknown";
			const auto steamid = controller.get<std::uint64_t>( entry.ptr, steamid_offset );
			fresh.push_back( { std::move( name ), steamid, mode } );
		}
	}

	auto snapshot = std::make_shared<const std::vector<spectator_snapshot>>( std::move( fresh ) );
	this->m_local_spectated.store( !snapshot->empty( ), std::memory_order_release );
	this->m_spectators.store( std::move( snapshot ), std::memory_order_release );
}

world_item_kind world_sampler::classify_item( std::uint32_t schema_id )
{
	using subtype = world_item_kind;
	static constexpr auto catalog = std::to_array<std::pair<std::uint32_t, subtype>>( {
		{ "C_AK47"_id, subtype::ak47 },
		{ "C_WeaponM4A1"_id, subtype::m4a4 },
		{ "C_WeaponM4A1Silencer"_id, subtype::m4a1s },
		{ "C_WeaponAWP"_id, subtype::awp },
		{ "C_WeaponAug"_id, subtype::aug },
		{ "C_WeaponFamas"_id, subtype::famas },
		{ "C_WeaponGalilAR"_id, subtype::galil_ar },
		{ "C_WeaponSG556"_id, subtype::sg553 },
		{ "C_WeaponG3SG1"_id, subtype::g3sg1 },
		{ "C_WeaponSCAR20"_id, subtype::scar20 },
		{ "C_WeaponSSG08"_id, subtype::ssg08 },
		{ "C_WeaponMAC10"_id, subtype::mac10 },
		{ "C_WeaponMP5SD"_id, subtype::mp5sd },
		{ "C_WeaponMP7"_id, subtype::mp7 },
		{ "C_WeaponMP9"_id, subtype::mp9 },
		{ "C_WeaponBizon"_id, subtype::pp_bizon },
		{ "C_WeaponP90"_id, subtype::p90 },
		{ "C_WeaponUMP45"_id, subtype::ump45 },
		{ "C_WeaponNOVA"_id, subtype::nova },
		{ "C_WeaponSawedoff"_id, subtype::sawed_off },
		{ "C_WeaponXM1014"_id, subtype::xm1014 },
		{ "C_WeaponMag7"_id, subtype::mag7 },
		{ "C_WeaponM249"_id, subtype::m249 },
		{ "C_WeaponNegev"_id, subtype::negev },
		{ "C_DEagle"_id, subtype::deagle },
		{ "C_WeaponElite"_id, subtype::dual_berettas },
		{ "C_WeaponFiveSeven"_id, subtype::five_seven },
		{ "C_WeaponGlock"_id, subtype::glock },
		{ "C_WeaponHKP2000"_id, subtype::p2000 },
		{ "C_WeaponUSPSilencer"_id, subtype::usps },
		{ "C_WeaponP250"_id, subtype::p250 },
		{ "C_WeaponCZ75a"_id, subtype::cz75 },
		{ "C_WeaponTec9"_id, subtype::tec9 },
		{ "C_WeaponRevolver"_id, subtype::r8_revolver },
		{ "C_WeaponTaser"_id, subtype::taser },
		{ "C_Knife"_id, subtype::knife },
		{ "C_C4"_id, subtype::c4 },
		{ "C_Item_Healthshot"_id, subtype::healthshot },
		{ "C_HEGrenade"_id, subtype::he_grenade },
		{ "C_Flashbang"_id, subtype::flashbang },
		{ "C_SmokeGrenade"_id, subtype::smoke_grenade },
		{ "C_MolotovGrenade"_id, subtype::molotov },
		{ "C_IncendiaryGrenade"_id, subtype::incendiary },
		{ "C_DecoyGrenade"_id, subtype::decoy }
	} );

	const auto match = std::ranges::find( catalog, schema_id,
		[]( const auto& entry ) { return entry.first; } );
	return match == catalog.end( ) ? subtype::unknown : match->second;
}

projectile_kind world_sampler::classify_projectile( std::uint32_t schema_id )
{
	using subtype = projectile_kind;
	static constexpr auto catalog = std::to_array<std::pair<std::uint32_t, subtype>>( {
		{ "C_HEGrenadeProjectile"_id, subtype::he_grenade },
		{ "C_FlashbangProjectile"_id, subtype::flashbang },
		{ "C_SmokeGrenadeProjectile"_id, subtype::smoke_grenade },
		{ "C_MolotovProjectile"_id, subtype::molotov },
		{ "C_Inferno"_id, subtype::molotov_fire },
		{ "C_DecoyProjectile"_id, subtype::decoy }
	} );

	const auto match = std::ranges::find( catalog, schema_id,
		[]( const auto& entry ) { return entry.first; } );
	return match == catalog.end( ) ? subtype::unknown : match->second;
}

}
