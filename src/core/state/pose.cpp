#include <stdafx.hpp>

#include <core/state/pose.hpp>
#include <app/workers.hpp>

namespace game {

	namespace {
		constexpr auto k_pose_fallback_lifetime = detail::pose_fallback_lifetime;

        detail::pose_identity pose_context(const local_snapshot& local,
            const std::shared_ptr<const std::string>& level, std::uint64_t presentation)
        {
            return {presentation, local.controller, local.pawn,
                local.alive ? local.pawn : local.observer_pawn,
                reinterpret_cast<std::uintptr_t>(level.get()), local.pawn_handle,
                local.view_team, local.team_mode};
        }
	}

	void player_pose_sampler::set_presentation_state(
		const bool active, const std::uint32_t sample_rate )
	{
        auto state = this->m_presentation.load(std::memory_order_acquire);
        const bool changed = static_cast<bool>(state & 1u) != active;
        if (changed)
        {
            const auto next = ((state + 2u) & ~std::uint64_t{1}) | std::uint64_t{active};
            this->m_presentation.store(next, std::memory_order_release);
            this->m_latest.store({}, std::memory_order_release);
            this->m_last_valid.clear();
            this->m_last_identity = {};
            this->m_last_level.reset();
            this->m_frames.clear();
            this->m_schedule.reset();
        }
        this->m_rate = sample_rate;
	}

    std::shared_ptr<const player_pose_frame> player_pose_sampler::acquire_for_presentation()
    {
        // Single producer: only the render thread samples, after all frame pacing waits.
        if (!(this->m_presentation.load(std::memory_order_acquire) & 1u)) return {};
        const auto now = std::chrono::steady_clock::now();
        this->m_schedule.configure(this->m_rate, now);
        if (this->m_schedule.due(now))
        {
            this->sample_once();
            this->m_schedule.sampled(std::chrono::steady_clock::now());
        }
        return this->latest();
    }

	std::shared_ptr<const player_pose_frame> player_pose_sampler::latest( ) const
	{

        const auto presentation = this->m_presentation.load(std::memory_order_acquire);
        if (!(presentation & 1u)) return {};
        const auto frame = this->m_latest.load(std::memory_order_acquire);
        const auto local = game::local_player().snapshot();
        const auto level = app::workers::current_map();
        if (!frame || !local || !detail::pose_frame_usable(frame->identity,
                pose_context(*local, level, presentation), frame->timestamp,
                std::chrono::steady_clock::now())
            || this->m_presentation.load(std::memory_order_acquire) != presentation)
            return {};
        return frame;
	}

	void player_pose_sampler::sample_once( )
	{
		VESTA_PERF_SCOPE( pose_sample );
		const auto now = std::chrono::steady_clock::now( );
        const auto presentation = this->m_presentation.load(std::memory_order_acquire);
        const auto local = game::local_player().snapshot();
        const auto level = app::workers::current_map();
        const auto identity = local ? pose_context(*local, level, presentation) : detail::pose_identity{};
        if (identity != this->m_last_identity)
        {
            this->m_last_valid.clear();
            this->m_latest.store({}, std::memory_order_release);
            this->m_last_identity = identity;
            this->m_last_level = level;
        }
        if (!detail::valid_pose_identity(identity)) return;
        auto frame = this->m_frames.acquire();
        frame->players.clear();
        frame->camera = {};
        frame->identity = identity;
        frame->level = level;
		frame->sequence = this->m_sequence.fetch_add(
			1, std::memory_order_relaxed ) + 1;
		frame->timestamp = now;
		frame->world = game::world( ).players( );
		if ( !frame->world )
		{
			return;
		}

		frame->players.reserve( frame->world->size( ) );
        const auto local_pawn = identity.pawn;
        const auto view_pawn = identity.view_pawn;
		for ( std::size_t index = 0; index < frame->world->size( ); ++index )
		{
			const auto& player = ( *frame->world )[ index ];
			if ( !player.pawn || !player.bone_cache || player.health <= 0 )
			{
				continue;
			}
			if ( player.pawn == local_pawn || player.pawn == view_pawn
                || (identity.team_mode && identity.view_team == player.team))
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

        if (!game::camera().sample_presentation(frame->camera)) return;
        const auto final_local = game::local_player().snapshot();
        const auto final_level = app::workers::current_map();
        const auto final_presentation = this->m_presentation.load(std::memory_order_acquire);
        if (!final_local || !detail::pose_frame_usable(identity,
            pose_context(*final_local, final_level, final_presentation), now,
            std::chrono::steady_clock::now())) return;
        this->m_latest.store(std::move(frame), std::memory_order_release);
	}

} // namespace game
