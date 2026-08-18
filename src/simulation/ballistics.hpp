#pragma once

namespace simulation {

		class ballistics_t
		{
		public:
			struct inaccuracy_debug_data
			{
				std::pair<float, float> inaccuracy_crouch{};
				std::pair<float, float> inaccuracy_stand{};
				std::pair<float, float> inaccuracy_ladder{};
				std::pair<float, float> inaccuracy_move{};
				std::pair<float, float> max_speed{};
				float inaccuracy_jump_initial{};
				float inaccuracy_jump_apex{};
				float recovery_time_crouch{};
				float recovery_time_stand{};
				float recovery_time_crouch_final{};
				float recovery_time_stand_final{};
				int recovery_transition_start{};
				int recovery_transition_end{};
				int num_bullets{};
				bool is_revolver{};
				int fire_mode{};
				float turning_inaccuracy{};
				float accuracy_penalty{};
				int recoil_index{};
				foundation::vec3 velocity{};
				float speed{};
				std::uint8_t move_type{};
				bool is_walking{};
				bool on_ground{};
				bool crouching{};
				float base_inaccuracy{};
				float recovery{};
				float move_factor{};
				float move_inaccuracy{};
				float strafing_inaccuracy{};
				float air_inaccuracy{};
				float final_inaccuracy{};
			};

			struct context
			{
				std::uintptr_t weapon;
				std::uintptr_t weapon_vdata;
				std::uint32_t weapon_type;
				std::uint16_t item_def_idx;
				int num_bullets;
				int fire_mode;
				float inaccuracy;
				float spread;
				float recoil_index;
				bool is_reloading;
				bool is_full_auto;
				bool is_scoped;
				bool weapon_ready;
				int clip;
				float current_time;
				int global_tick;
				float global_tick_fraction;
				float cycle_time;
				float last_shot_time;
			float last_fired_weapon_time;
				int next_primary_attack_tick;
				float next_primary_attack_ratio;
				int next_secondary_attack_tick;
				float next_secondary_attack_ratio;
				int postpone_fire_ready_tick;
				float postpone_fire_ready_fraction;
				int player_tick;

				float wat_tick_offset;
				bool valid;
				inaccuracy_debug_data debug;

				foundation::vec3 velocity{};
				float velocity_length{};
				bool on_ground{};
				std::uint32_t ground_entity{};
				bool is_walking{};
			};

			class penetration
			{
			public:
				struct weapon_data
				{
					float damage;
					float penetration;
					float range_modifier;
					float range;
					float armor_ratio;
					float headshot_multiplier;
				};

				struct result
				{
					float damage;
					float distance;
					int hitbox;
					bool penetrated;
				};

				void prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon );

				[[nodiscard]] bool run( const foundation::vec3& start, const foundation::vec3& end, const game::player_snapshot& target, const game::skeleton_reader::data& bones, result& out ) const;
				[[nodiscard]] bool run_seed( const foundation::vec3& origin,
					const foundation::vec3& direction,
					const game::player_snapshot& target,
					const game::skeleton_reader::data& bones, int hitbox_parts,
					bool allow_penetration, float minimum_damage,
					int required_hitbox, result& out ) const;
				[[nodiscard]] bool can( const foundation::vec3& start, const foundation::vec3& direction, float& out_damage ) const;
				[[nodiscard]] float get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const;
				[[nodiscard]] const weapon_data& get_weapon_data( ) const { return this->m_weapon_data; }

			private:
				weapon_data m_weapon_data{};
				std::uintptr_t m_weapon_vdata{};
			};

			void tick( );

			[[nodiscard]] bool seed_weapon( std::uintptr_t pawn,
				std::uintptr_t controller, const foundation::vec3& velocity,
				context& output );

			[[nodiscard]] context ctx( ) const
			{
				std::shared_lock lock( this->m_ctx_mutex );
				return this->m_ctx;
			}
			[[nodiscard]] context shot_ctx( float fire_time, std::uintptr_t weapon,
				float post_shot_time ) const;
			[[nodiscard]] const penetration& pen( ) const { return this->m_pen; }

			[[nodiscard]] float estimate_hit_probability( const foundation::vec3& eye_pos, const foundation::vec3& aim_angle, const game::player_snapshot& target, const game::skeleton_reader::data& bones, int hitbox_parts ) const;
			[[nodiscard]] std::uint32_t derive_command_seed( const foundation::vec3& angles, int tick ) const;
			[[nodiscard]] foundation::vec2 sample_spread_offset( int seed, float accuracy, float spread,
				float recoil_index, int item_def_idx, int weapon_mode, int num_bullets = 1,
				int bullet_index = 0 ) const;
			[[nodiscard]] foundation::vec2 sample_predicted_spread( int seed,
				float inaccuracy, float spread, float recoil_index,
				int item_def_idx, int weapon_mode, int bullet_index ) const;
			[[nodiscard]] foundation::vec3 predict_counter_movement_origin( const foundation::vec3& pos ) const;
			[[nodiscard]] bool precision_ready( ) const;
			[[nodiscard]] float command_lead_time( ) const;
			[[nodiscard]] float get_spread( std::uintptr_t weapon_vdata,
				int fire_mode ) const;
			[[nodiscard]] float get_inaccuracy( std::uintptr_t pawn, std::uintptr_t weapon,
				std::uintptr_t weapon_vdata, std::uint32_t weapon_type,
				const foundation::vec3& eye_angles, inaccuracy_debug_data& dbg ) const;
			[[nodiscard]] float get_inaccuracy_preshot( std::uintptr_t weapon, std::uintptr_t weapon_vdata, std::uint32_t weapon_type, const foundation::vec3& preshot_velocity, bool preshot_on_ground, float preshot_accuracy_penalty, int preshot_recoil_index, bool preshot_is_walking, inaccuracy_debug_data& dbg ) const;
			[[nodiscard]] bool ray_hits_capsule( const foundation::vec3& ray_origin, const foundation::vec3& ray_dir, const foundation::vec3& capsule_start, const foundation::vec3& capsule_end, float radius ) const;

			context m_ctx{};

			std::uintptr_t m_ctx_pawn{};
			std::uintptr_t m_ctx_weapon{};
			std::uint32_t m_ctx_weapon_handle{};
			std::chrono::steady_clock::time_point m_ctx_published_at{};
			std::array<context, 128> m_ctx_history{};
			std::size_t m_ctx_history_head{};
			std::size_t m_ctx_history_count{};
			penetration m_pen{};
			mutable std::shared_mutex m_ctx_mutex{};
		};

	inline ballistics_t& ballistics( ) { static ballistics_t value{}; return value; }

}
