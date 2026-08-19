#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace platform::windows {

	[[nodiscard]] inline bool binding_active( const int mode, const int key,
		const int always_mode, const int hold_mode, const int toggle_mode ) noexcept
	{
		if ( mode == always_mode ) return true;
		if ( key <= 0 || key >= 256 ) return false;
		const auto down = ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0;
		if ( mode == hold_mode ) return down;
		if ( mode != toggle_mode ) return false;

		static std::array<std::atomic<bool>, 256> previous{};
		static std::array<std::atomic<bool>, 256> toggled{};
		const auto was_down = previous[ key ].exchange(
			down, std::memory_order_acq_rel );
		if ( down && !was_down )
		{
			auto current = toggled[ key ].load( std::memory_order_acquire );
			while ( !toggled[ key ].compare_exchange_weak( current, !current,
				std::memory_order_acq_rel, std::memory_order_acquire ) ) {}
		}
		return toggled[ key ].load( std::memory_order_acquire );
	}

}
