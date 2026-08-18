#pragma once

#include <simulation/ballistics.hpp>

namespace features::aimbot {
	using simulation::ballistics_t;

	enum class combat_block_reason : std::uint8_t
	{
		none,
		inactive,
		airborne,
		flashed,
		smoke,
		wall,
		invulnerable,
		weapon_not_ready,
		reloading,
		accuracy,
		hitchance,
		damage,
	};

	struct combat_frame
	{
		std::uint64_t sequence{};
		std::chrono::steady_clock::time_point timestamp{};
		foundation::matrix4 camera{};
		foundation::vec3 eye{};
		foundation::vec3 view_angles{};
		std::uintptr_t local_pawn{};
		float flash_alpha{};
		ballistics_t::context weapon{};
		std::shared_ptr<const std::vector<game::player_snapshot>> targets{};
	};

		class aimbot_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );
			void tick( );
			void reset( );
			void seed_tick( ballistics_t& seed_shared );
			void reset_seed( );
			[[nodiscard]] bool seed_hot_path_requested( ) const noexcept;
			[[nodiscard]] int seed_observed_tick( ) const noexcept
			{
				return m_seed_last_tick;
			}
			[[nodiscard]] std::chrono::steady_clock::time_point
				seed_tick_observed_at( ) const noexcept
			{
				return m_seed_tick_observed_at;
			}
			[[nodiscard]] bool seed_input_pending( ) const noexcept
			{
				return m_trigger_held;
			}
			void sync_seed_phase( int tick,
				std::chrono::steady_clock::time_point observed_at ) noexcept
			{
				if ( tick > 0 && tick != m_seed_phase_tick )
				{
					m_seed_phase_tick = tick;
					m_seed_phase_tick_at = observed_at;
				}
			}

		private:
			struct target
			{
				const game::player_snapshot* player{};
				game::skeleton_reader::data bones{};
				foundation::vec3 aim_point{};

				foundation::vec3 aim_offset{};
				int hitbox{ -1 };
				int bone{ -1 };
				int hitgroup{ -1 };
				foundation::vec3 point{};
				float damage{};
				float fov{};
				bool penetrated{};
			};

			[[nodiscard]] target choose_target( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, const config::combat_profile::resolved_config& cfg, bool enforce_fov = true ) const;
			[[nodiscard]] foundation::vec3 get_aim_point( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const game::player_snapshot& player, const game::skeleton_reader::data& bones, const config::combat_profile::resolved_config& cfg, float& out_damage, int& out_hitbox, int& out_bone, bool& out_penetrated, foundation::vec3& out_offset ) const;

			[[nodiscard]] float get_fov( const foundation::vec3& view_angles, const foundation::vec3& eye_pos, const foundation::vec3& target_pos ) const;
			[[nodiscard]] float screen_radius_for_fov( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, float fov_degrees ) const;

			void draw_penetration_crosshair( zdraw::draw_list& draw_list, const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::resolved_config& cfg, float current_time );
			void draw_fov_ring( zdraw::draw_list& draw_list, const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::aimbot& cfg );
			void aimbot( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const target& tgt, const config::combat_profile::aimbot& cfg );
			void recoil_control( const config::combat_profile::aimbot& cfg,
				bool defer_input );
			void flush_recoil_input( );

			struct trigger_result
			{
				const game::player_snapshot* player{};
				game::skeleton_reader::data bones{};
				int hitbox{ -1 };
				int hitgroup{ -1 };
				foundation::vec3 point{};
				float damage{};
				bool penetrated{};
			};

			[[nodiscard]] trigger_result trace_direction( const foundation::vec3& eye_pos, const foundation::vec3& direction, const std::vector<game::player_snapshot>& players, const config::combat_profile::triggerbot& cfg ) const;
			void triggerbot( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, const config::combat_profile::triggerbot& cfg );

			enum class seed_prediction_phase : std::uint8_t
			{
				current,
				ambiguous,
				next,
			};

			struct seed_shot_sample
			{
				int tick{};
				foundation::vec3 hash_angles{};
				foundation::vec3 direction_angles{};
			};

			struct seed_shot_plan
			{
				seed_prediction_phase phase{ seed_prediction_phase::current };
				seed_shot_sample current{};
				seed_shot_sample next{};

				std::uint64_t source_key{};
				int source_tick{ -1 };
				foundation::vec3 source_angles{};
				foundation::vec3 prepared_punch{};
			};

			struct seed_angle_history_entry
			{
				int tick{ -1 };
				foundation::vec3 hash_angles{};
			};

			struct seed_target_state
			{
				std::chrono::steady_clock::time_point acquired_at{};
				std::chrono::steady_clock::time_point last_seen_at{};
			};

			[[nodiscard]] std::optional<seed_shot_plan> build_seed_plan(
				std::uintptr_t pawn, const foundation::vec3& view_angles,
				bool host_session, int seed_tick,
				std::chrono::steady_clock::time_point now, bool memoize );
			[[nodiscard]] static bool possible_seed_match(
				const seed_shot_plan& plan, const std::pair<bool, bool>& matches );
			[[nodiscard]] static bool safe_seed_match(
				const seed_shot_plan& plan, const std::pair<bool, bool>& matches );

			motion::damped_value m_fov_alpha{};
			motion::scalar_transition m_pen_color_tween{};
			bool m_pen_color_can_pen{ false };
			foundation::source_random m_rng{};
			bool m_rng_seeded{ false };
			mutable std::mutex m_indicator_mutex{};
			foundation::vec3 m_indicator_point{};
			float m_indicator_fov{};
			std::chrono::steady_clock::time_point m_indicator_time{};
			std::chrono::steady_clock::time_point m_next_visual_scan{};
			std::uintptr_t m_indicator_pawn{};
			std::uintptr_t m_indicator_bone_cache{};
			int m_indicator_bone{ -1 };
			foundation::vec3 m_indicator_offset{};

			foundation::vec2 m_aim_error{};

			int m_aim_last_input_sequence{ -1 };
			foundation::vec3 m_aim_last_input_view{};
			std::chrono::steady_clock::time_point m_aim_last_input_time{};
			float m_aim_degrees_per_pixel{};
			float m_aim_degrees_candidate{};
			int m_aim_degrees_confirmations{};

			std::uintptr_t m_aim_pawn{};
			std::chrono::steady_clock::time_point m_aim_last_call{};
			std::chrono::steady_clock::time_point m_aim_last_seen{};
			std::chrono::steady_clock::time_point m_aim_reaction_until{};
			float m_aim_ramp{};
			float m_aim_initial_dist{};
			float m_aim_curve{};
			float m_aim_overshoot{ 1.0f };
			float m_aim_wander_phase{};
			float m_aim_wander_freq{};
			float m_aim_tracking_lag{};

			foundation::vec3 m_rcs_raw{};
			foundation::vec3 m_rcs_velocity{};
			float m_rcs_gain{ 1.0f };
			float m_rcs_response_scale{ 1.0f };
			float m_rcs_phase{};
			float m_rcs_freq{ 2.5f };
			foundation::vec3 m_rcs_applied{};
			foundation::vec2 m_rcs_mouse_error{};
			foundation::vec2 m_rcs_pending_mouse{};
			std::chrono::steady_clock::time_point m_rcs_last_call{};
			std::chrono::steady_clock::time_point m_rcs_last_input_time{};
			int m_rcs_last_input_sequence{ -1 };
			std::uintptr_t m_rcs_weapon{};
			int m_rcs_last_clip{ -1 };
			float m_rcs_last_shot_time{ -1.0f };
			int m_rcs_burst_shots{};
			bool m_rcs_active{ false };
			std::uint64_t m_combat_sequence{};

			foundation::vec3 m_aim_previous_relative_velocity{};
			foundation::vec3 m_aim_relative_acceleration{};
			bool m_aim_velocity_valid{};
			int m_aim_simulation_tick{ -1 };
			float m_aim_simulation_time{};
			int m_aim_velocity_samples{};

			bool m_trigger_held{ false };
			std::chrono::steady_clock::time_point m_trigger_release_time{};
			int m_trigger_press_shots{ -1 };
			bool m_trigger_shot_scheduled{ false };
			std::chrono::steady_clock::time_point m_trigger_fire_time{};
			std::chrono::steady_clock::time_point m_trigger_cooldown_until{};

			game::player_snapshot m_trigger_target{};
			game::skeleton_reader::data m_trigger_target_bones{};
			foundation::vec3 m_trigger_target_eye{};
			foundation::vec3 m_trigger_target_angles{};
			int m_trigger_target_parts{};
			bool m_trigger_target_valid{};
			bool m_seed_press_active{ false };
			bool m_seed_held_secondary{ false };
			bool m_seed_held_proxy{ false };
			bool m_revolver_pre_cock_down{ false };
			bool m_revolver_committed{ false };
			std::uintptr_t m_revolver_weapon{};
			int m_seed_last_shots{ -1 };
			int m_seed_pending_target_tick{ -1 };
			std::chrono::steady_clock::time_point m_seed_pending_time{};

			int m_seed_memo_sequence{ -1 };
			float m_seed_memo_pitch{};
			float m_seed_memo_yaw{};

			float m_seed_memo_next_pitch{};
			float m_seed_memo_next_yaw{};
			seed_prediction_phase m_seed_memo_phase{ seed_prediction_phase::current };
			std::array<seed_angle_history_entry, 3> m_seed_angle_history{};
			std::size_t m_seed_angle_history_count{};
			int m_seed_phase_tick{ -1 };
			std::chrono::steady_clock::time_point m_seed_phase_tick_at{};
			std::unordered_map<std::uintptr_t, seed_target_state> m_seed_targets{};

			std::vector<game::player_snapshot> m_seed_player_buffer{};
			std::uintptr_t m_seed_last_controller{};
			std::uintptr_t m_seed_last_pawn{};
			int m_seed_last_tick{ -1 };
			std::chrono::steady_clock::time_point m_seed_tick_observed_at{};

			float m_last_time{ 0.0f };
		};

		class grenade_aim_t
		{
		public:
			void tick( );

		private:
			enum class grenade_kind : std::uint8_t
			{
				unknown,
				he,
				flash,
				smoke,
				molotov,
				decoy,
			};

			struct trajectory_result
			{
				foundation::vec3 end_pos{};
				float duration{};
				float closest_head_distance{ std::numeric_limits<float>::max( ) };
				int bounces{};
				bool valid{};
			};

			struct target_info
			{
				game::player_snapshot player{};
				std::uintptr_t pawn{};
				foundation::vec3 predicted_pos{};
				foundation::vec3 head_pos{};
				foundation::vec3 velocity{};
				foundation::vec3 optimal_angles{};
				float score{ std::numeric_limits<float>::max( ) };
				bool found_trajectory{ false };
			};

			void find_target( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, float max_fov );
			void calculate_trajectory( const foundation::vec3& eye_pos, float throw_vel_vdata, float throw_strength, grenade_kind kind );
			void smooth_aim( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::global_settings::grenade_aim_config& cfg );
			[[nodiscard]] grenade_kind resolve_grenade_kind( std::uintptr_t weapon_vdata ) const;
			[[nodiscard]] trajectory_result simulate_fast( const foundation::vec3& start, const foundation::vec3& angles,
				const foundation::vec3& player_vel, float throw_vel_vdata, float throw_strength,
				float sv_gravity, float molotov_floor_normal, grenade_kind kind,
				const foundation::vec3& target_head ) const;

			bool m_is_active{ false };
			target_info m_current_target{};
			foundation::vec2 m_aim_error{};
			std::chrono::steady_clock::time_point m_last_calculation{};
			grenade_kind m_last_kind{ grenade_kind::unknown };
			std::uintptr_t m_last_weapon{};
		};

	inline grenade_aim_t& grenade_aim( ) { static grenade_aim_t value{}; return value; }
	inline aimbot_t& aim( ) { static aimbot_t value{}; return value; }

}
