#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <render/draw.hpp>

namespace scripting {

	enum class script_state : std::uint8_t
	{
		stopped,
		starting,
		running,
		error,
		over_budget
	};

	struct script_info
	{
		std::string id{};
		std::string name{};
		std::string version{};
		std::string author{};
		std::string description{};
		std::string error{};
		script_state state{ script_state::stopped };
		bool enabled{};
		bool autoload{};
		bool hot_reload{ true };
		std::size_t memory_bytes{};
		double last_callback_ms{};
	};

	enum class control_kind : std::uint8_t
	{
		text,
		button,
		toggle,
		slider,
		select,
		input,
		color,
		keybind,
		separator
	};

	using control_value = std::variant<bool, double, std::string, int, zdraw::rgba>;

	struct ui_control
	{
		std::string id{};
		std::string label{};
		control_kind kind{ control_kind::text };
		control_value value{ std::string{} };
		std::string action_text{};
		double minimum{};
		double maximum{ 1.0 };
		double step{ 0.01 };
		std::vector<std::string> options{};
		std::uint64_t revision{};
	};

	class runtime_t
	{
	public:
		struct implementation;

		runtime_t( );
		~runtime_t( );

		runtime_t( const runtime_t& ) = delete;
		runtime_t& operator=( const runtime_t& ) = delete;

		[[nodiscard]] bool initialize( );
		void shutdown( ) noexcept;
		void refresh( );

		void render( zdraw::draw_list& draw_list,
			std::uint32_t width, std::uint32_t height );

		[[nodiscard]] std::vector<script_info> scripts( ) const;
		[[nodiscard]] std::vector<ui_control> controls(
			std::string_view script_id ) const;
		void set_control( std::string_view script_id,
			std::string_view control_id, const control_value& value );
		void press_control( std::string_view script_id,
			std::string_view control_id );

		void set_enabled( std::string_view id, bool enabled );
		void set_autoload( std::string_view id, bool enabled );
		void set_hot_reload( std::string_view id, bool enabled );
		void reload( std::string_view id );
		[[nodiscard]] bool import_script( const std::filesystem::path& source );

		[[nodiscard]] std::filesystem::path root_path( ) const;
		[[nodiscard]] std::filesystem::path scripts_path( ) const;
		[[nodiscard]] static const char* state_name( script_state state ) noexcept;

	private:
		std::unique_ptr<implementation> m_impl{};
	};

	inline runtime_t& runtime( )
	{
		static runtime_t value{};
		return value;
	}

}
