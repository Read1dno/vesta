#pragma once

#include <windows.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <span>
#include <thread>

namespace platform::windows {

	enum class pointer_action : std::uint32_t
	{
		none = 0,
		primary_down = 1u << 0,
		primary_up = 1u << 1,
		secondary_down = 1u << 2,
		secondary_up = 1u << 3,
		relative_move = 1u << 4,
		middle_down = 1u << 5,
		middle_up = 1u << 6,
		auxiliary1_down = 1u << 7,
		auxiliary1_up = 1u << 8,
		auxiliary2_down = 1u << 9,
		auxiliary2_up = 1u << 10,
	};

	[[nodiscard]] constexpr pointer_action operator|(
		pointer_action left, pointer_action right ) noexcept
	{
		return static_cast<pointer_action>(
			static_cast<std::uint32_t>( left ) |
			static_cast<std::uint32_t>( right ) );
	}

	class input_gateway
	{
	public:
		input_gateway( ) = default;
		~input_gateway( );

		input_gateway( const input_gateway& ) = delete;
		input_gateway& operator=( const input_gateway& ) = delete;

		struct key_transition
		{
			std::uint16_t virtual_key{};
			bool pressed{};
		};

		[[nodiscard]] bool connect( ) noexcept;
		bool pointer( int dx, int dy,
			pointer_action actions ) const noexcept;
		[[nodiscard]] bool injected_primary_down( ) const noexcept
		{
			return m_injected_primary_down.load( std::memory_order_acquire );
		}
		bool key( std::uint16_t virtual_key,
			bool pressed ) const noexcept;
		bool keys( std::span<const key_transition> transitions ) const noexcept;

		void set_key_gate( std::uint16_t virtual_key, bool enabled ) noexcept;
		void set_movement_gate(
			std::span<const std::uint16_t> virtual_keys, bool enabled ) noexcept;
		[[nodiscard]] bool movement_gate_held(
			std::uint16_t virtual_key ) const noexcept
		{
			return virtual_key < m_movement_physical_down.size( )
				&& m_movement_physical_down[ virtual_key ].load(
					std::memory_order_acquire );
		}
		[[nodiscard]] bool physical_key_down(
			std::uint16_t virtual_key ) const noexcept;
		[[nodiscard]] bool key_gate_held( ) const noexcept
		{
			return m_gate_physical_down.load( std::memory_order_acquire );
		}
		[[nodiscard]] bool key_gate_ready( ) const noexcept
		{
			return m_gate_hook_ready.load( std::memory_order_acquire );
		}

	private:
		static LRESULT CALLBACK gate_proc( int code, WPARAM message, LPARAM parameter );
		void gate_thread_main( std::stop_token stop );
		void stop_key_gate( ) noexcept;

		static inline std::atomic<input_gateway*> s_gate_owner{};

		struct pointer_packet
		{
			POINT point{};
			unsigned long mouse_data{};
			unsigned long flags{};
			unsigned long timestamp{};
			std::uintptr_t extra_info{};
		};

		struct key_packet
		{
			std::uint16_t virtual_key{};
			std::uint16_t scan_code{};
			unsigned long flags{};
			unsigned long timestamp{};
			std::uintptr_t extra_info{};
		};

		using inject_pointer_fn = BOOL( NTAPI* )( pointer_packet*, int );
		using inject_key_fn = BOOL( NTAPI* )( key_packet*, int );

		inject_pointer_fn m_pointer_injector{};
		inject_key_fn m_key_injector{};
		mutable std::atomic<bool> m_injected_primary_down{};
		std::jthread m_gate_thread{};
		std::atomic<DWORD> m_gate_thread_id{};
		std::atomic<bool> m_gate_thread_ready{};
		std::atomic<bool> m_gate_hook_ready{};
		std::atomic<bool> m_gate_enabled{};
		std::atomic<bool> m_primary_gate_enabled{};
		std::atomic<std::uint16_t> m_gate_key{};
		std::atomic<bool> m_gate_physical_down{};
		std::array<std::atomic<bool>, 256> m_movement_gated{};
		std::array<std::atomic<bool>, 256> m_movement_physical_down{};
		std::atomic<std::uint16_t> m_movement_gate_count{};
		mutable std::array<std::atomic<bool>, 256> m_injected_down{};
		HHOOK m_gate_hook{};
	};

}
