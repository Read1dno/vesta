#pragma once

#include <core/input/bindings.hpp>

namespace features::misc {

		class nade_helper_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );
			void tick( );

		private:
			enum class throw_phase : std::uint8_t
			{
				idle,
				crouching,
				priming,
				running,
				jumping,
				complete,
			};

			struct owned_control
			{
				game::input_binding binding{};
				bool pressed{};
			};

			struct lineup_view
			{
				const char* name{};
				const char* action{};
				foundation::vec3 position{};
				float pitch{};
				float yaw{};
				std::uint16_t actions{};
				std::uint16_t run_ticks{};
				std::uint8_t after_jump_ticks{};
				float throw_strength{};
				bool manual{};
				float distance{};
			};

			[[nodiscard]] bool collect( const foundation::vec3& player_pos, std::vector<lineup_view>& out ) const;
			[[nodiscard]] static std::uint8_t resolve_kind( std::uintptr_t weapon_vdata );

			[[nodiscard]] int select_armed( const std::vector<lineup_view>& lineups, const foundation::vec3& view_angles ) const;
			[[nodiscard]] bool execution_position_ready( const lineup_view& lineup,
				const foundation::vec3& player_pos ) const;
			void reset_lock( );

			void aim_at( const lineup_view& lineup, const foundation::vec3& view_angles, float& out_error );
			[[nodiscard]] bool begin_throw( const lineup_view& lineup,
				std::uintptr_t pawn, std::uintptr_t weapon, std::uint32_t tick,
				std::chrono::steady_clock::time_point now );
			[[nodiscard]] bool prime_throw( std::uint32_t tick,
				std::chrono::steady_clock::time_point now );
			void drive_throw( std::uintptr_t pawn, std::uintptr_t weapon,
				std::uint32_t tick, std::chrono::steady_clock::time_point now );
			void finish_throw( std::uint32_t tick );
			[[nodiscard]] bool set_control( owned_control& control, bool pressed );
			void release_movement( bool include_jump = true );
			void release_attacks( );
			void cancel_throw( bool latch );

			[[nodiscard]] static std::string throw_instruction( const lineup_view& lineup );

			void draw_text_plaque( zdraw::draw_list& draw_list, float center_x, float top_y,
				std::string_view title, std::string_view subtitle, const zdraw::rgba& accent, float alpha ) const;

			void draw_plaque( zdraw::draw_list& draw_list, const lineup_view& lineup, const foundation::vec2& screen ) const;
			void draw_stand_marker( zdraw::draw_list& draw_list, const lineup_view& lineup, bool standing ) const;
			void draw_aim_guidance( zdraw::draw_list& draw_list, const lineup_view& lineup,
				const foundation::vec3& eye_pos, bool selected, bool converged ) const;

			foundation::vec2 m_aim_error{};
			std::vector<std::uint16_t> m_gated_keys{};
			std::vector<lineup_view> m_render_scratch{};
			std::vector<lineup_view> m_tick_scratch{};
			lineup_view m_active_lineup{};
			throw_phase m_throw_phase{ throw_phase::idle };
			owned_control m_forward{};
			owned_control m_walk{};
			owned_control m_duck{};
			owned_control m_jump{};
			owned_control m_attack{};
			owned_control m_attack2{};
			std::uintptr_t m_active_pawn{};
			std::uintptr_t m_active_weapon{};
			std::uint32_t m_phase_tick{};
			std::uint32_t m_run_start_tick{};
			std::uint32_t m_jump_tick{};
			std::chrono::steady_clock::time_point m_phase_started{};
			std::chrono::steady_clock::time_point m_lock_started{};
			std::chrono::steady_clock::time_point m_last_aim_update{};
			const char* m_lock_name{};
			foundation::vec3 m_lock_position{};
			float m_lock_pitch{};
			float m_lock_yaw{};
			bool m_activation_latched{};

			static constexpr float k_aim_marker_distance{ 220.0f };
		};

		class auto_accept_t
		{
		public:
			void tick( );
		private:
			enum class click_phase : std::uint8_t
			{
				idle,
				moved,
				pressed,
			};

			std::uintptr_t m_signal_identity{};
			std::uintptr_t m_candidate_panel{};
			std::uintptr_t m_clicked_panel{};
			std::uintptr_t m_scan_best_panel{};
			std::uint32_t m_scan_best_generation{};
			std::uint32_t m_panorama_cursor{};
			std::vector<std::uintptr_t> m_panorama_slots{};
			click_phase m_click_phase{ click_phase::idle };
			std::uintptr_t m_click_window{};
			std::uintptr_t m_click_panel{};
			std::int32_t m_saved_cursor_x{};
			std::int32_t m_saved_cursor_y{};
			std::int32_t m_click_x{};
			std::int32_t m_click_y{};
			bool m_saved_cursor_valid{};
			bool m_clicked_panel_hidden{};
		};

		class bhop_t
		{
		public:
			void tick( );

		private:
			[[nodiscard]] bool will_leave_ground( std::uintptr_t pawn,
				const foundation::vec3& origin, const foundation::vec3& velocity,
				bool require_current_support ) const;

			std::uint16_t m_jump_key{};
			std::uint16_t m_activation_key{};
			std::chrono::steady_clock::time_point m_next_binding_refresh{};
			bool m_jump_down{ false };
			std::uint16_t m_owned_jump_key{};
			bool m_gate_requested{};
			bool m_edge_was_on_ground{ false };
			bool m_edge_armed{ false };
			float m_last_bunny_simulation_time{ -1.0f };
			std::chrono::steady_clock::time_point m_last_bunny_tap{};

			std::chrono::steady_clock::time_point m_last_transition{};
		};

		inline nade_helper_t& nade_helper( ) { static nade_helper_t value{}; return value; }
		inline auto_accept_t& auto_accept( ) { static auto_accept_t value{}; return value; }
		inline bhop_t& bhop( ) { static bhop_t value{}; return value; }

}
