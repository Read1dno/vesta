#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <core/state/entities.hpp>
#include <core/state/runtime.hpp>

namespace game {

struct sampled_player_pose
{
	std::size_t source_index{};
	std::uintptr_t pawn{};
	std::uintptr_t bone_cache{};
	std::string_view model_path{};
	skeleton_reader::data bones{};
	bool reused_after_read_failure{};
};

struct player_pose_frame
{
	std::uint64_t sequence{};
	std::chrono::steady_clock::time_point timestamp{};
	std::shared_ptr<const std::vector<player_snapshot>> world{};
	presentation_camera_sample camera{};
	std::vector<sampled_player_pose> players{};
};

class player_pose_sampler
{
public:
	void run( );
	void set_presentation_state( bool active, std::uint32_t display_refresh );
	[[nodiscard]] std::shared_ptr<const player_pose_frame> latest( ) const;

private:
	struct cached_pose
	{
		std::uintptr_t bone_cache{};
		std::string model_path{};
		skeleton_reader::data bones{};
		std::chrono::steady_clock::time_point timestamp{};
	};

	void sample_once( );

	std::atomic<bool> m_active{};
	std::atomic<std::uint32_t> m_rate{ 144 };
	std::atomic<std::uint64_t> m_sequence{};
	std::atomic<void*> m_wake_event{};
	std::atomic<std::shared_ptr<const player_pose_frame>> m_latest{};
	std::unordered_map<std::uintptr_t, cached_pose> m_last_valid{};
};

}
