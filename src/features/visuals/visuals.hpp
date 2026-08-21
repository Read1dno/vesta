#pragma once

#include <simulation/ballistics.hpp>
#include <simulation/grenade.hpp>
#include <core/state/pose.hpp>

namespace features::visuals {
	using simulation::grenade_path;
	using simulation::grenade_trajectory_engine;

		class player_t
		{
		public:
			void render( zdraw::draw_list& draw_list,
				const std::shared_ptr<const game::player_pose_frame>& frame );
			[[nodiscard]] static std::string_view weapon_glyph( std::string_view weapon_name );

			inline static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 18> skeleton_connections
			{ {
				{ 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 23 }, { 23, 6 }, { 6, 7 },
				{ 6, 9 }, { 9, 10 }, { 10, 11 },
				{ 6, 13 }, { 13, 14 }, { 14, 15 },
				{ 1, 17 }, { 17, 18 }, { 18, 19 },
				{ 1, 20 }, { 20, 21 }, { 21, 22 },
			} };
			inline static constexpr std::array<int, 1> threat_head_bones{ 7 };
			inline static constexpr std::array<int, 6> threat_body_bones{ 6, 23, 4, 3, 2, 1 };
			inline static constexpr std::array<int, 14> threat_limb_bones{ 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22 };

		private:
			void paint_frame( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const config::visual_profile::player::box& cfg, bool is_visible );
			void paint_skeleton( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const config::visual_profile::player::skeleton& cfg, bool is_visible );
			void paint_head_marker( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, const config::visual_profile::player::head_circle& cfg );
			void paint_view_direction( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, const config::visual_profile::player::view_line& cfg );
			void paint_offscreen_markers( zdraw::draw_list& draw_list, const std::vector<game::player_snapshot>& players, const config::visual_profile::player::offscreen_arrows& cfg );
			void paint_threat_highlights( zdraw::draw_list& draw_list, const game::skeleton_reader::data& bones, const game::player_snapshot& player, float current_time );
			void paint_health_meter( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::health_bar& cfg );
			void paint_armor_meter( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::armor_bar& cfg );
			void paint_identity( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::name& cfg );
			void paint_weapon_label( zdraw::draw_list& draw_list, const game::bounds_projector::data& bounds, const game::player_snapshot& player, const config::visual_profile::player::weapon& cfg );
			struct active_bomb_info {
				bool valid{false};
				foundation::vec3 position{};
				float blow_time{0.0f};
				std::int32_t baked_site{ -1 };
			};
			void paint_status_labels( zdraw::draw_list& draw_list,
				zdraw::draw_list& bright_draw_list,
				const game::bounds_projector::data& bounds,
				const game::player_snapshot& player,
				const config::visual_profile::player& cfg,
				const active_bomb_info& bomb, float current_time );
			struct animation_data
			{
				motion::damped_value health{};
				motion::damped_value armor{};
				bool initialized{ false };
				float last_damage_time{ 0.0f };
				int last_health{ 100 };
			};

			std::unordered_map<std::uintptr_t, animation_data> m_animations{};
		};

		class item_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );
		};

		class projectile_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );

		private:
			void draw_inferno_bounds( zdraw::draw_list& draw_list, const game::projectile_snapshot& proj, const config::visual_profile::projectile& cfg ) const;
			std::unordered_map<std::uintptr_t, float> m_decoy_start_times{};
		};

		class bomb_t
		{
		public:
			struct hud_snapshot
			{
				bool active{ false };
				foundation::vec3 active_position{};
				bool planted{ false };
				float time_remaining{ 0.0f };
				float timer_length{ 40.0f };
				bool being_defused{ false };
				float defuse_remaining{ 0.0f };
				float defuse_length{ 0.0f };
				bool defuse_success{ false };
				int bomb_site{ 0 };
				int predicted_damage{ -1 };
				int local_health{ 0 };
				foundation::vec3 position{};
			};
			struct damage_snapshot
			{
				bool valid{ false };
				foundation::vec3 position{};
				float blow_time{};
				std::int32_t baked_site{ -1 };
			};

			void tick( );
			void on_render( zdraw::draw_list& draw_list );
			[[nodiscard]] hud_snapshot info_snapshot( ) const;
			[[nodiscard]] damage_snapshot player_damage_snapshot( ) const;

		private:
			struct bomb_data
			{
				bool planted{ false };
				foundation::vec3 position{};
				float blow_time{ 0.0f };
				float timer_length{ 40.0f };
				float defuse_length{ 0.0f };
				float defuse_countdown{ 0.0f };
				bool being_defused{ false };
				int bomb_site{ 0 };
				std::int32_t baked_site{ -1 };
				int predicted_damage{ -1 };
			};

			struct active_bomb_data
			{
				foundation::vec3 position{};
				bool valid{ false };
			};

			void draw_planted_bomb( zdraw::draw_list& draw_list, const bomb_data& data, const config::visual_profile::bomb& cfg, float current_time ) const;
			void draw_active_bomb( zdraw::draw_list& draw_list, const active_bomb_data& data, const config::visual_profile::bomb& cfg ) const;

			void draw_safe_zone( zdraw::draw_list& draw_list, const bomb_data& data,
				const config::visual_profile::bomb& cfg, int local_health );
			void rebuild_safe_zone( const foundation::vec3& center,
				std::int32_t site, float threshold, int bands, float band_step );

			[[nodiscard]] zdraw::rgba lerp_color( const zdraw::rgba& a, const zdraw::rgba& b, float t ) const;

			struct zone_segment
			{
				foundation::vec3 a{};
				foundation::vec3 b{};
				std::uint32_t node_a{};
				std::uint32_t node_b{};
				std::uint8_t band{};
			};

			struct zone_polyline
			{
				std::vector<foundation::vec3> points{};
				std::uint8_t band{};
				bool closed{};
			};

			std::vector<zone_segment> m_zone_segments{};
			std::vector<zone_polyline> m_zone_polylines{};
			std::vector<game::blast_model::grid_quad> m_zone_surfaces{};
			std::vector<float> m_zone_damage{};
			std::vector<float> m_zone_screen_points{};
			mutable std::shared_mutex m_state_mutex{};
			bomb_data m_sampled_planted{};
			active_bomb_data m_sampled_active{};
			float m_sampled_game_time{};
			int m_sampled_health{};
			std::chrono::steady_clock::time_point m_sampled_at{};
			foundation::vec3 m_zone_center{};
			float m_zone_threshold{ -1.0f };
			std::int32_t m_zone_site{ -1 };
			std::int32_t m_zone_grid_min_x{};
			std::int32_t m_zone_grid_min_y{};
			int m_zone_grid_width{};
			int m_zone_grid_height{};
			int m_zone_bands{ 0 };
			float m_zone_step{ 0.0f };
		};

		class sound_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );

		private:
			struct event
			{
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point spawn{};
			};

			void draw_ring( zdraw::draw_list& draw_list, const foundation::vec3& center, float radius, const zdraw::rgba& color, float fade );

			std::unordered_map<std::uintptr_t, float> m_last_emit{};
			std::unordered_map<std::uintptr_t,
				std::chrono::steady_clock::time_point> m_last_event{};
			std::vector<event> m_events{};
			bool m_last_local_sync{};
		};

		class radar_t
		{
		public:
			void tick( );
			void on_render( zdraw::draw_list& draw_list );
		};

		class crosshair_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );
		};

		class grenade_prediction_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );

		private:
			struct held_grenade_snapshot
			{
				std::uintptr_t weapon{};
				std::uintptr_t weapon_vdata{};
				std::uint16_t item_definition{};
				bool valid{};
			};

			struct in_flight_grenade
			{
				std::uintptr_t entity{};
				std::uintptr_t weapon_id{};
				grenade_path traj{};
				std::chrono::steady_clock::time_point throw_time{};
				std::chrono::steady_clock::time_point detonate_time{};
				std::chrono::steady_clock::time_point last_seen{};
				bool detonated{ false };
			};

			[[nodiscard]] static held_grenade_snapshot sample_held_grenade( );
			[[nodiscard]] bool preview_allowed( const held_grenade_snapshot& weapon ) const;
			void refresh_weapon_profile( const held_grenade_snapshot& weapon );
			void sample_throw( const held_grenade_snapshot& weapon,
				foundation::vec3& origin, foundation::vec3& velocity );

			void reconcile_live_projectiles( );
			[[nodiscard]] static std::uintptr_t weapon_id_for(
				game::projectile_kind type );

			void draw_path( zdraw::draw_list& draw_list,
				const grenade_path& trajectory, float alpha ) const;

			std::uintptr_t m_weapon_vdata{};
			std::uintptr_t m_weapon_id{};
			float m_throw_velocity{};

			std::vector<in_flight_grenade> m_in_flight{};
			grenade_path m_preview{};
			grenade_path m_display_preview{};
			grenade_trajectory_engine m_trajectory_engine{};
			std::chrono::steady_clock::time_point m_last_throw_time{};
			std::chrono::steady_clock::time_point m_last_flight_update{};
			std::chrono::steady_clock::time_point m_last_preview_update{};
			std::chrono::steady_clock::time_point m_last_preview_blend{};
			bool m_was_holding{ false };

			static constexpr auto throw_cooldown{ 1.0f };
			static constexpr auto missing_grace{ 0.5f };
		};

		class bullet_impacts_t
		{
		public:
			void on_render( zdraw::draw_list& draw_list );
			struct confirmed_hit
			{
				std::uintptr_t pawn{};
				std::uint64_t sequence{};
				int damage{};
				bool killed{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			[[nodiscard]] confirmed_hit latest_confirmed_hit( ) const;
			[[nodiscard]] std::vector<confirmed_hit> confirmed_hits_since(
				std::uint64_t sequence ) const;
		private:
			struct tracer_t
			{
				foundation::vec3 start;
				std::vector<foundation::vec3> impacts;
				std::chrono::steady_clock::time_point timestamp;
				float shot_time{};
				bool exact{};
			};
			struct pending_shot_t
			{
				foundation::vec3 start{};
				foundation::vec3 direction{};
				foundation::vec3 traced_end{};
				std::chrono::steady_clock::time_point timestamp{};
				bool resolved{};
			};
			struct hit_candidate_t
			{
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct hitmarker_t
			{
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct pending_damage_t
			{
				std::uintptr_t pawn{};
				foundation::vec3 position{};
				int health_before{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct damage_popup_t
			{
				foundation::vec3 position{};
				int damage{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct tracked_health_t
			{
				std::uintptr_t pawn{};
				std::uint32_t pawn_handle{};
				int health{};
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point seen{};
			};
			struct pending_server_damage_t
			{
				std::uintptr_t pawn{};
				foundation::vec3 position{};
				int damage{};
				bool killed{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct action_feedback_t
			{
				int damage{};
				int kills{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct recent_hit_position_t
			{
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			struct server_health_t
			{
				std::uintptr_t pawn{};
				std::uint32_t pawn_handle{};
				int health{};
				foundation::vec3 position{};
				std::chrono::steady_clock::time_point seen{};
			};
			struct local_utility_t
			{
				foundation::vec3 position{};
				game::projectile_kind kind{ game::projectile_kind::unknown };
				std::chrono::steady_clock::time_point seen{};
				bool triggered{};
			};
			struct utility_damage_source_t
			{
				foundation::vec3 position{};
				float radius{};
				std::chrono::steady_clock::time_point expires{};
			};
			struct pending_nonbullet_damage_t
			{
				std::uintptr_t pawn{};
				foundation::vec3 position{};
				int damage{};
				bool killed{};
				std::chrono::steady_clock::time_point timestamp{};
			};
			std::vector<tracer_t> m_tracers{};
			std::vector<pending_shot_t> m_pending_shots{};
			std::vector<hit_candidate_t> m_hit_candidates{};
			std::vector<hitmarker_t> m_hitmarkers{};
			std::vector<pending_damage_t> m_pending_damage{};
			std::vector<damage_popup_t> m_damage_popups{};
			std::unordered_map<std::uintptr_t, int> m_known_health{};
			std::unordered_map<std::uint32_t, tracked_health_t> m_tracked_health{};
			std::unordered_map<std::uintptr_t, local_utility_t> m_local_utility{};
			std::vector<utility_damage_source_t> m_utility_damage_sources{};
			std::vector<pending_nonbullet_damage_t> m_pending_nonbullet_damage{};
			std::unordered_map<std::uintptr_t, std::chrono::steady_clock::time_point> m_recent_bullet_hits{};
			mutable std::mutex m_confirmed_hits_mutex{};
			confirmed_hit m_latest_confirmed_hit{};
			std::vector<confirmed_hit> m_confirmed_hits{};
			std::unordered_map<std::uint32_t, server_health_t> m_server_health{};
			std::unordered_map<std::uintptr_t, recent_hit_position_t> m_recent_hit_positions{};

			std::unordered_map<std::uintptr_t, recent_hit_position_t> m_local_victim_evidence{};
			std::vector<pending_server_damage_t> m_pending_server_damage{};
			std::vector<std::chrono::steady_clock::time_point> m_hit_confirmations{};
			std::vector<action_feedback_t> m_action_feedback{};
			std::uintptr_t m_action_tracking_services{};
			float m_last_round_damage{};
			int m_last_round_kills{};
			bool m_action_tracking_initialized{};
			std::unordered_map<std::uintptr_t, float> m_seen_impacts{};

			std::unordered_set<std::uint64_t> m_seen_bullet_service{};
			int m_last_bullet_service_count{ -1 };
			std::unordered_set<std::uint64_t> m_seen_hitmarker_impacts{};
			int m_last_hitmarker_impact_count{ -1 };
			int m_last_total_hits{ -1 };
			std::uintptr_t m_hit_counter_pawn{};
			float m_last_hitmarker_shot_time{ -1.0f };
			float m_last_fire_time{ -1.0f };
			int m_last_shot_render_tick{ -1 };
			std::chrono::steady_clock::time_point m_next_capture{};
			simulation::ballistics_t::context m_cached_weapon_ctx{};
			bool m_has_cached_weapon_ctx{};

			void collect_nonbullet_damage_feedback( );
			void capture_shot( );
			void collect_exact_impacts( );
			void resolve_pending_traces( );
			void poll_server_hits( );
			void poll_action_tracking( );
			void collect_server_damage( );
			void resolve_action_feedback( );
			void resolve_server_damage( );
			void emit_confirmed_hit( std::uintptr_t pawn,
				const foundation::vec3& position, int damage, bool killed,
				bool correlate_trigger = true );
			void resolve_hitmarkers( );
			void render_hitmarkers( zdraw::draw_list& draw_list );
			void render_damage_numbers( zdraw::draw_list& draw_list );
			void add_tracer( const foundation::vec3& start, const foundation::vec3& end, bool exact, float shot_time = 0.0f );
			void collect_bullet_hit_models( std::uintptr_t pawn, bool enemy_pawn );
			void collect_bullet_service_impacts( std::uintptr_t pawn );
			void collect_impact_hitmarkers( std::uintptr_t pawn );
			[[nodiscard]] bool select_pending_shot( const foundation::vec3& end, foundation::vec3& start,
				const foundation::vec3* observed_start = nullptr );
			[[nodiscard]] bool bullet_hit_endpoint( std::uintptr_t entity, const foundation::vec3& start, foundation::vec3& end ) const;
		};

	inline player_t& player( ) { static player_t value{}; return value; }
	inline item_t& items( ) { static item_t value{}; return value; }
	inline projectile_t& projectiles( ) { static projectile_t value{}; return value; }
	inline bomb_t& bomb( ) { static bomb_t value{}; return value; }
	inline sound_t& sound( ) { static sound_t value{}; return value; }
	inline grenade_prediction_t& grenade_prediction( ) { static grenade_prediction_t value{}; return value; }
	inline bullet_impacts_t& bullet_impacts( ) { static bullet_impacts_t value{}; return value; }
	inline radar_t& radar( ) { static radar_t value{}; return value; }
	inline crosshair_t& crosshair( ) { static crosshair_t value{}; return value; }

}
