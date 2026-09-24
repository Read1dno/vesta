#include <stdafx.hpp>
#include <features/aimbot/aimbot.hpp>
#include <simulation/grenade.hpp>
namespace features::aimbot {
using simulation::ballistics;
void grenade_aim_t::tick( )
{
	this->refresh_runtime_config( );
	const auto& ctx = ballistics().ctx( );
	const auto cfg = this->runtime_config()->combat.global.grenade_aim;

	if ( !cfg.enabled || !( GetAsyncKeyState( cfg.key ) & 0x8000 ) )
	{
		this->m_is_active = false;
		this->m_aim_error = {};
		this->m_current_target = {};
		return;
	}

	if ( !ctx.valid || !ctx.weapon || !game::collision().valid( )
		|| ctx.weapon_type != game::rules::equipment_class::throwable )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	const auto pin_pulled = app::context().process.load<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_id ) );
	const auto throw_time = app::context().process.load<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_id ) );

	if ( !pin_pulled || throw_time > 0.0f )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	const auto kind = this->resolve_grenade_kind( ctx.weapon_vdata );
	if ( kind == grenade_kind::unknown )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}

	float throw_vel_vdata{}, throw_strength{};
	if ( !app::context().process.copy( ctx.weapon_vdata
			+ SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_id ),
			&throw_vel_vdata, sizeof( throw_vel_vdata ) )
		|| !app::context().process.copy( ctx.weapon
			+ SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_id ),
			&throw_strength, sizeof( throw_strength ) )
		|| !std::isfinite( throw_vel_vdata ) || throw_vel_vdata <= 0.0f
		|| !std::isfinite( throw_strength ) || throw_strength < 0.0f
		|| throw_strength > 1.0f )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	if ( std::fabsf( throw_strength - 0.5f ) <= 0.1f ) throw_strength = 0.5f;

	foundation::vec3 eye_pos{}, view_angles{};
	if ( !game::camera().sample( eye_pos, view_angles ) )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	const auto players = game::world().players( );
	if ( !players )
	{
		this->m_is_active = false;
		this->m_current_target = {};
		return;
	}
	const auto previous_pawn = this->m_current_target.pawn;
	this->find_target( eye_pos, view_angles, *players, static_cast<float>( cfg.fov ) );
	if ( !this->m_current_target.pawn )
	{
		this->m_is_active = false;
		return;
	}

	this->m_is_active = true;
	const auto now = std::chrono::steady_clock::now( );
	const auto state_changed = previous_pawn != this->m_current_target.pawn ||
		ctx.weapon != this->m_last_weapon || kind != this->m_last_kind;
	if ( state_changed || now - this->m_last_calculation >= std::chrono::milliseconds( 24 ) )
	{
		this->m_last_calculation = now;
		this->m_last_weapon = ctx.weapon;
		this->m_last_kind = kind;
		this->calculate_trajectory( eye_pos, throw_vel_vdata, throw_strength, kind );
	}

	if ( this->m_current_target.found_trajectory )
	{
		this->smooth_aim( eye_pos, view_angles, cfg );
	}
}

void grenade_aim_t::find_target( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const std::vector<game::player_snapshot>& players, float max_fov )
{
	const auto previous = this->m_current_target;
	target_info selected{};
	float best_fov = max_fov;

	for ( const auto& player : players )
	{
		if ( !game::local_player().is_enemy( player.team ) || player.health <= 0 || player.invulnerable )
		{
			continue;
		}

		if ( !player.bones.is_valid( ) ) continue;
		const auto head = player.bones.get_position( game::rules::joint_id::head );
		const auto fov = foundation::angular_distance( view_angles, eye_pos, head );
		if ( fov < best_fov )
		{
			best_fov = fov;
			selected.player = player;
			selected.pawn = player.pawn;
			selected.head_pos = head;
		}
	}

	if ( selected.pawn && selected.pawn == previous.pawn )
	{
		selected.optimal_angles = previous.optimal_angles;
		selected.predicted_pos = previous.predicted_pos;
		selected.score = previous.score;
		selected.found_trajectory = previous.found_trajectory;
	}
	this->m_current_target = std::move( selected );
}

grenade_aim_t::grenade_kind grenade_aim_t::resolve_grenade_kind( std::uintptr_t weapon_vdata ) const
{
	if ( !weapon_vdata ) return grenade_kind::unknown;
	const auto name_ptr = app::context().process.load<std::uintptr_t>(
		weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_id ) );
	if ( !name_ptr ) return grenade_kind::unknown;
	char name[ 64 ]{};
	if ( !app::context().process.copy( name_ptr, name, sizeof( name ) - 1 ) ) return grenade_kind::unknown;
	switch ( identity::of( name ) )
	{
	case "weapon_hegrenade"_id:    return grenade_kind::he;
	case "weapon_flashbang"_id:    return grenade_kind::flash;
	case "weapon_smokegrenade"_id: return grenade_kind::smoke;
	case "weapon_molotov"_id:
	case "weapon_incgrenade"_id:   return grenade_kind::molotov;
	case "weapon_decoy"_id:        return grenade_kind::decoy;
	default:                           return grenade_kind::unknown;
	}
}

grenade_aim_t::trajectory_result grenade_aim_t::simulate_fast( const foundation::vec3& start,
	const foundation::vec3& view_angles, const foundation::vec3& player_vel, float throw_vel_vdata,
	float throw_strength, float sv_gravity, float molotov_floor_normal, grenade_kind kind,
	const foundation::vec3& target_head ) const
{
	trajectory_result result{};
	auto angles = view_angles;
	if ( angles.x > 90.0f ) angles.x -= 360.0f;
	else if ( angles.x < -90.0f ) angles.x += 360.0f;
	angles.x -= ( 90.0f - std::fabsf( angles.x ) ) * 10.0f / 90.0f;

	foundation::vec3 forward{};
	angles.to_directions( &forward, nullptr, nullptr );

	auto pos = start;
	pos.z += throw_strength * 12.0f - 12.0f;
	const auto start_trace = game::collision().sweep_hull(
		pos, pos + forward * 22.0f, simulation::grenade_collision_half_extents );
	pos = start_trace.hit ? start_trace.end_pos - forward * 6.0f : pos + forward * 16.0f;

	const auto throw_vel = std::clamp( throw_vel_vdata * 0.9f, 15.0f, 750.0f );
	const auto throw_speed = ( throw_strength * 0.7f + 0.3f ) * throw_vel;
	auto vel = forward * throw_speed + player_vel * 1.25f;
	const auto gravity = sv_gravity * 0.4f;
	auto closest_head_sqr = pos.distance_sqr( target_head );

	for ( int tick = 0; tick < 256; ++tick )
	{
		const auto new_z = vel.z - gravity * game::rules::simulation_step;
		const foundation::vec3 move{ vel.x * game::rules::simulation_step, vel.y * game::rules::simulation_step,
			( vel.z + new_z ) * 0.5f * game::rules::simulation_step };
		vel.z = new_z;

		const auto trace = game::collision().sweep_hull(
			pos, pos + move, simulation::grenade_collision_half_extents );
		pos = trace.end_pos;
		closest_head_sqr = std::min( closest_head_sqr, pos.distance_sqr( target_head ) );

		if ( trace.hit )
		{
			++result.bounces;
			if ( kind == grenade_kind::molotov && trace.normal.z >= molotov_floor_normal )
			{
				result.valid = true;
				result.end_pos = pos;
				result.duration = static_cast<float>( tick + 1 ) * game::rules::simulation_step;
				result.closest_head_distance = std::sqrt( closest_head_sqr );
				return result;
			}

			auto reflected = ( vel - trace.normal * ( vel.dot( trace.normal ) * 2.0f ) ) * 0.45f;
			if ( trace.normal.z > 0.7f )
			{
				const auto speed_sqr = reflected.length_sqr( );
				if ( speed_sqr > 96000.0f )
				{
					const auto incidence = reflected.normalized( ).dot( trace.normal );
					if ( incidence > 0.5f ) reflected *= 1.5f - incidence;
				}
				if ( speed_sqr < 400.0f ) reflected = {};
			}
			vel = reflected;
			const auto remaining = 1.0f - trace.fraction;
			if ( remaining > 0.0f && vel.length_sqr( ) > 0.0f )
			{
				const auto post_origin = pos + trace.normal * ( 1.0f / 32.0f );
				const auto post_trace = game::collision().sweep_hull(
					post_origin, post_origin + vel * ( remaining * game::rules::simulation_step ),
					simulation::grenade_collision_half_extents );
				pos = post_trace.end_pos;
			}
		}

		const auto elapsed = static_cast<float>( tick ) * game::rules::simulation_step;
		const auto stopped = vel.length_sqr( ) < 400.0f;
		const auto timed_detonation = ( kind == grenade_kind::he || kind == grenade_kind::flash )
			? static_cast<float>( tick - 8 ) * game::rules::simulation_step > 1.5f
			: kind == grenade_kind::molotov && elapsed > 2.0f;
		const auto resting_detonation = ( kind == grenade_kind::smoke || kind == grenade_kind::decoy ) && stopped;
		if ( timed_detonation || resting_detonation || result.bounces > 20 )
		{
			result.valid = true;
			result.end_pos = pos;
			result.duration = static_cast<float>( tick + 1 ) * game::rules::simulation_step;
			result.closest_head_distance = std::sqrt( closest_head_sqr );
			return result;
		}
	}

	return result;
}

void grenade_aim_t::calculate_trajectory( const foundation::vec3& eye_pos, float throw_vel_vdata,
	float throw_strength, grenade_kind kind )
{
	this->m_current_target.found_trajectory = false;
	const auto local_pawn = game::local_player().pawn( );
	foundation::vec3 local_vel{}, target_vel{};
	if ( !local_pawn || !game::collision().valid( )
		|| !app::context().process.copy( local_pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&local_vel, sizeof( local_vel ) )
		|| !app::context().process.copy( this->m_current_target.pawn
			+ SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&target_vel, sizeof( target_vel ) ) ) return;
	if ( !std::isfinite( local_vel.x ) || !std::isfinite( local_vel.y )
		|| !std::isfinite( local_vel.z ) || local_vel.length_sqr( ) > 4'000'000.0f ) return;
	if ( !std::isfinite( target_vel.x ) || !std::isfinite( target_vel.y ) ||
		!std::isfinite( target_vel.z ) || target_vel.length_sqr( ) > 4'000'000.0f ) return;
	this->m_current_target.velocity = target_vel;

	const auto sv_gravity = game::variables().get<float>( CONVAR( "sv_gravity"_id ) );
	const auto molotov_slope = game::variables().get<float>( CONVAR( "weapon_molotov_maxdetonateslope"_id ) );
	if ( !std::isfinite( sv_gravity ) || sv_gravity <= 0.0f || sv_gravity > 2000.0f
		|| !std::isfinite( molotov_slope ) || molotov_slope < 0.0f
		|| molotov_slope > 90.0f ) return;
	const auto molotov_floor_normal = std::cos( foundation::to_radians( molotov_slope ) );
	const auto gravity = std::clamp( sv_gravity, 0.0f, 2000.0f ) * 0.4f;

	auto target_base = this->m_current_target.player.origin;
	if ( kind == grenade_kind::he )
	{
		target_base = this->m_current_target.head_pos;
	}
	else if ( kind == grenade_kind::flash )
	{
		foundation::vec3 target_forward{};
		this->m_current_target.player.eye_angles.to_directions( &target_forward, nullptr, nullptr );
		const auto requested = this->m_current_target.head_pos + target_forward * 96.0f;
		const auto visibility = game::collision().trace_ray( this->m_current_target.head_pos, requested );
		target_base = visibility.hit ? visibility.end_pos - target_forward * 8.0f : requested;
	}

	struct seed
	{
		foundation::vec3 angles{};
		float speed_error{};
	};
	std::array<seed, 24> seeds{};
	std::size_t seed_count{};
	const auto throw_vel = std::clamp( throw_vel_vdata * 0.9f, 15.0f, 750.0f );
	const auto launch_speed = ( throw_strength * 0.7f + 0.3f ) * throw_vel;
	auto analytic_start = eye_pos;
	analytic_start.z += throw_strength * 12.0f - 12.0f;

	for ( int sample = 0; sample < 24; ++sample )
	{
		const auto flight_time = 0.35f + static_cast<float>( sample ) * 0.095f;
		const auto future_target = target_base + target_vel * flight_time;
		auto required = ( future_target - analytic_start - local_vel * ( 1.25f * flight_time ) +
			foundation::vec3{ 0.0f, 0.0f, 0.5f * gravity * flight_time * flight_time } ) / flight_time;
		const auto required_speed = required.length( );
		if ( required_speed < 1.0f ) continue;
		required /= required_speed;
		auto effective = foundation::look_at_angles( {}, required );
		const auto view_pitch = effective.x >= -10.0f
			? 0.9f * ( effective.x + 10.0f )
			: 1.125f * ( effective.x + 10.0f );
		if ( view_pitch < -89.0f || view_pitch > 89.0f ) continue;
		seeds[ seed_count++ ] = { { view_pitch, effective.y, 0.0f }, std::fabsf( required_speed - launch_speed ) };
	}
	std::sort( seeds.begin( ), seeds.begin( ) + seed_count,
		[]( const seed& lhs, const seed& rhs ) { return lhs.speed_error < rhs.speed_error; } );

	foundation::vec3 best_angles{};
	auto best_score = std::numeric_limits<float>::max( );
	auto best_endpoint_distance = std::numeric_limits<float>::max( );
	float best_duration{};
	const auto evaluate = [ & ]( const foundation::vec3& angles )
		{
			const auto head_at_arrival = this->m_current_target.head_pos + target_vel * 1.2f;
			const auto result = this->simulate_fast( eye_pos, angles, local_vel, throw_vel_vdata,
				throw_strength, sv_gravity, molotov_floor_normal, kind, head_at_arrival );
			if ( !result.valid ) return;
			const auto desired = target_base + target_vel * result.duration;
			const auto endpoint_distance = result.end_pos.distance( desired );
			auto score = endpoint_distance + static_cast<float>( result.bounces ) * 1.5f + result.duration * 0.5f;
			if ( kind == grenade_kind::smoke || kind == grenade_kind::molotov || kind == grenade_kind::decoy )
				score += result.closest_head_distance * 0.08f;
			if ( score >= best_score ) return;
			best_score = score;
			best_endpoint_distance = endpoint_distance;
			best_duration = result.duration;
			best_angles = angles;
		};

	for ( std::size_t i = 0; i < std::min<std::size_t>( seed_count, 8 ); ++i ) evaluate( seeds[ i ].angles );
	if ( best_score == std::numeric_limits<float>::max( ) )
		evaluate( foundation::look_at_angles( eye_pos, target_base ) );

	for ( const auto step : { 3.0f, 1.25f, 0.45f } )
	{
		const auto center = best_angles;
		for ( int pitch = -1; pitch <= 1; ++pitch )
			for ( int yaw = -1; yaw <= 1; ++yaw )
				if ( pitch || yaw ) evaluate( { std::clamp( center.x + pitch * step, -89.0f, 89.0f ),
					foundation::wrap_yaw( center.y + yaw * step ), 0.0f } );
	}

	const auto acceptance = kind == grenade_kind::molotov ? 110.0f :
		( kind == grenade_kind::smoke || kind == grenade_kind::decoy ? 130.0f : 150.0f );
	this->m_current_target.score = best_score;
	this->m_current_target.predicted_pos = target_base + target_vel * best_duration;
	this->m_current_target.optimal_angles = best_angles;
	this->m_current_target.found_trajectory = best_score < std::numeric_limits<float>::max( ) &&
		best_endpoint_distance <= acceptance;
}

void grenade_aim_t::smooth_aim( const foundation::vec3& eye_pos, const foundation::vec3& view_angles, const config::combat_profile::global_settings::grenade_aim_config& cfg )
{
	constexpr auto m_yaw{ 0.022f };
	const auto sensitivity = game::variables().get<float>( CONVAR( "sensitivity"_id ) );
	const auto fov_adjust = app::context().process.load<float>( game::local_player().pawn( ) + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_id ) );
	const auto deg_per_pixel = sensitivity * m_yaw * fov_adjust;

	if ( deg_per_pixel <= 0.0f )
	{
		return;
	}

	auto delta_x = this->m_current_target.optimal_angles.x - view_angles.x;
	auto delta_y = foundation::wrap_yaw( this->m_current_target.optimal_angles.y - view_angles.y );

	if ( cfg.smoothing > 1 )
	{
		const auto factor = static_cast< float >( cfg.smoothing );
		delta_x /= factor;
		delta_y /= factor;
	}

	const auto move_x = -delta_y / deg_per_pixel;
	const auto move_y = delta_x / deg_per_pixel;

	this->m_aim_error.x += move_x;
	this->m_aim_error.y += move_y;

	const auto dx = static_cast< int >( this->m_aim_error.x );
	const auto dy = static_cast< int >( this->m_aim_error.y );

	this->m_aim_error.x -= static_cast< float >( dx );
	this->m_aim_error.y -= static_cast< float >( dy );

	if ( dx != 0 || dy != 0 )
	{
		app::context().input.pointer( dx, dy, platform::windows::pointer_action::relative_move );
	}
}

}
