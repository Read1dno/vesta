#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace features::visuals {

	enum class event_kind : std::uint8_t
	{
		info,
		hit,
		kill,
		blocked
	};

	struct event_log_entry
	{
		std::uint64_t sequence{};
		std::string text{};
		event_kind kind{};
		std::chrono::steady_clock::time_point timestamp{};
	};

	class event_log_t
	{
	public:
		void push( std::string text, event_kind kind = event_kind::info );
		void push_throttled( std::uint32_t key, std::string_view text,
			event_kind kind, std::chrono::milliseconds interval );
		[[nodiscard]] std::vector<event_log_entry> snapshot(
			float lifetime_seconds, int maximum ) const;

	private:
		mutable std::mutex m_mutex{};
		std::deque<event_log_entry> m_entries{};
		std::vector<std::pair<std::uint32_t,
			std::chrono::steady_clock::time_point>> m_throttles{};
		std::uint64_t m_sequence{};
	};

	inline event_log_t& event_log( )
	{
		static event_log_t value{};
		return value;
	}

}
