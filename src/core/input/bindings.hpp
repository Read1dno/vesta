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

	class configured_input_bindings
	{
	public:
		[[nodiscard]] input_binding resolve(input_action action);
		[[nodiscard]] std::vector<input_binding> candidates(input_action action);
		[[nodiscard]] bool text_entry_active();

	private:
		std::mutex m_mutex{};
		std::uintptr_t m_hud_global{};
		std::uintptr_t m_hud_chat{};
		std::uintptr_t m_hud_chat_vtable{};
		std::ptrdiff_t m_chat_active_offset{ -1 };
		std::uint32_t m_chat_process_id{};
		bool m_chat_active{};
		std::chrono::steady_clock::time_point m_next_chat_lookup{};
		std::chrono::steady_clock::time_point m_next_chat_sample{};
	};

	inline configured_input_bindings& input_bindings( )
	{
		static configured_input_bindings value{};
		return value;
	}

} // namespace game
