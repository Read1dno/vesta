#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <simulation/collision.hpp>
#include <simulation/blast.hpp>
#include <simulation/nav_mesh.hpp>
#include <app/map_loader.hpp>
#include <app/context.hpp>
#include <diagnostics/journal.hpp>

namespace app
{
namespace readiness = diagnostics::readiness;

map_loader::map_loader(readiness::channel& geometry)
    : m_geometry(geometry)
    , m_worker([this](std::stop_token stop) { run(stop); })
{
    // Serials restart with every loader; drop the previous session's state.
    m_geometry.reset();
}

map_loader::~map_loader()
{
    m_worker.request_stop();
    m_queue.cancel();
    // Join before publishing shutdown so a finishing worker cannot overwrite it.
    if (m_worker.joinable())
        m_worker.join();
    m_geometry.stop();
}

void map_loader::request(std::string map)
{
    // Map names are deliberately not journaled; only fixed states and events cross the boundary.
    const auto empty = map.empty();
    m_queue.request(std::move(map),
                    [this, empty](std::uint64_t serial)
                    {
                        if (empty)
                            m_geometry.publish(serial, readiness::state::not_started, readiness::reason::no_map);
                        else
                        {
                            m_geometry.publish(serial, readiness::state::loading, readiness::reason::geometry_building);
                            diagnostics::emit(diagnostics::event::map_geometry_begin);
                        }
                        simulation::collision().clear();
                        simulation::blast_damage().clear();
                        simulation::nav_mesh().clear();
                    });
}

void map_loader::run(std::stop_token stop)
{
    const auto background = ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    if (!background)
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::stop_callback cancel(stop, [this] { m_queue.cancel(); });
    while (const auto job = m_queue.next(stop))
    {
        if (stop.stop_requested())
            break;
        if (job->key.empty())
            continue;
        try
        {
            bool from_file{};
            {
                VESTA_PERF_SCOPE(map_load);
                simulation::collision_world built;
                from_file = built.build_from_map_file(job->key, job->stop);
                if (!from_file && !job->stop.stop_requested())
                {
                    // The exact files were unusable; say why, then try the live copy, which is incomplete by nature.
                    diagnostics::emit(diagnostics::event::map_geometry_file_failed, diagnostics::severity::warning,
                                      diagnostics::code_domain::mode,
                                      static_cast<std::uint32_t>(built.last_file_failure()));
                    built.parse(job->stop);
                }
                if (!m_queue.commit(job->stop, [&] { simulation::collision().replace_with(built); }))
                {
                    diagnostics::emit(diagnostics::event::map_geometry_cancelled);
                    continue;
                }
                if (simulation::collision().valid() && from_file)
                {
                    m_geometry.publish(job->serial, readiness::state::ready);
                    diagnostics::emit(diagnostics::event::map_geometry_ready);
                }
                else if (simulation::collision().valid())
                {
                    // Geometry exists but did not come from the map files: wall checks may be wrong, so the player is told.
                    m_geometry.publish(job->serial, readiness::state::ready, readiness::reason::geometry_partial);
                    diagnostics::emit(diagnostics::event::map_geometry_live_fallback, diagnostics::severity::warning);
                }
                else
                {
                    // No usable geometry for this map: dependent features stay off and say why.
                    m_geometry.publish(job->serial, readiness::state::waiting_dependency,
                                       readiness::reason::geometry_missing);
                    diagnostics::emit(diagnostics::event::map_geometry_unavailable, diagnostics::severity::warning);
                }
                // The navigation mesh measures ceilings against the geometry committed above.
                auto nav = simulation::load_nav_mesh(job->key, job->stop);
                if (!m_queue.commit(job->stop, [&] { simulation::nav_mesh().replace(std::move(nav)); }))
                {
                    diagnostics::emit(diagnostics::event::map_geometry_cancelled);
                    continue;
                }
                simulation::blast_model blast;
                blast.parse(job->stop);
                if (!m_queue.commit(job->stop, [&] { simulation::blast_damage().replace_with(blast); }))
                {
                    diagnostics::emit(diagnostics::event::map_geometry_cancelled);
                    continue;
                }
            }
            if (from_file)
            {
                std::mutex wait_mutex;
                std::condition_variable_any wake;
                std::unique_lock wait_lock(wait_mutex);
                while (!job->stop.stop_requested())
                {
                    wake.wait_for(wait_lock, job->stop, std::chrono::milliseconds(100), [] { return false; });
                    if (job->stop.stop_requested())
                        break;
                    simulation::collision().refresh_map_entities(job->stop);
                }
            }
        }
        catch (const std::exception&)
        {
            // Unwind the private build before publishing; never terminate unrelated workers.
            m_geometry.publish(job->serial, readiness::state::error, readiness::reason::geometry_failed);
            diagnostics::emit(diagnostics::event::map_geometry_failed, diagnostics::severity::error);
        }
        catch (...)
        {
            m_geometry.publish(job->serial, readiness::state::error, readiness::reason::geometry_failed);
            diagnostics::emit(diagnostics::event::map_geometry_failed, diagnostics::severity::error);
        }
    }
    if (background)
        ::SetThreadPriority(::GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
}

} // namespace app
