#pragma once

#include <cstdint>

#include <render/draw.hpp>
#include <core/input/activation.hpp>

namespace config {

	struct visual_profile
	{
		struct player
		{
			enum activation_type : int { always_on = 0, hold = 1, toggle = 2 };
			bool enabled{ true };
			int activation_mode{ always_on };
			int activation_key{};
			[[nodiscard]] bool active( ) const noexcept
			{
				return enabled && platform::windows::binding_active(
					activation_mode, activation_key, always_on, hold, toggle );
			}

			bool spectator_sync{ false };
			struct legit_sync
			{
				bool enabled{ false };
				bool direct_visible{ true };
				bool radar{ true };
				bool sound{ true };
				float radar_hold{ 2.0f };
				float sound_hold{ 1.6f };
				float sound_distance{ 1100.0f };
			} m_legit_sync{};

			static constexpr float layout_reference_width{ 172.2f };
			static constexpr float layout_reference_height{ 333.06f };

			[[nodiscard]] static float resolve_layout_axis( float min, float max, float coordinate, float reference_extent )
			{
				if ( coordinate < 0.0f ) return min + coordinate * reference_extent;
				if ( coordinate > 1.0f ) return max + ( coordinate - 1.0f ) * reference_extent;
				return min + ( max - min ) * coordinate;
			}

			struct layout_element
			{
				float x{};
				float y{};
				float scale{ 1.0f };
			};

			struct editor_layout
			{

				layout_element name{ 0.494192481f, -0.018994622f, 1.160000086f };
				layout_element weapon{ 0.50f, 1.038997650f, 1.0f };
				layout_element health{ -0.017735176f, 0.50f, 0.920000017f };
				layout_element armor{ 0.50f, 1.012f, 1.0f };
				layout_element flags{ 0.949290633f, -0.009056670f, 1.0f };
			} m_layout{};

			struct box
			{
				enum class style_type : std::uint8_t { full, cornered };
				bool enabled{ true };
				style_type style{ style_type::cornered };
				bool fill{ false };
				bool outline{ false };
				float corner_length{ 10.0f };
				zdraw::rgba visible_color{ 220, 60, 60, 255 };
				zdraw::rgba occluded_color{ 160, 50, 50, 180 };

				zdraw::rgba fill_visible_color{ 60, 200, 100, 80 };
				zdraw::rgba fill_occluded_color{ 100, 100, 110, 80 };
			} m_box{};

			struct skeleton
			{
				bool enabled{ true };
				float thickness{ 1.0f };
				zdraw::rgba visible_color{ 255, 255, 255, 255 };
				zdraw::rgba occluded_color{ 255, 255, 255, 255 };
			} m_skeleton{};

			struct head_circle
			{
				bool enabled{ true };
				float thickness{ 1.0f };
				zdraw::rgba color{ 255, 255, 255, 255 };
			} m_head_circle{};

			struct view_line
			{
				bool enabled{ true };
				float length{ 50.0f };
				float thickness{ 1.0f };
				zdraw::rgba color{ 255, 255, 255, 255 };
			} m_view_line{};

			struct offscreen_arrows
			{
				bool enabled{ true };
				float size{ 22.0f };
				float radius{ 377.0f };
				float thickness{ 2.25f };
				zdraw::rgba color{ 204, 230, 241, 255 };
				bool bloom{ true };
				zdraw::rgba bloom_color{ 120, 190, 255, 210 };
				float bloom_radius{ 6.0f };
				float bloom_speed{ 0.50f };
				float bloom_min_alpha{ 0.12f };
				float bloom_max_alpha{ 0.68f };
			} m_offscreen_arrows{};

			struct hitboxes
			{
				bool enabled{ false };
				zdraw::rgba visible_color{ 240, 240, 245, 15 };
				zdraw::rgba occluded_color{ 70, 70, 80, 12 };
				bool fill{ true };
				bool outline{ true };
			} m_hitboxes{};

			struct threat_module
			{
				bool enabled{ false };
				float max_distance{ 60.0f };
				bool head_hitbox{ true };
				bool body_hitbox{ true };
				bool limb_hitbox{ true };
				float fill_alpha{ 30.0f };
				float outline_alpha{ 220.0f };
				float outline_thickness{ 1.0f };
				zdraw::rgba head_color{ 230, 50, 50, 110 };
				zdraw::rgba body_color{ 255, 255, 255, 70 };
				zdraw::rgba limb_color{ 255, 255, 255, 50 };
			} m_threat_module{};

			struct health_bar
			{
				enum class position_type : std::uint8_t { left, top, bottom, right };
				bool enabled{ true };
				position_type position{ position_type::left };
				float thickness{ 3.0f };
				bool outline{ true };
				float outline_thickness{ 1.0f };
				bool gradient{ true };
				bool show_value{ true };
				int segments{ 1 };
				float segment_gap{ 2.5f };
				zdraw::rgba full_color{ 215, 50, 220, 255 };
				zdraw::rgba low_color{ 220, 50, 50, 255 };
				zdraw::rgba background_color{ 0, 17, 28, 220 };
				zdraw::rgba outline_color{ 0, 0, 0, 255 };
				zdraw::rgba text_color{ 245, 245, 250, 255 };
			} m_health_bar{};

			struct armor_bar
			{
				enum class position_type : std::uint8_t { left, top, bottom, right };
				bool enabled{ true };
				position_type position{ position_type::bottom };
				float thickness{ 3.5f };
				bool outline{ true };
				float outline_thickness{ 1.0f };
				bool gradient{ false };
				bool show_value{ false };
				int segments{ 1 };
				float segment_gap{ 1.0f };
				zdraw::rgba full_color{ 50, 80, 220, 255 };
				zdraw::rgba low_color{ 50, 80, 220, 255 };
				zdraw::rgba background_color{ 10, 10, 14, 220 };
				zdraw::rgba outline_color{ 0, 0, 0, 255 };
				zdraw::rgba text_color{ 245, 245, 250, 255 };
			} m_armor_bar{};

			struct info_flags
			{
				enum flag : std::uint16_t
				{
					none = 0,
					money = 1 << 0,
					armor = 1 << 1,
					kit = 1 << 2,
					scoped = 1 << 3,
					defusing = 1 << 4,
					flashed = 1 << 5,
					ping = 1 << 6,
					distance = 1 << 7,
					bomb_damage = 1 << 8
				};
				struct style
				{
					zdraw::rgba color{};
					float scale{ 1.0f };
				};
				bool enabled{ true };
				std::uint16_t flags{ flag::money | flag::armor | flag::kit | flag::scoped |
					flag::defusing | flag::flashed | flag::ping | flag::bomb_damage };
				style money_style{ { 60, 210, 100, 255 }, 1.0f };
				style armor_style{ { 60, 120, 220, 255 }, 1.0f };
				style kit_style{ { 180, 180, 200, 255 }, 1.0f };
				style scoped_style{ { 240, 240, 245, 255 }, 1.0f };
				style defusing_style{ { 220, 60, 60, 255 }, 1.0f };
				style flashed_style{ { 200, 200, 210, 255 }, 1.0f };
				style ping_style{ { 98, 217, 109, 255 }, 1.0f };
				style distance_style{ { 130, 130, 145, 255 }, 1.0f };
				style bomb_damage_style{ { 255, 120, 120, 255 }, 1.0f };
				[[nodiscard]] bool has( flag f ) const { return this->flags & f; }
			} m_info_flags{};

			struct name
			{
				bool enabled{ true };
				zdraw::rgba color{ 245, 245, 250, 240 };
			} m_name{};

			struct weapon
			{
				enum class display_type : std::uint8_t { text, icon, text_and_icon };
				bool enabled{ true };
				display_type display{ display_type::icon };
				zdraw::rgba text_color{ 200, 200, 210, 220 };
				zdraw::rgba icon_color{ 240, 240, 245, 240 };
				struct ammo_indicator
				{
					bool enabled{ true };
					bool show_count{ false };
					zdraw::rgba empty_color{ 225, 55, 65, 245 };
				} ammo{};
			} m_weapon{};

		} m_player{};

		struct item
		{
			bool enabled{ true };
			float max_distance{ 40.0f };
			struct icon { bool enabled{ true }; zdraw::rgba color{ 255, 255, 255, 220 }; } m_icon{};
			struct name { bool enabled{ false }; zdraw::rgba color{ 200, 200, 200, 180 }; } m_name{};
			struct ammo { bool enabled{ true }; zdraw::rgba color{ 255, 255, 255, 200 }; zdraw::rgba empty_color{ 220, 80, 80, 200 }; } m_ammo{};
			struct filters { bool rifles{ true }; bool smgs{ true }; bool shotguns{ true }; bool snipers{ true }; bool pistols{ true }; bool heavy{ true }; bool grenades{ true }; bool utility{ true }; } m_filters{};
		} m_item{};

		struct projectile
		{
			bool enabled{ true };
			bool show_icon{ true };
			bool show_timer_ring{ true };
			bool show_inferno_bounds{ true };
			zdraw::rgba default_color{ 195, 200, 215, 200 };
			zdraw::rgba color_he{ 220, 150, 150, 220 };
			zdraw::rgba color_flash{ 230, 220, 150, 220 };
			zdraw::rgba color_smoke{ 160, 200, 180, 220 };
			zdraw::rgba color_molotov{ 220, 170, 130, 220 };
			zdraw::rgba color_decoy{ 200, 200, 200, 200 };
			zdraw::rgba timer_high_color{ 255, 255, 255, 255 };
			zdraw::rgba timer_low_color{ 220, 100, 100, 255 };
			zdraw::rgba indicator_background{ 15, 16, 22, 185 };
			float inferno_gradient_width{ 34.0f };
			float inferno_gradient_opacity{ 45.0f };
		} m_projectile{};

		struct bomb
		{
			bool enabled{ true };
			bool show_active_bomb{ true };
			zdraw::rgba active_bomb_color{ 255, 200, 50, 255 };
			bool show_planted_bomb{ true };
			zdraw::rgba bomb_color_t{ 255, 180, 50, 255 };
			zdraw::rgba bomb_color_ct{ 50, 180, 255, 255 };
			bool show_timer{ true };
			zdraw::rgba timer_text_color{ 255, 220, 100, 255 };
			bool show_info_panel{ true };
			zdraw::rgba panel_background{ 12, 13, 18, 225 };

			bool show_safe_zone{ true };
			zdraw::rgba safe_zone_color{ 70, 235, 105, 240 };
			int safe_zone_bands{ 1 };
			float safe_zone_band_step{ 4.0f };
			float safe_zone_draw_radius{ 1200.0f };
		} m_bomb{};

		struct no_flash
		{
			bool enabled{ true };
			float max_distance{ 1000.0f };
			zdraw::rgba background_color{ 0, 0, 0, 255 };
			zdraw::rgba wireframe_color{ 200, 200, 200, 150 };
		} m_no_flash{};

		struct no_smoke
		{
			bool enabled{ false };
			zdraw::rgba wireframe_color{ 190, 210, 220, 180 };
		} m_no_smoke{};

		struct crosshair
		{
			bool enabled{ false };

			bool copy_game{ true };

			bool sync{ true };
			bool dot{ true };
			bool lines{ true };
			bool t_style{ false };
			float length{ 5.0f };
			float thickness{ 1.0f };
			float gap{ 3.0f };
			zdraw::rgba color{ 255, 255, 255, 255 };
			zdraw::rgba outline_color{ 0, 0, 0, 255 };
			bool outline{ true };
			float outline_thickness{ 1.0f };
			bool penetration_enabled{ false };
			zdraw::rgba penetration_color_yes{ 0, 255, 0, 255 };
			zdraw::rgba penetration_color_no{ 255, 0, 0, 255 };
			float penetration_min_damage{ 90.0f };
		} m_crosshair{};

		struct chams
		{

			enum material_type : int
			{
				solid = 0,
				shaded,
				glow,
				glow_outline,
				iridescent,
				water_flow,
				glossy,
				material_type_count
			};

			struct material
			{
				bool enabled{ true };
				int type{ solid };
				bool wireframe{ false };
				zdraw::rgba color{ 255, 60, 60, 220 };

				float roughness{ 0.35f };
				float metalness{ 0.0f };
				float exponent{ 2.0f };
				float falloff{ 0.5f };
				float fresnel_fill{ 0.25f };
				float strength{ 1.0f };
				float speed{ 1.0f };
				zdraw::rgba tint{ 120, 200, 255, 255 };
			};

			bool enabled{ true };

			bool antialiasing{ true };

			struct model_glow
			{
				bool enabled{ true };
				zdraw::rgba color{ 125, 85, 255, 150 };
				float radius{ 4.0f };
				float strength{ 0.65f };
				int layers{ 3 };
			} glow_effect{};

			struct kill_particles
			{
				bool enabled{ true };
				zdraw::rgba color{ 170, 125, 255, 220 };
				float duration{ 0.9f };
				float size{ 3.0f };
				int count{ 14 };
			} kill_effect{};

			struct shot_ghost
			{
				bool enabled{ true };
				float duration{ 0.55f };
				material appearance{
					.enabled = true,
					.type = glow_outline,
					.wireframe = false,
					.color = { 255, 185, 80, 150 },
					.exponent = 3.0f,
					.falloff = 0.18f,
					.fresnel_fill = 0.22f };
			} on_shot{};

			bool occlude_dynamic_doors{ true };
			bool occlude_smoke{ true };

			material visible{
				.enabled = true,
				.type = glow_outline,
				.wireframe = false,
				.color = { 136, 60, 255, 184 },
				.roughness = 0.349999994f,
				.metalness = 0.0f,
				.exponent = 5.099999905f,
				.falloff = 0.050000001f,
				.fresnel_fill = 0.719999969f,
				.strength = 0.329999983f,
				.speed = 4.0f,
				.tint = { 120, 200, 255, 255 }
			};
			material invisible{
				.enabled = true,
				.type = water_flow,
				.wireframe = false,
				.color = { 60, 71, 255, 200 },
				.roughness = 0.349999994f,
				.metalness = 0.0f,
				.exponent = 2.0f,
				.falloff = 0.5f,
				.fresnel_fill = 0.25f,
				.strength = 1.0f,
				.speed = 1.200000048f,
				.tint = { 120, 200, 255, 255 }
			};
		} m_chams{};

		struct radar
		{
			enum activation_type : int
			{
				always_on = 0,
				hold = 1,
				toggle = 2
			};
			bool enabled{ true };
			int activation_mode{ always_on };
			int activation_key{};
			[[nodiscard]] bool active( ) const noexcept
			{
				return enabled && platform::windows::binding_active(
					activation_mode, activation_key, always_on, hold, toggle );
			}
			bool show_names{ true };
			bool show_health{ true };
			bool show_armor{ true };
			bool show_weapon{ true };
			bool show_projectiles{ true };
			bool show_trajectories{ true };
			bool show_grenade_zones{ true };
			float information_scale{ 0.75f };
			float marker_scale{ 1.0f };
			bool text_outline{ true };
			float text_outline_thickness{ 1.0f };
			zdraw::rgba text_outline_color{ 0, 0, 0, 230 };
			zdraw::rgba enemy_color{ 255, 64, 64, 255 };
			zdraw::rgba direction_color{ 255, 255, 255, 255 };
			zdraw::rgba name_color{ 245, 245, 250, 255 };
			zdraw::rgba weapon_color{ 205, 212, 225, 245 };
			zdraw::rgba status_color{ 225, 230, 240, 245 };
			zdraw::rgba health_color{ 95, 230, 125, 255 };
			zdraw::rgba armor_color{ 80, 155, 255, 255 };
			zdraw::rgba he_color{ 255, 105, 80, 255 };
			zdraw::rgba flash_color{ 250, 245, 180, 255 };
			zdraw::rgba smoke_color{ 180, 190, 205, 255 };
			zdraw::rgba molotov_color{ 255, 145, 55, 255 };
			zdraw::rgba decoy_color{ 140, 190, 255, 255 };
			float trajectory_thickness{ 1.5f };
			float trajectory_endpoint_size{ 3.0f };
			float zone_fill_alpha{ 15.0f };
			float zone_outline_alpha{ 59.0f };
			float zone_outline_thickness{ 1.0f };
		} m_radar{};

		struct sound_esp
		{
			bool enabled{ false };
			bool local_sync{ false };
			float duration{ 1.6f };
			float radius{ 34.0f };
			zdraw::rgba color{ 120, 200, 255, 200 };
		} m_sound{};
	};

}
