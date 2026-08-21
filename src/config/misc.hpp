#pragma once

#include <windows.h>

#include <cstdint>

#include <render/draw.hpp>

namespace config {

	struct general_profile
	{
		enum class screen_anchor : std::uint8_t
		{
			top_left,
			top_right,
			bottom_left,
			bottom_right
		};

		struct screen_layout
		{
			constexpr screen_layout( const float x = 40.0f, const float y = 40.0f )
				: offset_x( x ), offset_y( y ), legacy_position_x( x ), legacy_position_y( y )
			{
			}

			screen_anchor anchor{ screen_anchor::top_left };
			float offset_x{ 40.0f };
			float offset_y{ 40.0f };
			float scale{ 1.0f };
			int version{ 1 };

			float legacy_position_x{ 40.0f };
			float legacy_position_y{ 40.0f };
		};

		struct bullet_tracers
		{
			bool enabled{ false };
			zdraw::rgba color{ 192, 203, 229, 200 };
			float duration{ 2.0f };
			float thickness{ 1.5f };
			bool bloom{ true };
			int max_count{ 40 };
			bool draw_cubes{ true };
			bool draw_line{ true };
			float cube_half{ 1.25f };
			zdraw::rgba cube_edge_color{ 192, 203, 229, 230 };
			float cube_face_alpha{ 38.0f };
			float fade_near{ 45.0f };
			float fade_far{ 160.0f };
		} m_bullet_tracers{};

		struct hitmarker
		{
			bool enabled{ true };
			zdraw::rgba color{ 245, 247, 250, 255 };
			float size{ 8.0f };
			float gap{ 3.0f };
			float thickness{ 1.5f };
			float duration{ 0.55f };
		} m_hitmarker{};

		struct hitsound
		{
			bool enabled{ true };
			int style{ 4 };
			float volume{ 0.55f };
			bool show_damage{ true };
			zdraw::rgba damage_color{ 255, 108, 112, 255 };
			float damage_size{ 13.0f };
			float damage_duration{ 0.75f };
			float damage_rise{ 30.0f };
		} m_hitsound{};

		struct grenades
		{
			bool enabled{ true };
			bool local_only{ false };
			zdraw::rgba color{ 255, 255, 255, 200 };
			float thickness{ 2.0f };
			bool bloom{ true };
			zdraw::rgba bloom_color{ 120, 170, 255, 58 };
			float bloom_radius{ 3.0f };
			bool show_bounces{ true };
			zdraw::rgba bounce_color{ 120, 170, 255, 235 };
			float bounce_size{ 4.0f };
			bool show_endpoint{ true };
			zdraw::rgba endpoint_color{ 255, 255, 255, 235 };
			float endpoint_size{ 8.0f };
		} m_grenades{};

		struct nade_helper
		{
			bool enabled{ false };

			float draw_distance{ 800.0f };
			float stand_distance{ 220.0f };
			float stand_radius{ 22.0f };

			float release_radius{ 6.0f };
			float height_tolerance{ 8.0f };

			bool show_action{ true };
			bool show_distance{ true };

			zdraw::rgba plaque_background{ 12, 13, 18, 205 };
			zdraw::rgba plaque_text{ 245, 245, 250, 255 };
			zdraw::rgba plaque_accent{ 120, 170, 255, 255 };
			zdraw::rgba stand_marker{ 120, 170, 255, 235 };
			zdraw::rgba stand_marker_active{ 110, 235, 140, 245 };
			zdraw::rgba aim_marker{ 245, 245, 250, 235 };

			bool aim_assist{ true };
			bool auto_release{ true };
			int aim_key{ VK_XBUTTON2 };
			int aim_smoothing{ 18 };

			float aim_threshold{ 0.35f };
			int lock_time_ms{ 45 };
		} m_nade_helper{};

		struct watermark
		{
			bool enabled{ true };
			screen_layout layout{ 40.0f, 40.0f };
			bool vertical{};
			bool show_ping{ true };
			bool show_loss{ true };
			bool show_cpu{ true };
			bool show_fps{ true };
		} m_watermark{};

		struct spectator_list
		{
			bool enabled{ false };
			bool show_avatars{ true };
			screen_layout layout{ 40.0f, 320.0f };
		} m_spectator_list{};

		struct event_log
		{
			bool enabled{ false };
			screen_layout layout{ 40.0f, 410.0f };
			float duration{ 5.0f };
			int max_entries{ 5 };
			bool show_shots{ true };
			bool show_hits{ true };
			bool show_kills{ true };
			bool show_misses{ true };
			bool show_blocked{ true };
			bool show_info{ true };
		} m_event_log{};

		struct keybind_list
		{
			bool enabled{ false };
			screen_layout layout{ 40.0f, 260.0f };
			bool show_always{ true };
			bool show_hold{ true };
			bool show_toggle{ true };
		} m_keybind_list{};

		struct bomb_info
		{
			screen_layout layout{ 40.0f, 200.0f };
		} m_bomb_info{};

		struct bunny_hop
		{
			bool enabled{ false };
			int activation_key{ VK_SPACE };
		} m_bunny_hop{};

		struct edge_jump
		{
			bool enabled{ false };
			int activation_key{ VK_XBUTTON1 };
		} m_edge_jump{};

		struct auto_stop
		{
			bool enabled{ false };
			bool physical_fire{ true };
			bool predictive_trigger{ true };
			float stop_speed{ 8.0f };
			float required_shoot_speed{ 34.0f };
		} m_auto_stop{};

		int language{ 0 };
		float menu_scale{ 1.0f };
		struct interface_palette
		{
			zdraw::rgba background{ 13, 13, 18, 217 };
			zdraw::rgba panel{ 20, 20, 26, 179 };
			zdraw::rgba card{ 28, 28, 36, 153 };
			zdraw::rgba popup{ 13, 13, 18, 199 };
			zdraw::rgba accent{ 124, 58, 237, 255 };
			zdraw::rgba text{ 248, 248, 242, 255 };
			zdraw::rgba muted_text{ 161, 161, 170, 255 };
			zdraw::rgba border{ 255, 255, 255, 15 };
			zdraw::rgba hover{ 255, 255, 255, 20 };
		} palette{};

		bool auto_accept{ true };
		bool obs_bypass{ false };

		bool lua_enabled{ true };
		bool limit_fps{ true };
		int fps_limit{ 240 };
	};

}
