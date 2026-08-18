#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

enum ImGuiKey : int;

namespace overlay_input
{
	[[nodiscard]] ImGuiKey key_from_virtual_key( int key );
}

class input_router
{
public:
	input_router( ) = default;
	~input_router( );

	input_router( const input_router& ) = delete;
	input_router& operator=( const input_router& ) = delete;

	[[nodiscard]] bool initialize( );
	void shutdown( );
	void set_capture( bool enabled, HWND foreground );
	void pump_imgui( int origin_x, int origin_y, bool pump_pointer = true );
	[[nodiscard]] bool capture_ready( ) const noexcept
	{
		return m_hooks_ready.load( std::memory_order_acquire );
	}

private:
	static LRESULT CALLBACK keyboard_proc( int code, WPARAM message, LPARAM parameter );
	static LRESULT CALLBACK mouse_proc( int code, WPARAM message, LPARAM parameter );
	void thread_main( std::stop_token stop );
	[[nodiscard]] static bool passthrough_key( std::uint32_t key );

	static inline std::atomic<input_router*> s_active{};

	std::jthread m_thread{};
	std::atomic<DWORD> m_thread_id{};
	std::atomic<bool> m_ready{};
	std::atomic<bool> m_hooks_ready{};
	std::atomic<bool> m_capture{};
	std::atomic<HWND> m_capture_foreground{};
	std::array<std::atomic<bool>, 256> m_keys{};
	std::array<std::atomic<bool>, 256> m_key_pressed{};
	std::array<bool, 256> m_reported_keys{};
	std::array<std::atomic<bool>, 5> m_mouse_buttons{};
	std::array<std::atomic<bool>, 5> m_mouse_pressed{};
	std::array<bool, 5> m_reported_mouse_buttons{};
	std::atomic<LONG> m_wheel{};
	std::atomic<LONG> m_wheel_horizontal{};
	HHOOK m_keyboard_hook{};
	HHOOK m_mouse_hook{};
};
