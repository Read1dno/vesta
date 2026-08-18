#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::performance {

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
			render_frame,
		wait_frame_latency,
		chams,
		player_esp,
			world_visuals,
			bullet_feedback_capture,
		overlay_panels,
		menu,
		config_fingerprint,
		imgui_render,
		bloom_2d,
		present,
		count
	};

#if defined( VESTA_PERF_LOG ) && VESTA_PERF_LOG
	class scope
	{
	public:
		explicit scope( zone value ) noexcept;
		~scope( );
		scope( const scope& ) = delete;
		scope& operator=( const scope& ) = delete;

	private:
		zone m_zone{};
		zone m_previous{ zone::count };
		std::int64_t m_started{};
	};

	void record_rpm( std::size_t bytes, bool succeeded,
		std::int64_t started ) noexcept;
	[[nodiscard]] std::int64_t timestamp( ) noexcept;
	void flush_if_due( bool force = false ) noexcept;
#else
	class scope
	{
	public:
		explicit scope( zone ) noexcept {}
	};

	inline void record_rpm( std::size_t, bool, std::int64_t ) noexcept {}
	[[nodiscard]] inline std::int64_t timestamp( ) noexcept { return 0; }
	inline void flush_if_due( bool = false ) noexcept {}
#endif

}

#define VESTA_PERF_JOIN_INNER(a, b) a##b
#define VESTA_PERF_JOIN(a, b) VESTA_PERF_JOIN_INNER(a, b)
#define VESTA_PERF_SCOPE(name) \
	::platform::performance::scope VESTA_PERF_JOIN( vesta_perf_scope_, __LINE__ ){ \
		::platform::performance::zone::name }
