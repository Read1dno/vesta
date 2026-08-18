#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace features::misc {

	enum class auto_stop_source : std::uint8_t
	{
		advanced_trigger,
		seed_trigger,
		count
	};

	class auto_stop_t
	{
	public:

		void request_stop( auto_stop_source source,
			std::chrono::steady_clock::time_point shot_time,
			float required_shoot_speed );
		void cancel_request( auto_stop_source source );
		void notify_shot( auto_stop_source source );
		[[nodiscard]] bool ready_to_fire( auto_stop_source source ) const;
		[[nodiscard]] bool active( )
		{
			std::scoped_lock lock( this->m_control_mutex );
			return this->m_active;
		}

		void tick( );
		void reset( );

	private:
		struct request
		{
			std::chrono::steady_clock::time_point shot_time{};
			float required_shoot_speed{};
			std::uint64_t revision{};
			bool active{};
			bool braking{};
		};

		[[nodiscard]] float estimate_stop_seconds(
			const foundation::vec3& velocity, float target_speed ) const;
		void engage( const foundation::vec3& velocity, float stop_speed );
		void release( );

		mutable std::mutex m_request_mutex{};
		std::array<request, static_cast<std::size_t>( auto_stop_source::count )>
			m_requests{};
		std::mutex m_control_mutex{};
		std::vector<std::uint16_t> m_synthetic_keys{};
		bool m_active{};
		bool m_enabled_last_tick{};
	};

	inline auto_stop_t& auto_stop( )
	{
		static auto_stop_t value{};
		return value;
	}

}
