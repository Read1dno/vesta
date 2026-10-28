#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace diagnostics
{
// Only fixed product events and numeric native codes cross the journal boundary.
// No arbitrary messages, paths, identities, pointer values or script contents.
//
// Every event is listed once here; the enumeration and the name written to the journal both come from this list.
#define VESTA_JOURNAL_EVENTS(X)                                                                                        \
    X(bootstrap_begin, "bootstrap.begin")                                                                              \
    X(bootstrap_exit, "bootstrap.exit")                                                                                \
    X(bootstrap_exception, "bootstrap.exception")                                                                      \
    X(startup_instance_begin, "startup.instance.begin")                                                                \
    X(startup_instance_duplicate, "startup.instance.duplicate")                                                        \
    X(startup_instance_failed, "startup.instance.failed")                                                              \
    X(startup_console_failed, "startup.console.failed")                                                                \
    X(startup_attach_begin, "startup.attach.begin")                                                                    \
    X(startup_attach_ready, "startup.attach.ready")                                                                    \
    X(startup_attach_failed, "startup.attach.failed")                                                                  \
    X(startup_input_begin, "startup.input.begin")                                                                      \
    X(startup_input_ready, "startup.input.ready")                                                                      \
    X(startup_input_failed, "startup.input.failed")                                                                    \
    X(startup_modules_begin, "startup.modules.begin")                                                                  \
    X(startup_modules_ready, "startup.modules.ready")                                                                  \
    X(startup_modules_failed, "startup.modules.failed")                                                                \
    X(startup_addresses_begin, "startup.addresses.begin")                                                              \
    X(startup_addresses_ready, "startup.addresses.ready")                                                              \
    X(startup_addresses_failed, "startup.addresses.failed")                                                            \
    X(startup_schema_begin, "startup.schema.begin")                                                                    \
    X(startup_schema_ready, "startup.schema.ready")                                                                    \
    X(startup_schema_failed, "startup.schema.failed")                                                                  \
    X(startup_config_begin, "startup.config.begin")                                                                    \
    X(startup_cache_unavailable, "startup.cache.unavailable")                                                          \
    X(startup_scripting_begin, "startup.scripting.begin")                                                              \
    X(startup_scripting_ready, "startup.scripting.ready")                                                              \
    X(startup_scripting_disabled, "startup.scripting.disabled")                                                        \
    X(startup_workers_begin, "startup.workers.begin")                                                                  \
    X(startup_workers_ready, "startup.workers.ready")                                                                  \
    X(runtime_worker_system_error, "runtime.worker.system_error")                                                      \
    X(runtime_worker_unknown_exception, "runtime.worker.unknown_exception")                                            \
    X(runtime_game_exited, "runtime.game.exited")                                                                      \
    X(runtime_quiesce_begin, "runtime.quiesce.begin")                                                                  \
    X(runtime_quiesce_end, "runtime.quiesce.end")                                                                      \
    X(input_backends, "input.backends")                                                                                \
    X(input_hook_failed, "input.hook.failed")                                                                          \
    X(bhop_binding_available, "bhop.binding.available")                                                                \
    X(bhop_binding_unavailable, "bhop.binding.unavailable")                                                            \
    X(profile_scene_ready, "profile.scene.ready")                                                                      \
    X(profile_scene_unavailable, "profile.scene.unavailable")                                                          \
    X(overlay_backend, "overlay.backend")                                                                              \
    X(overlay_tracker_failed, "overlay.tracker.failed")                                                                \
    X(launch_begin, "launch.begin")                                                                                    \
    X(launch_no_process, "launch.no_process")                                                                          \
    X(launch_class_failed, "launch.class_failed")                                                                      \
    X(window_created, "window.created")                                                                                \
    X(graphics_failed, "graphics.failed")                                                                              \
    X(backend_prepared_without_source, "backend.prepared_without_source")                                              \
    X(run_begin, "run.begin")                                                                                          \
    X(run_end, "run.end")                                                                                              \
    X(run_failed, "run.failed")                                                                                        \
    X(presentation_frame_latency_stalled, "presentation.frame_latency_stalled")                                        \
    X(presentation_present_stalled, "presentation.present_stalled")                                                    \
    X(presentation_nonwaitable_stalled, "presentation.nonwaitable_stalled")                                            \
    X(presentation_starved, "presentation.starved")                                                                    \
    X(presentation_present_failed, "presentation.present_failed")                                                      \
    X(presentation_device_status, "presentation.device_status")                                                        \
    X(presentation_recovery_scheduled, "presentation.recovery_scheduled")                                              \
    X(shutdown_begin, "shutdown.begin")                                                                                \
    X(window_hidden_hooks_removed, "window.hidden_hooks_removed")                                                      \
    X(graphics_idle, "graphics.idle")                                                                                  \
    X(graphics_released, "graphics.released")                                                                          \
    X(window_destroyed, "window.destroyed")                                                                            \
    X(shutdown_complete, "shutdown.complete")                                                                          \
    X(content_shown, "content.shown")                                                                                  \
    X(content_hidden, "content.hidden")                                                                                \
    X(presentation_create_failed, "presentation.create_failed")                                                        \
    X(presentation_source_created, "presentation.source_created")                                                      \
    X(presentation_source_destroyed, "presentation.source_destroyed")                                                  \
    X(presentation_hwnd_attached, "presentation.hwnd_attached")                                                        \
    X(presentation_attach_failed, "presentation.attach_failed")                                                        \
    X(presentation_attached, "presentation.attached")                                                                  \
    X(presentation_hwnd_detached, "presentation.hwnd_detached")                                                        \
    X(presentation_detached, "presentation.detached")                                                                  \
    X(graphics_low_priority_failed, "graphics.low_priority_failed")                                                    \
    X(startup_storage_begin, "startup.storage.begin")                                                                  \
    X(startup_storage_ready, "startup.storage.ready")                                                                  \
    X(startup_storage_failed, "startup.storage.failed")                                                                \
    X(startup_storage_retained, "startup.storage.retained")                                                            \
    X(startup_storage_stage, "startup.storage.stage")                                                                  \
    X(map_geometry_begin, "map.geometry.begin")                                                                        \
    X(map_geometry_ready, "map.geometry.ready")                                                                        \
    X(map_geometry_unavailable, "map.geometry.unavailable")                                                            \
    X(map_geometry_failed, "map.geometry.failed")                                                                      \
    X(map_geometry_cancelled, "map.geometry.cancelled")                                                                \
    X(startup_previous_crash, "startup.previous_crash")                                                                \
    X(overlay_band_created, "overlay.band.created")                                                                    \
    X(overlay_band_fallback, "overlay.band.fallback")                                                                  \
    X(map_geometry_file_failed, "map.geometry.file_failed")                                                            \
    X(map_geometry_live_fallback, "map.geometry.live_fallback")                                                        \
    X(map_geometry_world, "map.geometry.world")                                                                        \
    X(map_geometry_entities, "map.geometry.entities")                                                                  \
    X(map_geometry_kv3_failed, "map.geometry.kv3_failed")                                                              \
    X(player_skipped, "world.player_skipped")                                                                          \
    X(autostop_waiting, "autostop.waiting")                                                                            \
    X(trigger_waiting, "trigger.waiting")                                                                              \
    X(autoaccept_resolver, "autoaccept.resolver")                                                                      \
    X(autoaccept_candidate, "autoaccept.candidate")                                                                    \
    X(autoaccept_click, "autoaccept.click")                                                                            \
    X(autoaccept_blocked, "autoaccept.blocked")                                                                        \
    X(autoaccept_scan, "autoaccept.scan")                                                                              \
    X(navmesh_ready, "navmesh.ready")                                                                                  \
    X(navmesh_unavailable, "navmesh.unavailable")                                                                      \
    X(walkbot_idle, "walkbot.idle")                                                                                    \
    X(walkbot_route, "walkbot.route")                                                                                  \
    X(walkbot_obstacle, "walkbot.obstacle")                                                                            \
    X(matchflow_action, "matchflow.action")                                                                            \
    X(matchflow_missing, "matchflow.missing")                                                                          \
    X(diagnostic_warning, "diagnostic.warning")

enum class event : std::uint16_t
{
#define VESTA_JOURNAL_EVENT_ID(id, label) id,
    VESTA_JOURNAL_EVENTS(VESTA_JOURNAL_EVENT_ID)
#undef VESTA_JOURNAL_EVENT_ID
};

inline constexpr std::array event_names{
#define VESTA_JOURNAL_EVENT_NAME(id, label) std::string_view{label},
    VESTA_JOURNAL_EVENTS(VESTA_JOURNAL_EVENT_NAME)
#undef VESTA_JOURNAL_EVENT_NAME
};

#undef VESTA_JOURNAL_EVENTS

inline constexpr std::size_t event_count = event_names.size();

enum class subject : std::uint8_t
{
    application,
    process_monitor,
    game_state,
    movement,
    combat,
    lineup,
    seed_trigger,
    watchdog,
    profile_timeout,
    last = profile_timeout
};

enum class severity : std::uint8_t
{
    information,
    warning,
    error
};

// How the numeric code of a record is to be read.
enum class code_domain : std::uint8_t
{
    none,
    win32,
    hresult,
    system,
    worker,
    // A feature-specific value: a stage, a count or a reason enumerator.
    mode,
    // The identity of a diagnostic message (FNV-1a of its format pattern), see diagnostics::logger.
    message,
    last = message
};

[[nodiscard]] constexpr std::string_view name(const event id) noexcept
{
    const auto index = static_cast<std::size_t>(id);
    return index < event_names.size() ? event_names[index] : std::string_view{"invalid"};
}
} // namespace diagnostics
