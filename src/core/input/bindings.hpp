#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace game {

	enum class input_action : std::uint8_t
	{
		forward,
		back,
		left,
		right,
		walk,
		duck,
		jump,
		attack,
		attack2,
		count,
	};

	enum class input_device : std::uint8_t
	{
		none,
		keyboard,
		mouse_primary,
		mouse_secondary,
		mouse_middle,
		mouse_auxiliary1,
		mouse_auxiliary2,
	};

	struct input_binding
	{
		input_device device{ input_device::none };
		std::uint16_t virtual_key{};
		std::string name{};

		[[nodiscard]] explicit operator bool( ) const noexcept
		{
			return device != input_device::none;
		}
	};

	class live_input_bindings
	{
	public:
		[[nodiscard]] input_binding resolve( input_action action,
			std::uint16_t preferred_virtual_key = 0 );
		[[nodiscard]] std::vector<input_binding> candidates( input_action action );

	private:
		void refresh_locked( std::chrono::steady_clock::time_point now );

		std::mutex m_mutex{};
		std::array<std::vector<input_binding>,
			static_cast<std::size_t>( input_action::count )> m_bindings{};
		std::uintptr_t m_input_service{};
		std::uintptr_t m_binding_table{};
		std::uintptr_t m_key_name_table{};
		std::chrono::steady_clock::time_point m_next_refresh{};
	};

	inline live_input_bindings& input_bindings( )
	{
		static live_input_bindings value{};
		return value;
	}

}
