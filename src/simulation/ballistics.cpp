#include <stdafx.hpp>
#include <simulation/ballistics.hpp>
#include <simulation/spread_sampler.hpp>
#include <simulation/accuracy_snapshot.hpp>
#include <core/memory/compatibility.hpp>
#include <core/math/ray_capsule.hpp>

namespace simulation {

	namespace detail {

		template <std::size_t capacity>
		class remote_field_snapshot
		{
		public:
			remote_field_snapshot( std::uintptr_t object, std::int32_t first,
				std::int32_t end ) noexcept
				: m_object( object ), m_first( first )
			{
				if ( !object || first < 0 || end <= first )
				{
					return;
				}

				m_size = static_cast<std::size_t>( end - first );
				if ( m_size > m_bytes.size( ) )
				{
					m_size = 0;
					return;
				}

				m_valid = app::context().process.copy(
					object + static_cast<std::uintptr_t>( first ),
					m_bytes.data( ), m_size );
			}

			template <typename value_t>
			[[nodiscard]] value_t load( std::int32_t offset ) const noexcept
			{
				if ( m_valid && offset >= m_first )
				{
					const auto relative = static_cast<std::size_t>( offset - m_first );
					if ( relative <= m_size && sizeof( value_t ) <= m_size - relative )
					{
						value_t value{};
						std::memcpy( &value, m_bytes.data( ) + relative, sizeof( value ) );
						return value;
					}
				}

				return {};
			}

			[[nodiscard]] bool valid( ) const noexcept { return m_valid; }

		private:
			std::array<std::byte, capacity> m_bytes{};
			std::uintptr_t m_object{};
			std::int32_t m_first{};
			std::size_t m_size{};
			bool m_valid{};
		};

		inline static float remap_value( float val, float a, float b, float c, float d )
		{
			if ( b == a )
			{
				return ( val - b >= 0.0f ) ? d : c;
			}

			const auto t = std::clamp( ( val - a ) / ( b - a ), 0.0f, 1.0f );
			return c + ( d - c ) * t;
		}

        [[nodiscard]] static std::optional<shotgun_pattern> read_shotgun_pattern(int item)
        {
            const auto map = game::compatibility::spread_patterns();
            if (!map) return std::nullopt;
            std::array<std::byte, 32> header{};
            if (!app::context().process.copy(map, header.data(), header.size())) return std::nullopt;
            int size{}, capacity{}, allocated{};
            std::uintptr_t nodes{};
            std::memcpy(&size, header.data()+8, 4);
            std::memcpy(&capacity, header.data()+12, 4);
            std::memcpy(&nodes, header.data()+16, 8);
            std::memcpy(&allocated, header.data()+28, 4);
            capacity &= 0x7fffffff;
            if (size <= 0 || capacity < size || capacity > 4096 || allocated <= 0
                || allocated > capacity || nodes < 0x10000 || nodes > 0x00007fffffffffffULL)
                return std::nullopt;
            // One snapshot replaces a process read for every tree node and every pellet.
            static thread_local std::vector<std::byte> entries;
            entries.resize(static_cast<std::size_t>(allocated) * 32);
            if (!app::context().process.copy(nodes, entries.data(), entries.size())) return std::nullopt;
            for (int i=0; i<allocated; ++i) {
                std::uint16_t key{};
                std::uintptr_t definition{};
                std::memcpy(&key, entries.data()+i*32+16, 2);
                if (key != item) continue;
                std::memcpy(&definition, entries.data()+i*32+24, 8);
                if (definition < 0x10000 || definition > 0x00007fffffffffffULL) return std::nullopt;
                std::array<std::byte, 0x201> data{};
                if (!app::context().process.copy(definition+0x404, data.data(), data.size()))
                    return std::nullopt;
                shotgun_pattern result{};
                static_assert(sizeof(result.angle_radius)==0x200);
                std::memcpy(result.angle_radius.data(), data.data(), 0x200);
                result.random = data[0x200] != std::byte{};
                return result;
            }
            return std::nullopt;
        }

	} // namespace detail

	ballistics_t::context ballistics_t::shot_ctx( float fire_time, std::uintptr_t weapon,
		float post_shot_time ) const
	{
		if ( !std::isfinite( fire_time ) || fire_time <= 0.0f || !weapon )
		{
			return {};
		}

		std::shared_lock lock( this->m_ctx_mutex );
		context best{};
		auto best_time = -std::numeric_limits<float>::infinity( );
		for ( std::size_t index = 0; index < this->m_ctx_history_count; ++index )
		{
			const auto slot = ( this->m_ctx_history_head +
				this->m_ctx_history.size( ) - 1 - index ) %
				this->m_ctx_history.size( );
			const auto& sample = this->m_ctx_history[ slot ];
			if ( !sample.valid || sample.weapon != weapon ||
				!std::isfinite( sample.current_time ) ||
				sample.current_time > fire_time + 0.020f )
			{
				continue;
			}

			if ( !std::isfinite( sample.last_fired_weapon_time ) ||
				sample.last_fired_weapon_time >= fire_time - 0.0001f )
			{
				continue;
			}

			if ( std::isfinite( post_shot_time ) &&
				std::isfinite( sample.last_shot_time ) &&
				sample.last_shot_time >= post_shot_time - 0.0001f )
			{
				continue;
			}

			if ( sample.current_time > best_time )
			{
				best = sample;
				best_time = sample.current_time;
			}
		}

		return best;
	}

	void ballistics_t::tick( )
	{
		context ctx{};
		const auto local_pawn = game::local_player().pawn( );
		const auto now = std::chrono::steady_clock::now( );
		const auto invalidate = [ this ]( )
		{
			std::unique_lock lock( this->m_ctx_mutex );
			this->m_ctx = {};
			this->m_ctx_pawn = {};
			this->m_ctx_weapon = {};
			this->m_ctx_weapon_handle = {};
			this->m_ctx_published_at = {};
		};
		const auto reject_transient = [ this, local_pawn, now ](
			const std::uintptr_t observed_weapon = 0,
			const std::uint32_t observed_handle = 0 )
		{
			std::unique_lock lock( this->m_ctx_mutex );
			const auto same_identity = this->m_ctx.valid
				&& this->m_ctx_pawn == local_pawn
				&& ( !observed_weapon || this->m_ctx_weapon == observed_weapon )
				&& ( !observed_handle
					|| this->m_ctx_weapon_handle == observed_handle );
			const auto recent = this->m_ctx_published_at.time_since_epoch().count( ) != 0
				&& now - this->m_ctx_published_at <= std::chrono::milliseconds( 100 );
			if ( same_identity && recent ) return;
			this->m_ctx = {};
			this->m_ctx_pawn = {};
			this->m_ctx_weapon = {};
			this->m_ctx_weapon_handle = {};
			this->m_ctx_published_at = {};
		};

		if ( !local_pawn )
		{
			invalidate( );
			return;
		}

		std::uintptr_t weapon_services{};
		if ( !app::context().process.copy( local_pawn
				+ SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_id ),
				&weapon_services, sizeof( weapon_services ) ) || !weapon_services )
		{
			reject_transient( );
			return;
		}

		std::uint32_t weapon_handle{};
		if ( !app::context().process.copy( weapon_services
				+ SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_id ),
				&weapon_handle, sizeof( weapon_handle ) ) || !weapon_handle )
		{
			reject_transient( );
			return;
		}

		ctx.weapon = game::entity_index().lookup( weapon_handle );
		if ( !ctx.weapon )
		{
			reject_transient( 0, weapon_handle );
			return;
		}
		{
			std::unique_lock lock( this->m_ctx_mutex );
			if ( this->m_ctx.valid && ( this->m_ctx_pawn != local_pawn
				|| this->m_ctx_weapon != ctx.weapon
				|| this->m_ctx_weapon_handle != weapon_handle ) )
			{
				this->m_ctx = {};
				this->m_ctx_pawn = {};
				this->m_ctx_weapon = {};
				this->m_ctx_weapon_handle = {};
				this->m_ctx_published_at = {};
			}
		}

		if ( !app::context().process.copy( ctx.weapon
				+ SCHEMA( "C_BaseEntity", "m_nSubclassID"_id ) + 0x8,
				&ctx.weapon_vdata, sizeof( ctx.weapon_vdata ) ) || !ctx.weapon_vdata )
		{
			reject_transient( ctx.weapon, weapon_handle );
			return;
		}

		bool reads_valid{ true };
		const auto read_required = [ & ]( const std::uintptr_t address, auto& value )
		{
			if ( !app::context().process.copy( address, &value, sizeof( value ) ) )
			{
				value = {};
				reads_valid = false;
			}
		};

		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_id ), ctx.weapon_type );
		read_required(
			ctx.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_id )
			+ SCHEMA( "C_AttributeContainer", "m_Item"_id )
			+ SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_id ), ctx.item_def_idx );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_id ), ctx.num_bullets );
        if (ctx.num_bullets>1) {
            const auto offset=SCHEMA("CCSWeaponBaseVData","m_nSpreadSeed"_id);
            if(offset<=0) reads_valid=false;
            else read_required(ctx.weapon_vdata+offset,ctx.pattern_seed);
        }
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_weaponMode"_id ), ctx.fire_mode );

		std::uintptr_t global_vars{};
		read_required( app::context().addresses.global_vars, global_vars );
		float sample_time{}, sample_fraction{};
		int sample_tick{};
		if ( global_vars )
		{
			read_required( global_vars + 0x30, sample_time );
			read_required( global_vars + 0x44, sample_tick );
			read_required( global_vars + 0x50, sample_fraction );
		}
		else reads_valid = false;

		foundation::vec3 view_angles{}, unused_origin{};
		if ( !game::camera().sample( unused_origin, view_angles ) ) reads_valid = false;
		if ( !reads_valid )
		{
			reject_transient( ctx.weapon, weapon_handle );
			return;
		}
		ctx.inaccuracy = this->get_inaccuracy(
			local_pawn, ctx.weapon, ctx.weapon_vdata, ctx.weapon_type,
			view_angles, ctx.debug );
		ctx.fire_mode = ctx.debug.fire_mode;
		ctx.spread = this->get_spread( ctx.weapon_vdata, ctx.fire_mode );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_id ), ctx.recoil_index );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_bInReload"_id ), ctx.is_reloading );

		ctx.current_time = sample_time;
		// CGlobalVarsBase::tickcount and the interpolation fraction used by
		// BacktrackLocalPlayer's firing timestamp (client+0xA56230).
		ctx.global_tick = sample_tick;
		ctx.global_tick_fraction = sample_fraction;

		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_id ), ctx.cycle_time );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_id ), ctx.last_shot_time );
		read_required( local_pawn
			+ SCHEMA( "C_CSPlayerPawn", "m_flLastFiredWeaponTime"_id ), ctx.last_fired_weapon_time );
		read_required( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_bIsFullAuto"_id ), ctx.is_full_auto );
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_id ), ctx.next_primary_attack_tick );
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_id ), ctx.next_primary_attack_ratio );
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_id ), ctx.next_secondary_attack_tick );
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_flNextSecondaryAttackTickRatio"_id ), ctx.next_secondary_attack_ratio );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flWatTickOffset"_id ), ctx.wat_tick_offset );

		// Pre-shot player state for accurate inaccuracy recalculation
		read_required( local_pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ), ctx.velocity );
		ctx.velocity_length = ctx.velocity.length_2d( );
		read_required( local_pawn
			+ SCHEMA( "C_BaseEntity", "m_hGroundEntity"_id ), ctx.ground_entity );
		ctx.on_ground = ctx.ground_entity != 0xffffffffu;
		read_required( local_pawn
			+ SCHEMA( "C_CSPlayerPawn", "m_bIsWalking"_id ), ctx.is_walking );

		const auto controller = game::local_player().controller( );
		std::uint32_t current_tick_raw{};
		if ( controller ) read_required( controller
			+ SCHEMA( "CBasePlayerController", "m_nTickBase"_id ), current_tick_raw );
		else reads_valid = false;
		const auto current_tick = static_cast<int>( current_tick_raw );
		ctx.player_tick = current_tick;
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_nPostponeFireReadyTicks"_id ), ctx.postpone_fire_ready_tick );
		read_required( ctx.weapon
			+ SCHEMA( "C_CSWeaponBase", "m_flPostponeFireReadyFrac"_id ), ctx.postpone_fire_ready_fraction );
		float next_player_attack{};
		read_required( weapon_services
			+ SCHEMA( "CCSPlayer_WeaponServices", "m_flNextAttack"_id ), next_player_attack );
		int clip{};
		read_required( ctx.weapon
			+ SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_id ), clip );
		ctx.clip = clip;
		const auto primary_ready = ctx.next_primary_attack_tick < current_tick
			|| ( ctx.next_primary_attack_tick == current_tick && ctx.next_primary_attack_ratio <= 0.001f );

		ctx.weapon_ready = !ctx.is_reloading
			&& clip != 0
			&& primary_ready
			&& next_player_attack <= ctx.current_time;
		if ( ctx.weapon_type == game::rules::precision )
			read_required( local_pawn
				+ SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_id ), ctx.is_scoped );

		float final_time{};
		int final_tick{};
		read_required( global_vars + 0x30, final_time );
		read_required( global_vars + 0x44, final_tick );
		if ( !reads_valid || final_time != sample_time || final_tick != sample_tick )
		{
			reject_transient( ctx.weapon, weapon_handle );
			return;
		}

		this->m_pen.prepare( ctx.weapon_vdata, ctx.weapon );
		const auto& weapon_data = this->m_pen.get_weapon_data( );
		ctx.valid = std::isfinite( ctx.inaccuracy ) && ctx.inaccuracy >= 0.0f
			&& std::isfinite( ctx.spread ) && ctx.spread >= 0.0f
			&& std::isfinite( ctx.recoil_index ) && ctx.recoil_index >= 0.0f
			&& std::isfinite( ctx.current_time ) && ctx.current_time > 0.0f
			&& ctx.global_tick > 0 && ctx.player_tick > 0
			&& ctx.item_def_idx > 0 && ctx.num_bullets > 0
			&& ctx.num_bullets <= 32 && std::isfinite( ctx.cycle_time )
			&& ctx.cycle_time > 0.0f && std::isfinite( ctx.velocity.x )
			&& std::isfinite( ctx.velocity.y ) && std::isfinite( ctx.velocity.z )
			&& weapon_data.damage > 0.0f && weapon_data.penetration > 0.0f
			&& weapon_data.range > 0.0f && weapon_data.range_modifier > 0.0f
			&& weapon_data.range_modifier <= 1.0f;
		if ( !ctx.valid )
		{
			reject_transient( ctx.weapon, weapon_handle );
			return;
		}

		std::unique_lock lock( this->m_ctx_mutex );
		this->m_ctx = ctx;
		this->m_ctx_pawn = local_pawn;
		this->m_ctx_weapon = ctx.weapon;
		this->m_ctx_weapon_handle = weapon_handle;
		this->m_ctx_published_at = now;
		this->m_ctx_history[ this->m_ctx_history_head ] = ctx;
		this->m_ctx_history_head =
			( this->m_ctx_history_head + 1 ) % this->m_ctx_history.size( );
		this->m_ctx_history_count = std::min(
			this->m_ctx_history_count + 1, this->m_ctx_history.size( ) );
	}


	float ballistics_t::estimate_hit_probability( const foundation::vec3& eye_pos, const foundation::vec3& aim_angle, const game::player_snapshot& target, const game::skeleton_reader::data& bones, int hitbox_parts ) const
	{
		const auto& ctx = this->m_ctx;
		const auto total_spread = ctx.spread + ctx.inaccuracy;

		if ( total_spread < 0.0001f )
		{
			return 1.0f;
		}

		const auto range = this->m_pen.get_weapon_data( ).range;
		if ( range <= 0.0f )
		{
			return 0.0f;
		}

		struct capsule_t
		{
			foundation::vec3 start;
			foundation::vec3 end;
			float radius;
		};

		std::array<capsule_t, 32> capsules;
		auto capsule_count{ 0 };

		for ( const auto& hb : target.hitboxes )
		{
			if ( hb.index < 0 || hb.bone < 0 )
			{
				continue;
			}
			const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox( hb.index );
			const auto part = hb.index == 0 ? config::combat_profile::aim_part::head :
				( hb.index == 1 || hitgroup == 1 || hitgroup == 2 || hitgroup == 3
					? config::combat_profile::aim_part::body
					: ( hitgroup == 4 || hitgroup == 5 ? config::combat_profile::aim_part::arms
						: ( hitgroup == 6 || hitgroup == 7 ? config::combat_profile::aim_part::legs
							: config::combat_profile::aim_part::body ) ) );
			if ( !( hitbox_parts & part ) || capsule_count >= static_cast<int>( capsules.size( ) ) )
			{
				continue;
			}

			const auto& bone = bones.bones[ hb.bone ];

			// mins/maxs — две точки оси капсулы в пространстве кости
			const auto capsule_start = bone.position + bone.rotation.apply( hb.mins );
			const auto capsule_end = bone.position + bone.rotation.apply( hb.maxs );

			capsules[ capsule_count++ ] = { capsule_start, capsule_end, hb.radius };
		}

		if ( capsule_count == 0 )
		{
			return 0.0f;
		}

		foundation::vec3 forward{}, right{}, up{};
		aim_angle.to_directions( &forward, &right, &up );

		constexpr auto samples{ 256 };
		auto hits{ 0 };

		for ( int seed = 0; seed < samples; ++seed )
		{
			const auto spread = this->sample_spread_offset( seed, ctx.inaccuracy, ctx.spread,
				ctx.recoil_index, ctx.item_def_idx, ctx.fire_mode, ctx.num_bullets, 0, ctx.pattern_seed );
			// See the note in legit.cpp: the horizontal term is added, not subtracted.
			// Verified against the client's own impact queue, not from a decompile.
			const auto direction = ( forward + right * spread.x + up * spread.y ).normalized( );

			for ( int i = 0; i < capsule_count; ++i )
			{
				if ( this->ray_hits_capsule( eye_pos, direction, capsules[ i ].start, capsules[ i ].end, capsules[ i ].radius ) )
				{
					++hits;
					break;
				}
			}

			const auto remaining = samples - ( seed + 1 );
			if ( hits + remaining < samples / 4 )
			{
				break;
			}
		}

		return static_cast< float >( hits ) / static_cast< float >( samples );
	}

    std::uint32_t ballistics_t::derive_command_seed(const foundation::vec3& angles,int tick) const
    {
        return shot_model::seed(angles,tick);
    }

    foundation::vec2 ballistics_t::sample_spread_offset(int seed, float inaccuracy, float spread,
        float recoil_index, int item_def_idx, int weapon_mode, int num_bullets, int bullet_index, std::optional<int> pattern_seed) const
    {
        detail::spread_parameters parameters{seed, inaccuracy, spread, recoil_index,
            item_def_idx, weapon_mode, num_bullets};
        parameters.patterns = game::variables().get<bool>(CONVAR("weapon_accuracy_shotgun_spread_patterns"_id));
        parameters.force_radius = game::variables().get<bool>(CONVAR("weapon_debug_max_inaccuracy"_id));
        parameters.force_angle = game::variables().get<bool>(CONVAR("weapon_debug_inaccuracy_only_up"_id));
        std::optional<detail::shotgun_pattern> pattern;
        if (parameters.patterns && num_bullets>1) {
            if(pattern_seed) {
                struct cached_pattern { int seed{},bullets{}; detail::shotgun_pattern value{}; bool valid{}; };
                static thread_local cached_pattern cached;
                if(!cached.valid || cached.seed!=*pattern_seed || cached.bullets!=num_bullets)
                    cached={*pattern_seed,num_bullets,detail::make_shotgun_pattern(*pattern_seed,num_bullets),true};
                pattern=cached.value;
            } else pattern=detail::read_shotgun_pattern(item_def_idx);
        }
        const auto lookup = [&](int, int, float recoil, int bullets, int pellet, foundation::vec2& polar) {
            if (!pattern) return detail::pattern_result::unavailable;
            return pattern->sample(recoil, bullets, pellet, polar);
        };
        foundation::vec2 result{};
        if (!detail::sample_spread(parameters, bullet_index, lookup, result)) {
            const auto invalid = std::numeric_limits<float>::quiet_NaN();
            return {invalid, invalid};
        }
        return result;
    }

    foundation::vec2 ballistics_t::sample_predicted_spread(int seed, float inaccuracy,
        float spread, float recoil_index, int item_def_idx, int weapon_mode,
        int bullet_index, int num_bullets, std::optional<int> pattern_seed) const
    {
        return sample_spread_offset(seed, inaccuracy, spread, recoil_index,
            item_def_idx, weapon_mode, num_bullets, bullet_index, pattern_seed);
    }

	foundation::vec3 ballistics_t::predict_counter_movement_origin( const foundation::vec3& pos ) const
	{
		const auto pawn = game::local_player().pawn( );
		if ( !pawn ) return pos;

		const auto movement_services = app::context().process.load<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_id ) );
		if ( !movement_services ) return pos;

		const auto flags = app::context().process.load<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
		if ( ( flags & 1u ) == 0 ) return pos;

		auto velocity = app::context().process.load<foundation::vec3>( pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		velocity.z = 0.0f;

		if ( velocity.length_2d( ) <= 1.0f ) return pos;

		const auto ground_grip = app::context().process.load<float>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_id ) );
		const auto max_speed = app::context().process.load<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_id ) );
		const auto pawn_friction = app::context().process.load<float>( pawn + SCHEMA( "C_BaseEntity", "m_flFriction"_id ) );
		const auto current_time = this->m_ctx.current_time;
		const auto stashed_until = app::context().process.load<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flUseFrictionStashedSpeedUntilFrac"_id ) );
		const auto cached_speed = app::context().process.load<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flFrictionStashedSpeed"_id ) );

		const auto friction_rate = game::variables().get<float>( CONVAR( "sv_friction"_id ) );
		const auto minimum_control_speed = game::variables().get<float>( CONVAR( "sv_stopspeed"_id ) );
		const auto acceleration_rate = game::variables().get<float>( CONVAR( "sv_accelerate"_id ) );
		bool weapon_limited{};
        if (!game::variables().try_get(CONVAR("sv_accelerate_use_weapon_speed"_id),weapon_limited)) return pos;
		const auto water_scale = game::variables().get<float>( CONVAR( "sv_water_slow_amount"_id ) );

		const auto buttons = app::context().process.load<std::uintptr_t>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_nButtons"_id ) );
		const auto ducking_state = app::context().process.load<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bDucking"_id ) );

		const auto crouched = ( flags & 2 ) || ducking_state || ( buttons & static_cast< std::uintptr_t >( game::rules::duck ) );
		const auto sprinting = !crouched && ( buttons & static_cast< std::uintptr_t >( game::rules::sprint ) );

		const auto water_level = app::context().process.load<float>( pawn + SCHEMA( "C_BaseEntity", "m_flWaterLevel"_id ) );
		const auto submerged = static_cast< std::uint32_t >( water_level * 4.0f + 1.0f ) >= 2;

		auto weapon_ratio{ 1.0f };
		auto zoom_limited{ false };

		if ( weapon_limited && this->m_ctx.weapon_vdata )
		{
			const auto wpn_speed = app::context().process.load<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_id ) );
			weapon_ratio = std::fminf( 1.0f, wpn_speed / 250.0f );

			if ( this->m_ctx.weapon )
			{
				const auto zoom = app::context().process.load<std::int32_t>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBaseGun", "m_zoomLevel"_id ) );
				const auto zoom_count = app::context().process.load<std::int32_t>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nZoomLevels"_id ) );

				zoom_limited = zoom > 0 && zoom_count > 1 && ( wpn_speed * 0.52f ) < 110.0f;
			}
		}

		const auto apply_friction = [ & ]( foundation::vec3& vel, bool first_tick )
			{
				const auto spd = first_tick && current_time <= stashed_until ? cached_speed : vel.length( );
				if ( spd < 0.1f )
				{
					return;
				}

				const auto control = std::fmaxf( spd, minimum_control_speed );
				const auto drop = control * friction_rate * ground_grip * pawn_friction * game::rules::simulation_step;
				const auto adjusted = std::fmaxf( spd - drop, 0.0f );

				if ( adjusted < spd )
				{
					vel *= adjusted / spd;
				}
			};

		const auto counter_acceleration = [ & ]( const foundation::vec3& vel, const foundation::vec3& dir, float wish_spd ) -> float
			{
				const auto base = std::fmaxf( 250.0f, wish_spd );
				auto factor{ 1.0f };
				auto cap = base;

				if ( weapon_limited )
				{
					cap = base * weapon_ratio;

					if ( ( !crouched && !sprinting ) || zoom_limited )
					{
						factor = weapon_ratio;
					}
				}

				auto accel_base = base;

				if ( submerged )
				{
					cap *= water_scale;
					accel_base = sprinting ? base : base * water_scale;
				}

				if ( crouched )
				{
					cap *= 0.34f;
					factor = std::fminf( 0.34f, factor );
				}

				auto final_cap = accel_base * factor;
				auto accel = acceleration_rate;

				if ( sprinting && !zoom_limited )
				{
					final_cap *= 0.52f;

					const auto threshold = cap * 0.52f - 5.0f;
					const auto proj = std::fmaxf( 0.0f, vel.dot( dir ) );

					if ( proj > threshold )
					{
						const auto blend = ( proj - threshold ) / std::fmaxf( 0.001f, cap * 0.52f - threshold );
						accel *= std::fmaxf( 0.0f, 1.0f - std::fminf( 1.0f, blend ) );
					}
				}

				const auto gain = accel * game::rules::simulation_step * final_cap * ground_grip;
				const auto current_proj = vel.dot( dir );

				return std::fminf( gain, std::fmaxf( 0.0f, -current_proj ) );
			};

		const auto completion_speed = max_speed * 0.34f;
		auto predicted_origin = pos;
		auto predicted_velocity = velocity;

		for ( auto step = 0; step < 20 && predicted_velocity.length_2d( ) > completion_speed; ++step )
		{
			apply_friction( predicted_velocity, step == 0 );
			if ( predicted_velocity.length_2d( ) <= completion_speed ) break;

			auto counter_direction = foundation::vec3{ -predicted_velocity.x, -predicted_velocity.y, 0.0f };
			const auto desired_speed = counter_direction.length( );

			if ( desired_speed > 0.0001f ) counter_direction *= 1.0f / desired_speed;

			const auto counter_force = counter_acceleration( predicted_velocity, counter_direction, desired_speed );

			predicted_velocity.x += counter_direction.x * counter_force;
			predicted_velocity.y += counter_direction.y * counter_force;

			predicted_origin.x += predicted_velocity.x * game::rules::simulation_step;
			predicted_origin.y += predicted_velocity.y * game::rules::simulation_step;
		}

		return predicted_origin;
	}

	bool ballistics_t::precision_ready( ) const
	{
		if ( this->m_ctx.weapon_type != game::rules::precision ) return true;
		const auto pawn = game::local_player().pawn( );
		if ( !pawn ) return false;
		const auto flags = app::context().process.load<std::uint32_t>( pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
		if ( ( flags & 1u ) == 0 ) return true;
		const auto camera_services = app::context().process.load<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pCameraServices"_id ) );
		if ( !camera_services ) return false;

		static constexpr auto limits = std::to_array<std::pair<int, float>>( {
			{ 9, 0.0005f }, { 11, 0.0012f }, { 38, 0.0012f }, { 40, 0.00089f }
		} );
		const auto match = std::ranges::find( limits, this->m_ctx.item_def_idx,
			[]( const auto& entry ) { return entry.first; } );
		const auto limit = match == limits.end( ) ? 0.00089f : match->second;
		const auto observed = app::context().process.load<float>( camera_services
			+ SCHEMA( "CCSPlayer_CameraServices", "m_vClientScopeInaccuracy"_id ) );
		return ( observed <= 1e-6f ? 0.0f : observed ) <= limit;
	}

	float ballistics_t::command_lead_time( ) const
	{
		const auto pawn = game::local_player().pawn( );
		const auto controller = game::local_player().controller( );
		if ( !pawn || !controller ) return 0.0f;
		const auto ping = app::context().process.load<std::int32_t>( controller + SCHEMA( "CCSPlayerController", "m_iPing"_id ) );
		const auto interpolation = app::context().process.load<float>( pawn + 0x290 );
		return std::fma( static_cast<float>( ping ), 0.0005f, interpolation );
	}

	float ballistics_t::get_spread( std::uintptr_t weapon_vdata,
		const int fire_mode ) const
	{

		std::pair<float, float> values{};
		if ( !app::context().process.copy( weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flSpread"_id ),
			&values, sizeof( values ) ) )
			return std::numeric_limits<float>::quiet_NaN( );
		return fire_mode == 1 ? values.second : values.first;
	}

	float ballistics_t::get_inaccuracy( std::uintptr_t pawn, std::uintptr_t weapon,
		std::uintptr_t weapon_vdata, std::uint32_t weapon_type,
		const foundation::vec3& eye_angles, inaccuracy_debug_data& dbg ) const
	{
		float forcespread{};
        if (!game::variables().try_get(CONVAR("weapon_accuracy_forcespread"_id), forcespread))
            return std::numeric_limits<float>::quiet_NaN();
		if ( forcespread > 0.0f )
		{
			return std::fminf( forcespread, 1.0f );
		}

		const auto nospread = game::variables().get<bool>( CONVAR( "weapon_accuracy_nospread"_id ) );
		if ( nospread )
		{
			return 0.0f;
		}

		const auto vdata_first = SCHEMA( "CCSWeaponBaseVData", "m_bIsRevolver"_id );
		const auto vdata_end = SCHEMA( "CCSWeaponBaseVData", "m_nRecoveryTransitionEndBullet"_id )
			+ static_cast<std::int32_t>( sizeof( int ) );
		const detail::remote_field_snapshot<512> vdata_snapshot(
			weapon_vdata, vdata_first, vdata_end );
		if ( !vdata_snapshot.valid( ) )
			return std::numeric_limits<float>::quiet_NaN( );
		dbg.inaccuracy_crouch = vdata_snapshot.load<std::pair<float, float>>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyCrouch"_id ) );
		dbg.inaccuracy_stand = vdata_snapshot.load<std::pair<float, float>>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyStand"_id ) );
		dbg.inaccuracy_ladder = vdata_snapshot.load<std::pair<float, float>>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyLadder"_id ) );
		dbg.inaccuracy_move = vdata_snapshot.load<std::pair<float, float>>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_id ) );
		dbg.max_speed = vdata_snapshot.load<std::pair<float, float>>(
			SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_id ) );
		dbg.inaccuracy_jump_initial = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_id ) );
		dbg.inaccuracy_jump_apex = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_id ) );
		dbg.recovery_time_crouch = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flRecoveryTimeCrouch"_id ) );
		dbg.recovery_time_stand = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flRecoveryTimeStand"_id ) );
		dbg.recovery_time_crouch_final = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flRecoveryTimeCrouchFinal"_id ) );
		dbg.recovery_time_stand_final = vdata_snapshot.load<float>(
			SCHEMA( "CCSWeaponBaseVData", "m_flRecoveryTimeStandFinal"_id ) );
		dbg.recovery_transition_start = vdata_snapshot.load<int>(
			SCHEMA( "CCSWeaponBaseVData", "m_nRecoveryTransitionStartBullet"_id ) );
		dbg.recovery_transition_end = vdata_snapshot.load<int>(
			SCHEMA( "CCSWeaponBaseVData", "m_nRecoveryTransitionEndBullet"_id ) );
		dbg.num_bullets = vdata_snapshot.load<int>(
			SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_id ) );
		dbg.is_revolver = vdata_snapshot.load<bool>(
			SCHEMA( "CCSWeaponBaseVData", "m_bIsRevolver"_id ) );

		const auto weapon_first = SCHEMA( "C_CSWeaponBase", "m_weaponMode"_id );
		const auto weapon_end = SCHEMA( "C_CSWeaponBase", "m_iRecoilIndex"_id )
			+ static_cast<std::int32_t>( sizeof( int ) );
		const detail::remote_field_snapshot<128> weapon_snapshot(
			weapon, weapon_first, weapon_end );
		if ( !weapon_snapshot.valid( ) )
			return std::numeric_limits<float>::quiet_NaN( );
		dbg.fire_mode = weapon_snapshot.load<int>(
			SCHEMA( "C_CSWeaponBase", "m_weaponMode"_id ) );
		dbg.turning_inaccuracy = weapon_snapshot.load<float>(
			SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracy"_id ) );
		dbg.accuracy_penalty = weapon_snapshot.load<float>(
			SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_id ) );
		dbg.recoil_index = weapon_snapshot.load<int>(
			SCHEMA( "C_CSWeaponBase", "m_iRecoilIndex"_id ) );

		const auto pawn_first = SCHEMA( "C_BaseEntity", "m_fFlags"_id );
		const auto pawn_end = SCHEMA( "C_BaseEntity", "m_hGroundEntity"_id )
			+ static_cast<std::int32_t>( sizeof( std::uint32_t ) );
		const detail::remote_field_snapshot<512> pawn_snapshot(
			pawn, pawn_first, pawn_end );
		if ( !pawn_snapshot.valid( ) )
			return std::numeric_limits<float>::quiet_NaN( );
		const auto flags = pawn_snapshot.load<std::uint32_t>(
			SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
		dbg.velocity = pawn_snapshot.load<foundation::vec3>(
			SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		dbg.speed = dbg.velocity.length_2d( );
		dbg.move_type = pawn_snapshot.load<std::uint8_t>(
			SCHEMA( "C_BaseEntity", "m_MoveType"_id ) );
		if ( !app::context().process.copy( pawn
			+ SCHEMA( "C_CSPlayerPawn", "m_bIsWalking"_id ),
			&dbg.is_walking, sizeof( dbg.is_walking ) ) )
			return std::numeric_limits<float>::quiet_NaN( );
		const auto ground_handle = pawn_snapshot.load<std::uint32_t>(
			SCHEMA( "C_BaseEntity", "m_hGroundEntity"_id ) );
		dbg.on_ground = ground_handle != 0xffffffffu;
		dbg.crouching = ( flags & 2 ) != 0;

        (void)weapon_type;(void)eye_angles;
        if(!app::context().process.copy(pawn+SCHEMA("C_BasePlayerPawn","v_angle"_id),
            &dbg.eye_angles,sizeof(dbg.eye_angles)))
            return std::numeric_limits<float>::quiet_NaN();
        dbg.on_ground=ground_handle!=0xffffffffu && ground_handle!=0xfffffffeu
            && game::entity_index().lookup(ground_handle)!=0;
        return detail::accuracy_from_snapshot(dbg);
    }

	[[nodiscard]] float ballistics_t::get_inaccuracy_preshot(
		std::uintptr_t weapon, std::uintptr_t weapon_vdata, std::uint32_t weapon_type,
		const foundation::vec3& preshot_velocity, bool preshot_on_ground,
		float preshot_accuracy_penalty, int preshot_recoil_index,
		bool preshot_is_walking,
		inaccuracy_debug_data& dbg ) const
	{
        (void)weapon;(void)weapon_vdata;(void)weapon_type;
        dbg.velocity=preshot_velocity;dbg.speed=preshot_velocity.length_2d();
        dbg.on_ground=preshot_on_ground;dbg.accuracy_penalty=preshot_accuracy_penalty;
        dbg.recoil_index=preshot_recoil_index;dbg.is_walking=preshot_is_walking;
        return detail::accuracy_from_snapshot(dbg);
    }

    bool ballistics_t::ray_hits_capsule(const foundation::vec3& origin, const foundation::vec3& ray,
        const foundation::vec3& start, const foundation::vec3& end, float radius) const
    {
        float distance{};
        return foundation::ray_capsule_entry(origin, ray, start, end, radius, distance);
    }

} // namespace simulation
