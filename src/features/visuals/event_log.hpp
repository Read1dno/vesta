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

	enum class event_category : std::uint8_t
	{
		info,
		shot,
		hit,
		kill,
		miss,
		blocked
	};

	struct event_log_entry
	{
		std::uint64_t sequence{};
		std::string text{};
		event_kind kind{};
		event_category category{ event_category::info };
		std::chrono::steady_clock::time_point timestamp{};
		std::chrono::steady_clock::time_point deadline{};
		std::string source{};
		bool pending{};
		bool command_consumed{};
		bool seed{};
	};

	class event_log_t
	{
	public:
		void push( std::string text, event_kind kind = event_kind::info,
			event_category category = event_category::info );
		void push_throttled( std::uint32_t key, std::string_view text,
			event_kind kind, std::chrono::milliseconds interval,
			event_category category = event_category::blocked );
		std::uint64_t begin_trigger_shot( std::string source, bool seed );
		void mark_latest_trigger_consumed( );
		[[nodiscard]] bool resolve_latest_trigger_shot( int damage, bool killed );
		[[nodiscard]] std::vector<event_log_entry> snapshot(
			float lifetime_seconds, int maximum );

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
