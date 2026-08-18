#include <stdafx.hpp>
#include <core/input/bindings.hpp>
#include <features/misc/auto_stop.hpp>

namespace {
	[[nodiscard]] bool finite_velocity( const foundation::vec3& value )
	{
		return std::isfinite( value.x ) && std::isfinite( value.y )
			&& std::isfinite( value.z );
	}
}

namespace features::misc {

	void auto_stop_t::request_stop( const auto_stop_source source,
		const std::chrono::steady_clock::time_point shot_time,
		const float required_shoot_speed )
	{
		if ( !config::general_settings.m_auto_stop.enabled )
		{
			cancel_request( source );
			return;
		}

		std::scoped_lock lock( m_request_mutex );
		auto& request = m_requests[ static_cast<std::size_t>( source ) ];
		const auto was_active = request.active;
		request.shot_time = shot_time;
		request.required_shoot_speed = std::clamp(
			std::isfinite( required_shoot_speed ) ? required_shoot_speed : 5.0f,
			0.0f, 150.0f );
		request.active = true;

		if ( !was_active )
		{
			++request.revision;
			request.braking = false;
		}
	}

	void auto_stop_t::cancel_request( const auto_stop_source source )
	{
		std::scoped_lock lock( m_request_mutex );
		auto& request = m_requests[ static_cast<std::size_t>( source ) ];
		if ( request.active || request.braking ) ++request.revision;
		request = { .revision = request.revision };
	}

	void auto_stop_t::notify_shot( const auto_stop_source source )
	{
		{
			std::scoped_lock lock( m_request_mutex );
			auto& request = m_requests[ static_cast<std::size_t>( source ) ];
			++request.revision;
			request = { .revision = request.revision };
		}

		release( );
	}

	bool auto_stop_t::ready_to_fire( const auto_stop_source source ) const
	{
		if ( !config::general_settings.m_auto_stop.enabled ) return true;

		float required_speed{};
		{
			std::scoped_lock lock( m_request_mutex );
			const auto& request = m_requests[ static_cast<std::size_t>( source ) ];
			if ( !request.active ) return true;
			required_speed = request.required_shoot_speed;
		}

		const auto pawn = game::local_player().pawn( );
		if ( !pawn ) return false;
		std::uint32_t flags{};
		if ( !app::context().process.copy(
			pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ),
			&flags, sizeof( flags ) ) ) return false;

		if ( ( flags & 1u ) == 0u ) return true;
		foundation::vec3 velocity{};
		if ( !app::context().process.copy(
			pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ),
			&velocity, sizeof( velocity ) ) ) return false;

		const auto completion_speed = std::min( required_speed, std::max(
			config::general_settings.m_auto_stop.stop_speed, 0.5f ) );
		return finite_velocity( velocity )
			&& velocity.length_2d( ) <= completion_speed;
	}

	float auto_stop_t::estimate_stop_seconds(
		const foundation::vec3& velocity, const float target_speed ) const
	{
		auto speed = velocity.length_2d( );
		if ( !std::isfinite( speed ) || speed <= target_speed ) return 0.0f;

		const auto friction = std::max(
			game::variables().get<float>( CONVAR( "sv_friction"_id ) ), 0.1f );
		const auto stop_speed = std::max(
			game::variables().get<float>( CONVAR( "sv_stopspeed"_id ) ), 1.0f );
		const auto accelerate = std::max(
			game::variables().get<float>( CONVAR( "sv_accelerate"_id ) ), 0.1f );
		const auto step = std::max( game::rules::simulation_step, 0.001f );

		int ticks{};

		while ( speed > target_speed && ticks < 64 )
		{
			const auto friction_drop = std::max( speed, stop_speed ) * friction * step;
			const auto counter_gain = accelerate * 250.0f * step;
			speed = std::max( 0.0f, speed - friction_drop - counter_gain );
			++ticks;
		}
		return static_cast<float>( ticks ) * step;
	}

	void auto_stop_t::engage( const foundation::vec3& velocity,
		const float stop_speed )
	{
		std::scoped_lock control_lock( m_control_mutex );

		if ( m_active ) return;
		if ( velocity.length_2d( ) <= stop_speed ) return;

		auto& bindings = game::input_bindings( );
		const auto forward_key = bindings.resolve( game::input_action::forward );
		const auto back_key = bindings.resolve( game::input_action::back );
		const auto left_key = bindings.resolve( game::input_action::left );
		const auto right_key = bindings.resolve( game::input_action::right );
		const auto add_unique = []( std::vector<std::uint16_t>& output,
			const game::input_binding& binding )
		{
			if ( binding && binding.virtual_key
				&& std::ranges::find( output, binding.virtual_key ) == output.end( ) )
				output.push_back( binding.virtual_key );
		};
		const auto physically_down = []( const game::input_binding& binding )
		{
			return binding && binding.virtual_key
				&& ( ::GetAsyncKeyState( binding.virtual_key ) & 0x8000 ) != 0;
		};

		std::vector<std::uint16_t> desired{};
		const auto forward_down = physically_down( forward_key );
		const auto back_down = physically_down( back_key );
		if ( forward_down != back_down )
			add_unique( desired, forward_down ? back_key : forward_key );
		const auto left_down = physically_down( left_key );
		const auto right_down = physically_down( right_key );
		if ( left_down != right_down )
			add_unique( desired, left_down ? right_key : left_key );
		if ( desired.empty( ) ) return;

		std::vector<platform::windows::input_gateway::key_transition> presses{};
		for ( const auto key : desired ) presses.push_back( { key, true } );
		if ( !app::context().input.keys( presses ) ) return;
		m_synthetic_keys = std::move( desired );
		m_active = true;
	}

	void auto_stop_t::release( )
	{
		std::scoped_lock control_lock( m_control_mutex );
		if ( !m_active && m_synthetic_keys.empty( ) ) return;

		std::vector<platform::windows::input_gateway::key_transition> releases{};

		for ( const auto key : m_synthetic_keys ) releases.push_back( { key, false } );
		if ( !releases.empty( ) && !app::context().input.keys( releases ) ) return;
		m_synthetic_keys.clear( );
		m_active = false;
	}

	void auto_stop_t::reset( )
	{
		{
			std::scoped_lock lock( m_request_mutex );
			for ( auto& request : m_requests )
			{
				++request.revision;
				request = { .revision = request.revision };
			}
		}
		release( );
	}

	void auto_stop_t::tick( )
	{
		if ( !config::general_settings.m_auto_stop.enabled )
		{

			if ( m_enabled_last_tick )
			{
				m_enabled_last_tick = false;
				reset( );
			}
			return;
		}
		m_enabled_last_tick = true;
		const auto pawn = game::local_player().pawn( );
		const auto usable = pawn && game::local_player().alive( )
			&& game::rules::is_firearm( game::local_player().weapon_type( ) )
			&& !app::context().menu.is_open( )
			&& app::context().overlay.combat_input_ready( );
		if ( !usable )
		{
			reset( );
			return;
		}

		const auto flags = app::context().process.load<std::uint32_t>(
			pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_id ) );
		if ( ( flags & 1u ) == 0u )
		{
			release( );
			return;
		}
		const auto velocity = app::context().process.load<foundation::vec3>(
			pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_id ) );
		if ( !finite_velocity( velocity ) )
		{
			release( );
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		bool should_brake{};
		float stop_speed = 150.0f;
		{
			std::scoped_lock lock( m_request_mutex );
			for ( auto& request : m_requests )
			{
				if ( !request.active ) continue;
				if ( !request.braking )
				{
					const auto completion_speed = std::min(
						request.required_shoot_speed, std::max(
							config::general_settings.m_auto_stop.stop_speed, 0.5f ) );
					const auto stop_seconds = estimate_stop_seconds(
						velocity, completion_speed );
					const auto remaining = std::chrono::duration<float>(
						request.shot_time - now ).count( );
					request.braking = remaining <= stop_seconds
						+ game::rules::simulation_step;
				}
				const auto completion_speed = std::min(
					request.required_shoot_speed, std::max(
						config::general_settings.m_auto_stop.stop_speed, 0.5f ) );
				stop_speed = std::min( stop_speed, completion_speed );
				should_brake = should_brake || request.braking;
			}
		}

		if ( should_brake ) engage( velocity, stop_speed );
		else release( );
	}

}
