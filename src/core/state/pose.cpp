#include <stdafx.hpp>

#include <core/state/pose.hpp>

namespace game {

	namespace {
		constexpr auto k_pose_fallback_lifetime = std::chrono::milliseconds( 100 );
	}

	void player_pose_sampler::set_presentation_state(
		const bool active, const std::uint32_t display_refresh )
	{
		const auto rate = std::clamp<std::uint32_t>( display_refresh, 144, 240 );
		const auto active_changed = this->m_active.exchange(
			active, std::memory_order_acq_rel ) != active;
		const auto rate_changed = this->m_rate.exchange(
			rate, std::memory_order_acq_rel ) != rate;
		if ( active_changed || rate_changed )
		{
			if ( const auto event = static_cast<HANDLE>(
				this->m_wake_event.load( std::memory_order_acquire ) ) )
			{
				::SetEvent( event );
			}
		}
	}

	std::shared_ptr<const player_pose_frame> player_pose_sampler::latest( ) const
	{

		if ( !this->m_active.load( std::memory_order_acquire ) )
			return {};
		return this->m_latest.load( std::memory_order_acquire );
	}

	void player_pose_sampler::sample_once( )
	{
		VESTA_PERF_SCOPE( pose_sample );
		const auto now = std::chrono::steady_clock::now( );
		auto frame = std::make_shared<player_pose_frame>( );
		frame->sequence = this->m_sequence.fetch_add(
			1, std::memory_order_relaxed ) + 1;
		frame->timestamp = now;
		frame->world = game::world( ).players( );
		if ( !frame->world || !game::camera( ).sample_presentation( frame->camera ) )
		{
			return;
		}

		frame->players.reserve( frame->world->size( ) );
		const auto local_pawn = game::local_player( ).pawn( );
		const auto view_pawn = game::local_player( ).view_pawn( );
		for ( std::size_t index = 0; index < frame->world->size( ); ++index )
		{
			const auto& player = ( *frame->world )[ index ];
			if ( !player.pawn || !player.bone_cache || player.health <= 0 )
			{
				continue;
			}
			if ( player.pawn == local_pawn || player.pawn == view_pawn
				|| !game::local_player( ).is_enemy( player.team ) )
			{
				continue;
			}

			auto bones = game::skeletons( ).get( player.bone_cache );
			bool reused{};
			if ( bones.is_valid( ) )
			{
				this->m_last_valid[ player.pawn ] = {
					player.bone_cache, player.model_path, bones, now };
			}
			else
			{
				const auto cached = this->m_last_valid.find( player.pawn );
				if ( cached == this->m_last_valid.end( )
					|| cached->second.bone_cache != player.bone_cache
					|| cached->second.model_path != player.model_path
					|| now - cached->second.timestamp > k_pose_fallback_lifetime )
				{
					continue;
				}
				bones = cached->second.bones;
				reused = true;
			}

			frame->players.push_back( {
				index, player.pawn, player.bone_cache, player.model_path,
				bones, reused } );
		}

		std::erase_if( this->m_last_valid,
			[ & ]( const auto& entry )
			{
				return now - entry.second.timestamp > k_pose_fallback_lifetime;
			} );

		this->m_latest.store( std::move( frame ), std::memory_order_release );
	}

	void player_pose_sampler::run( )
	{

		::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_NORMAL );
		const auto wake_event = ::CreateEventW( nullptr, FALSE, FALSE, nullptr );
		this->m_wake_event.store( wake_event, std::memory_order_release );

		HANDLE timer = ::CreateWaitableTimerExW( nullptr, nullptr,
			CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
		if ( !timer )
		{
			timer = ::CreateWaitableTimerW( nullptr, FALSE, nullptr );
		}

		while ( true )
		{
			if ( !this->m_active.load( std::memory_order_acquire ) )
			{
				this->m_latest.store( {}, std::memory_order_release );
				this->m_last_valid.clear( );
				if ( wake_event )
				{
					::WaitForSingleObject( wake_event, INFINITE );
				}
				else
				{
					::Sleep( 16 );
				}
				continue;
			}

			this->sample_once( );

			const auto rate = std::max<std::uint32_t>(
				this->m_rate.load( std::memory_order_acquire ), 1 );
			LARGE_INTEGER due{};
			due.QuadPart = -static_cast<LONGLONG>( 10'000'000ull / rate );
			if ( timer && ::SetWaitableTimer( timer, &due, 0, nullptr, nullptr, FALSE ) )
			{
				if ( wake_event )
				{
					const HANDLE waits[]{ wake_event, timer };
					::WaitForMultipleObjects( 2, waits, FALSE, INFINITE );
				}
				else
				{
					::WaitForSingleObject( timer, INFINITE );
				}
			}
			else
			{
				::Sleep( std::max<DWORD>( 1, 1000 / rate ) );
			}
		}
	}

}
