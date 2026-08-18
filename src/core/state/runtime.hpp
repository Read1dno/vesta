#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <shared_mutex>

#include <core/math/vector.hpp>

namespace game {

struct presentation_camera_sample
{
	foundation::matrix4 matrix{};
	foundation::vec3 origin{};
	foundation::vec3 angles{};
	float fov{};
};

struct local_pawn_binding
{
	std::uintptr_t pawn{};
	std::uint32_t handle{};
	std::int32_t health{};
	std::int32_t team{};
	bool controlling_bot{};

	[[nodiscard]] explicit operator bool( ) const { return pawn != 0; }
};

[[nodiscard]] local_pawn_binding resolve_local_pawn( std::uintptr_t controller );

class local_state
{
public:
	void update( );

	[[nodiscard]] std::uintptr_t controller( ) const { return m_controller.load( ); }
	[[nodiscard]] std::uintptr_t pawn( ) const { return m_pawn.load( ); }
	[[nodiscard]] std::uint32_t pawn_handle( ) const { return m_pawn_handle.load( ); }
	[[nodiscard]] std::int32_t team( ) const { return m_team.load( ); }
	[[nodiscard]] bool valid( ) const { return m_pawn.load( ) != 0 || m_observer_pawn.load( ) != 0; }
	[[nodiscard]] bool alive( ) const { return m_alive.load( ); }
	[[nodiscard]] std::uintptr_t view_pawn( ) const { return m_alive.load( ) ? m_pawn.load( ) : m_observer_pawn.load( ); }

	[[nodiscard]] std::uintptr_t weapon( ) const { return m_weapon.load( ); }
	[[nodiscard]] std::uintptr_t weapon_vdata( ) const { return m_weapon_vdata.load( ); }
	[[nodiscard]] std::uint32_t weapon_type( ) const { return m_weapon_type.load( ); }
	[[nodiscard]] std::int32_t crosshair_id( ) const { return m_crosshair_id.load( ); }
	[[nodiscard]] std::uint32_t tick_base( ) const { return m_tick_base.load( ); }
	[[nodiscard]] std::int32_t health( ) const { return m_health.load( ); }
	[[nodiscard]] float game_time( ) const { return m_game_time.load( ); }
	[[nodiscard]] float flash_alpha( ) const { return m_flash_alpha.load( ); }

	[[nodiscard]] bool is_enemy( std::int32_t other_team ) const
	{
		return !m_team_mode.load( ) || m_view_team.load( ) != other_team;
	}

private:
	void reset( );
	void publish( std::uintptr_t controller, std::uintptr_t pawn,
		std::uint32_t pawn_handle,
		std::uintptr_t observer, std::int32_t team, std::int32_t view_team,
		std::int32_t crosshair, bool alive, bool team_mode,
		std::uintptr_t weapon, std::uintptr_t weapon_data,
		std::uint32_t weapon_type, std::uint32_t tick_base,
		std::int32_t health, float game_time, float flash_alpha );

	std::atomic<std::uintptr_t> m_controller{};
	std::atomic<std::uintptr_t> m_pawn{};
	std::atomic<std::uint32_t> m_pawn_handle{};
	std::atomic<std::uintptr_t> m_observer_pawn{};
	std::atomic<std::int32_t> m_team{};
	std::atomic<std::int32_t> m_view_team{};
	std::atomic<std::int32_t> m_crosshair_id{};
	std::atomic<bool> m_alive{};
	std::atomic<bool> m_team_mode{ true };
	std::atomic<std::uintptr_t> m_weapon{};
	std::atomic<std::uintptr_t> m_weapon_vdata{};
	std::atomic<std::uint32_t> m_weapon_type{};
	std::atomic<std::uint32_t> m_tick_base{};
	std::atomic<std::int32_t> m_health{};
	std::atomic<float> m_game_time{};
	std::atomic<float> m_flash_alpha{};
};

class camera_state
{
public:
	void update( );
	[[nodiscard]] bool sample_presentation(
		presentation_camera_sample& sample ) const;
	void begin_presentation_frame( const presentation_camera_sample& sample,
		std::uint32_t width, std::uint32_t height );
	void set_presentation_horizon( float seconds )
	{
		m_presentation_horizon_seconds.store( seconds, std::memory_order_relaxed );
	}

	[[nodiscard]] bool sample( foundation::vec3& origin, foundation::vec3& angles ) const;
	[[nodiscard]] foundation::vec2 project( const foundation::vec3& world_pos );
	[[nodiscard]] bool projection_valid( const foundation::vec2& screen_pos ) const
	{
		return screen_pos.x != k_invalid && screen_pos.y != k_invalid;
	}
	[[nodiscard]] bool has_camera( ) const { std::shared_lock lock( m_state_mutex ); return m_fov != k_invalid; }
	[[nodiscard]] foundation::vec3 origin( ) const { std::shared_lock lock( m_state_mutex ); return m_origin; }
	[[nodiscard]] foundation::vec3 angles( ) const { std::shared_lock lock( m_state_mutex ); return m_angles; }
	[[nodiscard]] float fov( ) const { std::shared_lock lock( m_state_mutex ); return m_fov; }
	[[nodiscard]] foundation::matrix4 matrix( ) const { std::shared_lock lock( m_state_mutex ); return m_matrix; }

private:
	struct matrix_sample
	{
		foundation::matrix4 matrix{};
		std::chrono::steady_clock::time_point time{};
	};

	static constexpr auto k_invalid{ 0xdead };

	foundation::matrix4 m_matrix{};
	foundation::vec3 m_origin{};
	foundation::vec3 m_angles{};
	float m_fov{};
	mutable std::shared_mutex m_state_mutex{};
	foundation::matrix4 m_frame_matrix{};
	float m_frame_width{};
	float m_frame_height{};
	std::array<matrix_sample, 16> m_matrix_history{};
	std::size_t m_matrix_history_next{};
	std::size_t m_matrix_history_count{};
	std::atomic<float> m_presentation_horizon_seconds{};
};

}
