#pragma once

#include <array>
#include <cstdint>
#include <windows.h>

#include <render/draw.hpp>

namespace config {

	struct combat_profile
	{
		struct activation
		{
			enum mode : int { hold = 0, always = 1 };
		};
		[[nodiscard]] static bool activation_active(
			int mode, int key ) noexcept
		{
			return mode == activation::always
				|| ( key > 0 && ( ::GetAsyncKeyState( key ) & 0x8000 ) != 0 );
		}

		struct wall_policy
		{
			enum mode : int { block = 0, penetration = 1, ignore = 2 };
		};

		struct legit_checks
		{
			bool airborne{};
			bool flashed{};
			bool smoke{};

			float flash_threshold{ 85.0f };
			int walls{ wall_policy::penetration };
		};

		struct humanizer_settings
		{
			float gravity{ 9.0f };
			float wind{ 3.0f };
			float max_step{ 15.0f };
			float damping{ 0.82f };
			int reaction_min_ms{ 60 };
			int reaction_max_ms{ 145 };
			float curve{ 0.30f };
			float overshoot_chance{ 17.5f };
			float overshoot_amount{ 0.14f };
			float jitter{ 0.30f };
			float deadzone{ 0.075f };
		};

		struct multipoint_settings
		{
			bool caps{ true };
			bool sides{ true };
			float head_scale{ 0.50f };
			float body_scale{ 0.50f };
			float limb_scale{ 0.50f };
		};

		struct prediction_settings
		{
			bool enabled{ true };
			float max_horizon_ms{ 120.0f };
			bool acceleration{ true };
		};

		struct rcs_settings
		{
			bool enabled{ true };
			int start_bullet{ 2 };
			float pitch{ 100.0f };
			float yaw{ 100.0f };
			float response_ms{ 24.0f };
			float randomness{ 2.0f };
			float drift{ 1.5f };
		};

		struct fov_settings
		{
			enum mode : int { fixed = 0, distance = 1, target_distance = 2 };
			enum indicator : int { center = 0, target = 1 };
			int selection{ fixed };

			int visualization{ center };

			float near_distance_m{ 1.5f };
			float near_fov{ 18.0f };
			float far_distance_m{ 45.0f };
			float far_fov{ 2.5f };
			float distance_curve{ 1.0f };
		};

		enum aim_part : int
		{
			head = 1 << 0,
			body = 1 << 1,
			arms = 1 << 2,
			legs = 1 << 3,
			all  = head | body | arms | legs,
		};

		struct seed_mode
		{
			enum : int
			{
				none = 0,
				restricted = 1,
				unrestricted = 2,
			};
		};

		struct aimbot
		{
			bool enabled{ false };
			int key{ VK_XBUTTON2 };
			int activation_mode{ activation::hold };
			int fov{ 5 };
			int smoothing{ 5 };
			int humanize{ 50 };
			bool autowall{ true };
			float min_damage{ 90.0f };
			bool lethal_only{};
			int hitbox_parts{ aim_part::head };
			bool multipoint{ true };
			bool visible_only{ true };
			bool draw_fov{ false };
			zdraw::rgba fov_color{ 225, 225, 225, 125 };
			bool predictive{ true };
			bool recoil_sync{ true };
			legit_checks checks{};
			humanizer_settings humanizer{};
			multipoint_settings multipoint_config{};
			prediction_settings prediction{};
			rcs_settings rcs{};
			fov_settings fov_config{};
		};

		struct triggerbot
		{
			bool enabled{ true };
			int key{ 0x43 };
			int activation_mode{ activation::hold };
			int seed_type{ seed_mode::none };
			int hitbox_parts{ aim_part::head };
			float hitchance{ 75.0f };
			int delay{ 10 };
			int randomize_ms{ 5 };
			float outlier_chance{ 3.0f };
			int outlier_delay_ms{ 20 };
			int delay_after_ms{ 100 };
			bool lethal_only{};

			int reaction_time{ 0 };
			bool autowall{ true };
			float min_damage{ 90.0f };
			bool autostop{ false };
			bool predictive{ true };

			bool revolver_pre_cock{};
			int revolver_release_margin_ms{ 16 };
			legit_checks checks{};
		};

		struct other
		{
			bool penetration_crosshair{ false };
			zdraw::rgba penetration_color_yes{ 0, 255, 0, 255 };
			zdraw::rgba penetration_color_no{ 255, 0, 0, 255 };
		};

		struct global_settings
		{
			bool aimbot_enabled{ false };
			int aimbot_key{ VK_XBUTTON2 };
			int aimbot_activation_mode{ activation::hold };
			int aimbot_fov{ 5 };
			int aimbot_smoothing{ 5 };
			int aimbot_humanize{ 50 };
			bool aimbot_autowall{ true };
			float aimbot_min_damage{ 90.0f };
			bool aimbot_lethal_only{};
			int aimbot_hitbox_parts{ aim_part::head };
			bool aimbot_multipoint{ true };
			bool aimbot_visible_only{ true };
			bool aimbot_draw_fov{ false };
			zdraw::rgba aimbot_fov_color{ 225, 225, 225, 125 };
			bool aimbot_predictive{ true };
			bool aimbot_recoil_sync{ true };
			legit_checks aimbot_checks{};
			humanizer_settings aimbot_humanizer{};
			multipoint_settings aimbot_multipoint_config{};
			prediction_settings aimbot_prediction{};
			rcs_settings aimbot_rcs{};
			fov_settings aimbot_fov_config{};

			struct grenade_aim_config {
				bool enabled{ false };
				int key{ 0x05 };
				int fov{ 30 };
				int smoothing{ 10 };
			} grenade_aim{};

			bool triggerbot_enabled{ true };
			int triggerbot_key{ 0x43 };
			int triggerbot_activation_mode{ activation::hold };
			int triggerbot_seed_type{ seed_mode::none };
			int triggerbot_hitbox_parts{ aim_part::head };
			float triggerbot_hitchance{ 75.0f };
			int triggerbot_delay{ 10 };
			int triggerbot_randomize_ms{ 5 };
			float triggerbot_outlier_chance{ 3.0f };
			int triggerbot_outlier_delay_ms{ 20 };
			int triggerbot_delay_after_ms{ 100 };
			bool triggerbot_lethal_only{};
			int triggerbot_reaction_time{ 0 };
			float triggerbot_min_damage{ 90.0f };
			bool triggerbot_autowall{ true };
			bool triggerbot_autostop{ false };
			bool triggerbot_predictive{ true };
			bool triggerbot_revolver_pre_cock{};
			int triggerbot_revolver_release_margin_ms{ 16 };
			legit_checks triggerbot_checks{};

			bool penetration_crosshair{ false };
			zdraw::rgba penetration_color_yes{ 0, 255, 0, 255 };
			zdraw::rgba penetration_color_no{ 255, 0, 0, 255 };
		};

		struct group_overrides
		{
			bool use_global{ true };
			int aimbot_fov{ 5 };
			int aimbot_smoothing{ 5 };
			int aimbot_humanize{ 50 };
			bool aimbot_autowall{ true };
			float aimbot_min_damage{ 90.0f };
			bool aimbot_lethal_only{};
			int aimbot_hitbox_parts{ aim_part::head };
			bool aimbot_multipoint{ true };
			bool aimbot_visible_only{ true };
			bool aimbot_predictive{ true };
			bool aimbot_recoil_sync{ true };
			legit_checks aimbot_checks{};
			humanizer_settings aimbot_humanizer{};
			multipoint_settings aimbot_multipoint_config{};
			prediction_settings aimbot_prediction{};
			rcs_settings aimbot_rcs{};
			fov_settings aimbot_fov_config{};

			int triggerbot_seed_type{ seed_mode::none };
			int triggerbot_hitbox_parts{ aim_part::head };
			float triggerbot_hitchance{ 75.0f };
			int triggerbot_delay{ 10 };
			int triggerbot_randomize_ms{ 5 };
			float triggerbot_outlier_chance{ 3.0f };
			int triggerbot_outlier_delay_ms{ 20 };
			int triggerbot_delay_after_ms{ 100 };
			bool triggerbot_lethal_only{};
			int triggerbot_reaction_time{ 0 };
			float triggerbot_min_damage{ 90.0f };
			bool triggerbot_autowall{ true };
			bool triggerbot_autostop{ false };
			bool triggerbot_predictive{ true };
			bool triggerbot_revolver_pre_cock{};
			int triggerbot_revolver_release_margin_ms{ 16 };
			legit_checks triggerbot_checks{};
		};

		static constexpr std::uint32_t k_group_count{ 6 };

		global_settings global{};
		std::array<group_overrides, k_group_count> overrides{};

		struct resolved_config
		{
			aimbot aimbot{};
			triggerbot triggerbot{};
			other other{};
		};

		resolved_config get( std::uint32_t weapon_type ) const;
		[[nodiscard]] bool seed_trigger_configured( ) const noexcept;
		struct group_config
		{
			aimbot aimbot{};
			triggerbot triggerbot{};
			other other{};
		};
		std::array<group_config, k_group_count> groups{};
	};

}
