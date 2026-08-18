#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

class window_tracker
{
public:
	struct update
	{
		RECT client{};
		std::uint64_t transition_revision{};
		bool geometry_changed{};
		bool visibility_changed{};
		bool z_order_changed{};
		bool transition_changed{};
		bool visible{};
	};

	window_tracker( ) = default;
	~window_tracker( );

	window_tracker( const window_tracker& ) = delete;
	window_tracker& operator=( const window_tracker& ) = delete;

	[[nodiscard]] bool initialize( std::uint32_t process_id );
	void bind_overlay( HWND overlay );
	void notify_display_change( );
	[[nodiscard]] bool poll( update& result );
	void place_overlay( ) const;
	void shutdown( );

	[[nodiscard]] HWND target( ) const { return m_target; }
	[[nodiscard]] const RECT& client( ) const { return m_client; }
	[[nodiscard]] std::uint32_t width( ) const;
	[[nodiscard]] std::uint32_t height( ) const;
	[[nodiscard]] std::uint32_t refresh_rate( ) const { return m_refresh_rate; }
	[[nodiscard]] bool visible( ) const { return m_visible; }
	[[nodiscard]] bool covers_monitor( ) const;
	[[nodiscard]] std::uint64_t transition_revision( ) const
	{
		return m_transition_revision.load( std::memory_order_acquire );
	}
	[[nodiscard]] DWORD failure_code( ) const { return m_failure_code; }
	[[nodiscard]] std::uint32_t failure_stage( ) const { return m_failure_stage; }

private:
	static void CALLBACK event_callback(
		HWINEVENTHOOK hook,
		DWORD event,
		HWND window,
		LONG object_id,
		LONG child_id,
		DWORD event_thread,
		DWORD event_time );

	[[nodiscard]] HWND find_target( ) const;
	[[nodiscard]] bool refresh_geometry( bool& changed );
	[[nodiscard]] bool calculate_visibility( ) const;
	void install_hooks( );
	void remove_hooks( );

	static inline std::atomic<window_tracker*> s_active{};

	std::uint32_t m_process_id{};
	DWORD m_failure_code{};
	std::uint32_t m_failure_stage{};
	HWND m_target{};
	HWND m_dxgi_proxy{};
	HWND m_overlay{};
	RECT m_client{};
	std::uint32_t m_refresh_rate{ 60 };
	std::atomic<bool> m_geometry_pending{ true };
	std::atomic<bool> m_visibility_pending{ true };
	std::atomic<bool> m_z_order_pending{ true };
	std::atomic<bool> m_reacquire_pending{};
	std::atomic<bool> m_force_hidden_pending{};
	std::atomic<std::uint64_t> m_transition_revision{ 1 };
	std::uint64_t m_observed_transition_revision{};
	ULONGLONG m_next_reacquire_tick{};
	ULONGLONG m_next_geometry_refresh_tick{};
	ULONGLONG m_next_visibility_refresh_tick{};
	ULONGLONG m_next_z_order_repair_tick{};
	bool m_visible{};
	HWINEVENTHOOK m_foreground_hook{};
	HWINEVENTHOOK m_location_hook{};
	HWINEVENTHOOK m_lifetime_hook{};
	HWINEVENTHOOK m_minimize_hook{};
	HWINEVENTHOOK m_cloak_hook{};
};
