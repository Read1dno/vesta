#pragma once

#include <atomic>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <vector>

#include <core/memory/catalogs.hpp>
#include <core/state/entities.hpp>

namespace game {

enum class script_data_demand : std::uint32_t
{
	none = 0,
	players = 1u << 0,
	items = 1u << 1,
	projectiles = 1u << 2,
	spectators = 1u << 3,
	bomb = 1u << 4,
	all = players | items | projectiles | spectators | bomb
};

[[nodiscard]] constexpr script_data_demand operator|( const script_data_demand left,
	const script_data_demand right ) noexcept
{
	return static_cast<script_data_demand>( static_cast<std::uint32_t>( left )
		| static_cast<std::uint32_t>( right ) );
}

class world_sampler
{
public:
	void run( );
	[[nodiscard]] bool entity_directory_requested( ) const;
	void set_script_demand( const script_data_demand demand ) noexcept
	{
		m_script_demand.store( static_cast<std::uint32_t>( demand ),
			std::memory_order_release );
	}
	[[nodiscard]] bool script_demand( ) const noexcept
	{
		return m_script_demand.load( std::memory_order_acquire ) != 0;
	}
	[[nodiscard]] bool script_demand( const script_data_demand demand ) const noexcept
	{
		return ( m_script_demand.load( std::memory_order_acquire )
			& static_cast<std::uint32_t>( demand ) ) != 0;
	}

	[[nodiscard]] std::shared_ptr<const std::vector<player_snapshot>> players( ) const;
	[[nodiscard]] std::shared_ptr<const std::vector<world_item_snapshot>> items( ) const;
	[[nodiscard]] std::shared_ptr<const std::vector<projectile_snapshot>> projectiles( ) const;
	[[nodiscard]] std::shared_ptr<const std::vector<spectator_snapshot>> spectators( ) const;
	[[nodiscard]] bool local_spectated( ) const noexcept
	{
		return m_local_spectated.load( std::memory_order_acquire );
	}
	[[nodiscard]] std::vector<player_snapshot> seed_players( std::uintptr_t local_pawn,
		std::uintptr_t local_controller, int local_team, bool free_for_all ) const;
	void seed_players_into( std::vector<player_snapshot>& destination,
		std::uintptr_t local_pawn, std::uintptr_t local_controller,
		int local_team, bool free_for_all,
		std::uintptr_t only_pawn = 0 ) const;

private:
	void collect_players( const std::vector<entity_directory::cached>& raw );
	void collect_items( const std::vector<entity_directory::cached>& raw );
	void collect_projectiles( const std::vector<entity_directory::cached>& raw );
	void collect_spectators( const std::vector<entity_directory::cached>& raw );

	[[nodiscard]] static world_item_kind classify_item( std::uint32_t schema_id );
	[[nodiscard]] static projectile_kind classify_projectile( std::uint32_t schema_id );

	std::atomic<std::shared_ptr<const std::vector<player_snapshot>>> m_players{
		std::make_shared<std::vector<player_snapshot>>( ) };
	std::atomic<std::shared_ptr<const std::vector<world_item_snapshot>>> m_items{
		std::make_shared<std::vector<world_item_snapshot>>( ) };
	std::atomic<std::shared_ptr<const std::vector<projectile_snapshot>>> m_projectiles{
		std::make_shared<std::vector<projectile_snapshot>>( ) };
	std::atomic<std::shared_ptr<const std::vector<spectator_snapshot>>> m_spectators{
		std::make_shared<std::vector<spectator_snapshot>>( ) };
	std::atomic_bool m_local_spectated{};
	std::atomic<std::uint32_t> m_script_demand{};
	std::unordered_map<std::uintptr_t, float> m_last_emit_sound{};
	std::unordered_map<std::uintptr_t, std::chrono::steady_clock::time_point> m_last_radar_seen{};
	std::unordered_map<std::uintptr_t, std::uint64_t> m_last_spotted_mask{};
	std::unordered_map<std::uintptr_t, std::chrono::steady_clock::time_point> m_last_sound_heard{};
	struct cached_player_bones
	{
		std::uintptr_t bone_cache{};
		skeleton_reader::data bones{};
		std::chrono::steady_clock::time_point timestamp{};
	};
	std::unordered_map<std::uintptr_t, cached_player_bones> m_last_valid_bones{};
};

}
