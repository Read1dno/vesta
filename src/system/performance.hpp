#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::performance
{

enum class zone : std::uint8_t
{
	game_loop,
	local_update,
	auto_accept,
	entity_refresh,
	world_update,
	bomb_update,
	radar_update,
	pose_sample,
	combat_loop,
	ballistics_tick,
	aim_tick,
	grenade_aim_tick,
	movement_loop,
	bhop_tick,
	auto_stop_tick,
	nade_helper_tick,
	seed_trigger_tick,
	seed_plan_build,
	seed_targets_read,
	seed_weapon_read,
	seed_penetration,
	render_frame,
	wait_frame_latency,
	chams,
	chams_world_effects,
	chams_geometry,
	chams_world_depth,
	bindings_refresh,
	player_esp,
	world_visuals,
	grenade_prediction_tick,
	bullet_feedback_tick,
	bullet_feedback_capture,
	overlay_panels,
	menu,
	config_fingerprint,
	imgui_render,
	bloom_2d,
	present,
    map_load,
    map_entities_refresh,
    map_entities_scan,
    map_entities_build,
    map_entities_publish,
	count
};

enum class counter : std::uint8_t
{
    map_entity_slots,
    map_entities_seen,
    map_solid_entities,
    map_entity_instances,
    map_scan_incomplete,
    map_entity_changes,
    map_geometry_published,
    count
};

#if defined(VESTA_PERF_LOG) && VESTA_PERF_LOG
class scope
{
  public:
	explicit scope(zone value) noexcept;
	~scope();
	scope(const scope &) = delete;
	scope &operator=(const scope &) = delete;

  private:
	zone m_zone{};
	zone m_previous{zone::count};
	std::int64_t m_started{};
	scope *m_parent{};
	std::uint64_t m_started_cycles{};
	std::uint64_t m_child_cycles{};
};

void record_rpm(std::size_t bytes, bool succeeded, std::int64_t started) noexcept;
[[nodiscard]] std::int64_t timestamp() noexcept;
void flush_if_due(bool force = false) noexcept;
void record_present() noexcept;
void record_counter(counter name, std::uint64_t amount = 1) noexcept;
#else
class scope
{
  public:
	explicit scope(zone) noexcept
	{
	}
};

inline void record_rpm(std::size_t, bool, std::int64_t) noexcept
{
}
[[nodiscard]] inline std::int64_t timestamp() noexcept
{
	return 0;
}
inline void flush_if_due(bool = false) noexcept
{
}
inline void record_present() noexcept
{
}
inline void record_counter(counter, std::uint64_t = 1) noexcept
{
}
#endif

} // namespace platform::performance

#define VESTA_PERF_JOIN_INNER(a, b) a##b
#define VESTA_PERF_JOIN(a, b) VESTA_PERF_JOIN_INNER(a, b)
#define VESTA_PERF_SCOPE(name)                                                                               \
	::platform::performance::scope VESTA_PERF_JOIN(vesta_perf_scope_, __LINE__)                              \
	{                                                                                                        \
		::platform::performance::zone::name                                                                  \
	}
