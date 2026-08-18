#include <stdafx.hpp>
#include <system/performance.hpp>

#if defined( VESTA_PERF_LOG ) && VESTA_PERF_LOG

#include <bit>
#include <filesystem>
#include <fstream>

namespace platform::performance {
	namespace {
		constexpr std::array names{
			"game_loop", "local_update", "auto_accept", "entity_refresh",
			"world_update", "bomb_update", "radar_update", "pose_sample",
			"combat_loop", "ballistics_tick", "aim_tick", "grenade_aim_tick",
			"movement_loop", "bhop_tick", "auto_stop_tick", "nade_helper_tick",
			"seed_trigger_tick", "render_frame",
			"wait_frame_latency", "chams", "player_esp", "world_visuals",
			"bullet_feedback_capture", "overlay_panels", "menu",
			"config_fingerprint", "imgui_render",
			"bloom_2d", "present"
		};
		static_assert( names.size( ) == static_cast<std::size_t>( zone::count ) );

		struct aggregate
		{
			std::atomic<std::uint64_t> calls{};
			std::atomic<std::uint64_t> total_ticks{};
			std::atomic<std::uint64_t> maximum_ticks{};
			std::array<std::atomic<std::uint64_t>, 32> histogram{};
		};
		struct rpm_aggregate
		{
			std::atomic<std::uint64_t> calls{};
			std::atomic<std::uint64_t> bytes{};
			std::atomic<std::uint64_t> failures{};
			std::atomic<std::uint64_t> ticks{};
			std::atomic<std::uint64_t> maximum{};
		};

		std::array<aggregate, static_cast<std::size_t>( zone::count )> zones{};
		std::array<rpm_aggregate, static_cast<std::size_t>( zone::count )>
			rpm_by_zone{};
		thread_local zone active_zone{ zone::count };
		std::atomic<std::uint64_t> rpm_calls{};
		std::atomic<std::uint64_t> rpm_bytes{};
		std::atomic<std::uint64_t> rpm_failures{};
		std::atomic<std::uint64_t> rpm_ticks{};
		std::atomic<std::uint64_t> rpm_maximum{};
		std::atomic<std::int64_t> next_flush{};

		[[nodiscard]] std::int64_t frequency( ) noexcept
		{
			static const auto value = [ ]
			{
				LARGE_INTEGER result{};
				::QueryPerformanceFrequency( &result );
				return std::max<std::int64_t>( result.QuadPart, 1 );
			}( );
			return value;
		}

		void update_maximum( std::atomic<std::uint64_t>& target,
			const std::uint64_t value ) noexcept
		{
			auto current = target.load( std::memory_order_relaxed );
			while ( current < value && !target.compare_exchange_weak(
				current, value, std::memory_order_relaxed ) ) {}
		}

		void record( const zone value, const std::uint64_t ticks ) noexcept
		{
			auto& out = zones[ static_cast<std::size_t>( value ) ];
			out.calls.fetch_add( 1, std::memory_order_relaxed );
			out.total_ticks.fetch_add( ticks, std::memory_order_relaxed );
			update_maximum( out.maximum_ticks, ticks );
			const auto bucket = std::min<std::size_t>(
				31, std::bit_width( ticks | 1ull ) - 1 );
			out.histogram[ bucket ].fetch_add( 1, std::memory_order_relaxed );
		}

		[[nodiscard]] std::uint64_t to_ns( const std::uint64_t ticks ) noexcept
		{
			return static_cast<std::uint64_t>(
				static_cast<long double>( ticks ) * 1'000'000'000.0L
				/ static_cast<long double>( frequency( ) ) );
		}

		[[nodiscard]] std::filesystem::path output_path( )
		{
			wchar_t temporary[ MAX_PATH ]{};
			if ( !::GetTempPathW( static_cast<DWORD>( std::size( temporary ) ),
				temporary ) ) return {};
			std::error_code error{};
			auto directory = std::filesystem::path( temporary ) / L"vesta";
			std::filesystem::create_directories( directory, error );
			return error ? std::filesystem::path{}
				: directory / L"performance.jsonl";
		}
	}

	std::int64_t timestamp( ) noexcept
	{
		LARGE_INTEGER value{};
		::QueryPerformanceCounter( &value );
		return value.QuadPart;
	}

	scope::scope( const zone value ) noexcept
		: m_zone{ value }, m_previous{ active_zone }, m_started{ timestamp( ) }
	{
		active_zone = value;
	}

	scope::~scope( )
	{
		const auto elapsed = timestamp( ) - m_started;
		active_zone = m_previous;
		if ( elapsed > 0 ) record( m_zone, static_cast<std::uint64_t>( elapsed ) );
	}

	void record_rpm( const std::size_t bytes, const bool succeeded,
		const std::int64_t started ) noexcept
	{
		const auto elapsed = timestamp( ) - started;
		rpm_calls.fetch_add( 1, std::memory_order_relaxed );
		rpm_bytes.fetch_add( bytes, std::memory_order_relaxed );
		if ( !succeeded ) rpm_failures.fetch_add( 1, std::memory_order_relaxed );
		if ( elapsed > 0 )
		{
			const auto ticks = static_cast<std::uint64_t>( elapsed );
			rpm_ticks.fetch_add( ticks, std::memory_order_relaxed );
			update_maximum( rpm_maximum, ticks );
		}

		if ( active_zone != zone::count )
		{
			auto& attributed = rpm_by_zone[
				static_cast<std::size_t>( active_zone ) ];
			attributed.calls.fetch_add( 1, std::memory_order_relaxed );
			attributed.bytes.fetch_add( bytes, std::memory_order_relaxed );
			if ( !succeeded )
				attributed.failures.fetch_add( 1, std::memory_order_relaxed );
			if ( elapsed > 0 )
			{
				const auto ticks = static_cast<std::uint64_t>( elapsed );
				attributed.ticks.fetch_add( ticks, std::memory_order_relaxed );
				update_maximum( attributed.maximum, ticks );
			}
		}
	}

	void flush_if_due( const bool force ) noexcept
	{
		const auto now = timestamp( );
		auto expected = next_flush.load( std::memory_order_relaxed );
		if ( !force && expected != 0 && now < expected ) return;
		const auto next = now + frequency( );
		if ( !next_flush.compare_exchange_strong(
			expected, next, std::memory_order_acq_rel ) && !force ) return;

		try
		{
			const auto path = output_path( );
			if ( path.empty( ) ) return;
			std::ofstream stream( path, std::ios::app );
			if ( !stream ) return;
			const auto uptime_ms = ::GetTickCount64( );
			for ( std::size_t index{}; index < zones.size( ); ++index )
			{
				auto& source = zones[ index ];
				const auto calls = source.calls.exchange( 0, std::memory_order_acq_rel );
				const auto total = source.total_ticks.exchange( 0, std::memory_order_acq_rel );
				const auto maximum = source.maximum_ticks.exchange( 0, std::memory_order_acq_rel );
				if ( !calls ) continue;
				std::uint64_t cumulative{};
				std::uint64_t p95_ticks{};
				const auto threshold = ( calls * 95 + 99 ) / 100;
				for ( std::size_t bucket{}; bucket < source.histogram.size( ); ++bucket )
				{
					cumulative += source.histogram[ bucket ].exchange(
						0, std::memory_order_acq_rel );
					if ( !p95_ticks && cumulative >= threshold )
						p95_ticks = 1ull << bucket;
				}
				stream << "{\"type\":\"zone\",\"t_ms\":" << uptime_ms
					<< ",\"name\":\"" << names[ index ] << "\",\"calls\":" << calls
					<< ",\"total_ns\":" << to_ns( total )
					<< ",\"avg_ns\":" << to_ns( total ) / calls
					<< ",\"p95_ns\":" << to_ns( p95_ticks )
					<< ",\"max_ns\":" << to_ns( maximum ) << "}\n";
			}

			const auto calls = rpm_calls.exchange( 0, std::memory_order_acq_rel );
			if ( calls )
			{
				const auto bytes = rpm_bytes.exchange( 0, std::memory_order_acq_rel );
				const auto failures = rpm_failures.exchange( 0, std::memory_order_acq_rel );
				const auto total = rpm_ticks.exchange( 0, std::memory_order_acq_rel );
				const auto maximum = rpm_maximum.exchange( 0, std::memory_order_acq_rel );
				stream << "{\"type\":\"rpm\",\"t_ms\":" << uptime_ms
					<< ",\"calls\":" << calls << ",\"bytes\":" << bytes
					<< ",\"failures\":" << failures
					<< ",\"total_ns\":" << to_ns( total )
					<< ",\"max_ns\":" << to_ns( maximum ) << "}\n";
			}

			for ( std::size_t index{}; index < rpm_by_zone.size( ); ++index )
			{
				auto& source = rpm_by_zone[ index ];
				const auto zone_calls = source.calls.exchange(
					0, std::memory_order_acq_rel );
				if ( !zone_calls ) continue;
				const auto zone_bytes = source.bytes.exchange(
					0, std::memory_order_acq_rel );
				const auto zone_failures = source.failures.exchange(
					0, std::memory_order_acq_rel );
				const auto zone_ticks = source.ticks.exchange(
					0, std::memory_order_acq_rel );
				const auto zone_maximum = source.maximum.exchange(
					0, std::memory_order_acq_rel );
				stream << "{\"type\":\"rpm_zone\",\"t_ms\":" << uptime_ms
					<< ",\"name\":\"" << names[ index ]
					<< "\",\"calls\":" << zone_calls
					<< ",\"bytes\":" << zone_bytes
					<< ",\"failures\":" << zone_failures
					<< ",\"total_ns\":" << to_ns( zone_ticks )
					<< ",\"avg_ns\":" << to_ns( zone_ticks ) / zone_calls
					<< ",\"max_ns\":" << to_ns( zone_maximum ) << "}\n";
			}
		}
		catch ( ... ) {}
	}

}

#endif
