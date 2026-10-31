#include <stdafx.hpp>
#include <diagnostics/performance.hpp>
#include <render/presentation.hpp>
#include <config/visuals.hpp>
#include <game/state/services.hpp>
#include <platform/windows/input.hpp>
#include <game/state/pose.hpp>
#include <diagnostics/health.hpp>
#include <game/memory/bound_services.hpp>
#include <ui/presentation.hpp>
#include <diagnostics/journal.hpp>
#include <platform/windows/worker_control.hpp>
#include <foundation/frame_schedule.hpp>
#include <foundation/object_watch.hpp>
#include <ui/overlay/present_policy.hpp>
#include <ui/overlay/hud.hpp>
#include <ui/overlay/graphics_device.hpp>
#include <ui/overlay/render_policy.hpp>
#include <scripting/runtime.hpp>
#include <config/hotkeys.hpp>
#include <config/library.hpp>
#include <features/aimbot/aimbot.hpp>
#include <features/misc/misc.hpp>
#include <features/misc/auto_stop.hpp>
#include <features/misc/walk_bot.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/event_log.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <features/chams/texture.hpp>
#include <render/text/localization.hpp>
#include <ui/overlay/overlay.hpp>
#include <ui/overlay/imgui_frame.hpp>
#include <render/text/esp_text.hpp>
#include <resources/fonts/notosans_medium.hpp>
#include <resources/fonts/weapons.hpp>
#include <resources/images/ct.hpp>
#include <resources/watermark_loss_icon.hpp>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <ui/overlay/window_band.hpp>
#include <wincodec.h>
#include <commdlg.h>
#include <fstream>
#include <filesystem>
#include <limits>
#include <optional>
#include <wrl/client.h>
#include <shobjidl.h>
#include <ui/menu/menu.hpp>

namespace ui
{
namespace
{
constexpr bool k_use_composition_backend = true;

[[nodiscard]] std::optional<std::string> utf8_from_wide(const std::wstring_view value)
{
    if (value.empty())
        return std::string{};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::nullopt;
    const auto input_size = static_cast<int>(value.size());
    const auto output_size =
        ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), input_size, nullptr, 0, nullptr, nullptr);
    if (output_size <= 0)
        return std::nullopt;
    std::string result(static_cast<std::size_t>(output_size), '\0');
    if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), input_size, result.data(), output_size,
                              nullptr, nullptr) != output_size)
    {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::wstring> wide_from_utf8(const std::string_view value)
{
    if (value.empty())
        return std::wstring{};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::nullopt;
    const auto input_size = static_cast<int>(value.size());
    const auto output_size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_size, nullptr, 0);
    if (output_size <= 0)
        return std::nullopt;
    std::wstring result(static_cast<std::size_t>(output_size), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_size, result.data(), output_size) !=
        output_size)
    {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] bool configured_overlay_content()
{
    const auto& visuals = config::visual_settings;
    const auto& misc = config::general_settings;
    const auto& combat = config::combat_settings.global;
    return visuals.m_player.enabled || visuals.m_item.enabled || visuals.m_projectile.enabled ||
           visuals.m_bomb.enabled || visuals.m_no_flash.enabled || visuals.m_no_smoke.enabled ||
           visuals.m_crosshair.enabled || (visuals.m_player.enabled && visuals.m_chams.enabled) ||
           visuals.m_radar.enabled || visuals.m_sound.enabled || misc.m_grenades.enabled ||
           misc.m_nade_helper.enabled || misc.m_watermark.enabled || misc.m_spectator_list.enabled ||
           misc.m_event_log.enabled || misc.m_keybind_list.enabled || misc.m_bullet_tracers.enabled ||
           misc.m_hitmarker.enabled || misc.m_hitsound.enabled || misc.m_hitsound.show_damage ||
           (combat.aimbot_enabled && combat.aimbot_draw_fov) || combat.penetration_crosshair;
}

void write_overlay_backend_diagnostic(bool window_in_band, bool tracker_enabled, DWORD tracker_error,
                                      bool input_router_enabled, bool composition_enabled)
{
    const auto mode = (window_in_band ? 1u : 0u) | (tracker_enabled ? 2u : 0u) | (input_router_enabled ? 4u : 0u) |
                      (composition_enabled ? 8u : 0u);
    diagnostics::emit(diagnostics::event::overlay_backend, diagnostics::severity::information,
                      diagnostics::code_domain::mode, mode);
    if (!tracker_enabled)
        diagnostics::emit(diagnostics::event::overlay_tracker_failed, diagnostics::severity::warning,
                          diagnostics::code_domain::win32, tracker_error);
}
void write_overlay_lifecycle_event(diagnostics::event id, HRESULT result = S_OK)
{
    const auto label = diagnostics::name(id);
    const auto level = FAILED(result) || label.ends_with(".failed") || label.ends_with("_failed")
                           ? diagnostics::severity::error
                       : label.ends_with("_stalled") ? diagnostics::severity::warning
                                                     : diagnostics::severity::information;
    diagnostics::emit(id, level, diagnostics::code_domain::hresult, static_cast<std::uint32_t>(result));
}

[[nodiscard]] HRESULT wait_for_gpu_idle(ID3D11Device* const device, ID3D11DeviceContext* const context)
{
    if (!device || !context)
        return S_OK;

    D3D11_QUERY_DESC description{};
    description.Query = D3D11_QUERY_EVENT;
    ID3D11Query* completion{};
    const HRESULT create_result = device->CreateQuery(&description, &completion);
    if (FAILED(create_result))
        return create_result;

    context->End(completion);
    context->Flush();

    const ULONGLONG deadline = ::GetTickCount64() + 250;
    HRESULT result = S_FALSE;
    while (result == S_FALSE && ::GetTickCount64() < deadline)
    {
        result = context->GetData(completion, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (result == S_FALSE)
            ::Sleep(1);
    }
    completion->Release();
    return result == S_FALSE ? HRESULT_FROM_WIN32(WAIT_TIMEOUT) : result;
}

class config_watch
{
public:
    [[nodiscard]] bool update() noexcept
    {
        const bool combat = m_combat.update(config::combat_settings);
        const bool visual = m_visual.update(config::visual_settings);
        const bool general = m_general.update(config::general_settings);
        return combat || visual || general;
    }

private:
    foundation::object_watch<config::combat_profile> m_combat{config::combat_settings};
    foundation::object_watch<config::visual_profile> m_visual{config::visual_settings};
    foundation::object_watch<config::general_profile> m_general{config::general_settings};
};

} // namespace

bool overlay_t::gpu_effects_enabled() const noexcept
{
    return m_use_gpu && config::general_settings.use_gpu && m_chams_renderer_initialized &&
           features::chams::g_renderer.ready();
}

void overlay_t::ensure_gpu_effects()
{
    if (!m_use_gpu || !config::general_settings.use_gpu || !m_device || !m_context)
        return;
    if (!m_chams_renderer_initialized)
    {
        m_chams_renderer_initialized = true;
        if (!features::chams::g_renderer.initialize(m_device, m_context))
            game::bound_memory().log.warning("chams renderer initialization failed.");
    }
    if (!m_chams_preview_initialized)
    {
        m_chams_preview_initialized = true;
        if (!features::chams::g_preview.initialize(m_device, m_context))
            game::bound_memory().log.warning("chams preview initialization failed.");
    }
}

bool overlay_t::launch(foundation::shutdown_action quiesce)
{
    const foundation::scope_exit cleanup(
        [this, quiesce]() noexcept
        {
            quiesce();
            shutdown();
        });
    write_overlay_lifecycle_event(diagnostics::event::launch_begin);
    const auto process_id = game::bound_memory().process.process_id();
    if (!process_id)
    {
        write_overlay_lifecycle_event(diagnostics::event::launch_no_process, E_INVALIDARG);
        return false;
    }

    m_window_tracking_active = m_window_tracker.initialize(process_id);
    if (!m_window_tracking_active)
    {
        return false;
    }

    if (!register_overlay_class())
    {
        write_overlay_lifecycle_event(diagnostics::event::launch_class_failed);
        return false;
    }

    constexpr int screen_w = 1;
    constexpr int screen_h = 1;
    constexpr int screen_x = 0;
    constexpr int screen_y = 0;
    m_render_width = 1;
    m_render_height = 1;
    m_resize_pending = false;
    m_use_gpu = true;
    m_frame_latency_unreliable = false;
    m_nonwaitable_stall_reported = false;
    m_latency_timeouts = 0;
    m_latency_fallback_reported = false;
    m_ui_reference_width = 0;
    m_ui_reference_height = 0;
    m_ui_fullscreen_canvas = false;

    const DWORD extended_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                 (k_use_composition_backend ? WS_EX_NOREDIRECTIONBITMAP : 0);
    // This HWND has no DirectComposition target/root while CS2 is backgrounded.
    // WS_EX_NOREDIRECTIONBITMAP and its 1x1 size keep it out of presentation.
    // The window is created in the SYSTEM_TOOLS band when Windows allows it for an ordinary process, so the
    // overlay stays above a game that does not go through the desktop band; otherwise a topmost window is used.
    const auto created = ui::overlay_band::create_window(extended_style, k_class_name, k_class_name, WS_POPUP, screen_x,
                                                         screen_y, 1, 1, ::GetModuleHandleW(nullptr));
    m_hwnd = created.window;
    m_window_in_band = created.in_band;
    if (!m_hwnd)
    {
        return false;
    }
    if (created.in_band)
        diagnostics::emit(diagnostics::event::overlay_band_created);
    else
        diagnostics::emit(diagnostics::event::overlay_band_fallback, diagnostics::severity::warning,
                          diagnostics::code_domain::win32, created.error);
    diagnostics::health().overlay.publish(
        1, created.in_band ? diagnostics::readiness::state::ready : diagnostics::readiness::state::waiting_dependency,
        created.in_band ? diagnostics::readiness::reason::none
                        : diagnostics::readiness::reason::overlay_band_unavailable);
    write_overlay_lifecycle_event(diagnostics::event::window_created);
    overlay_t::s_active_instance.store(this, std::memory_order_release);
    if (m_window_tracking_active)
    {
        m_window_tracker.bind_overlay(m_hwnd, m_window_in_band);
        // Poll once after binding. Readiness may already be true; attachment still
        // happens only from synchronize_window_bounds() after graphics preparation.
        window_tracker::update initial_update{};
        static_cast<void>(m_window_tracker.poll(initial_update));
    }

    constexpr MARGINS margins{-1, -1, -1, -1};
    ::DwmExtendFrameIntoClientArea(m_hwnd, &margins);
    ::SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    apply_capture_policy();

    if (!initialize_graphics())
    {
        write_overlay_lifecycle_event(diagnostics::event::graphics_failed);
        return false;
    }
    write_overlay_lifecycle_event(diagnostics::event::backend_prepared_without_source);

    ensure_gpu_effects();

    ui::bound_menu().initialize(m_hwnd);

    m_input_router_active = m_input_router.initialize();
    if (!m_input_router_active)
    {
        game::bound_memory().log.warning("overlay input router failed to launch -- falling back to polled input.");
    }
    write_overlay_backend_diagnostic(m_window_in_band, m_window_tracking_active, m_window_tracker.failure_code(),
                                     m_input_router_active, m_composition_active);

    game::bound_memory().log.info("render initialized.");
    write_overlay_lifecycle_event(diagnostics::event::run_begin);

    const auto completed = run();
    write_overlay_lifecycle_event(completed ? diagnostics::event::run_end : diagnostics::event::run_failed);

    return completed;
}

bool overlay_t::register_overlay_class()
{
    const auto instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (::GetClassInfoExW(instance, k_class_name, &existing))
    {
        return true;
    }

    WNDCLASSEXW descriptor{};
    descriptor.cbSize = sizeof(descriptor);
    descriptor.style = CS_CLASSDC;
    descriptor.lpfnWndProc = &overlay_t::window_callback;
    descriptor.hInstance = instance;

    descriptor.hCursor = nullptr;
    descriptor.lpszClassName = k_class_name;

    m_atom = ::RegisterClassExW(&descriptor);
    return m_atom != 0;
}

// Drains the thread's message queue; false once the overlay is shutting down (WM_QUIT included).
bool overlay_t::pump_messages()
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            set_menu_hit_testing(false);
            config::storage.write_cache();
            request_shutdown();
            return false;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return !m_shutdown_requested.load(std::memory_order_acquire);
}

// Waits for `signal` (the swap chain's frame-latency object, or the frame-rate timer) while dispatching messages.
// The frame-latency wait gives up after 50 ms and marks the waitable unreliable. False when the frame must be skipped:
// shutdown, or a window transition that detached or hid the presentation during the wait.
bool overlay_t::wait_for_signal(HANDLE signal, std::uint64_t observed_transition)
{
    if (!signal)
        return true;
    const bool frame_latency_signal = signal == m_frame_latency_waitable;
    std::optional<diagnostics::performance::scope> wait_profile{};
    if (frame_latency_signal)
        wait_profile.emplace(diagnostics::performance::zone::wait_frame_latency);

    constexpr ULONGLONG frame_latency_timeout_ms = 50;
    const ULONGLONG frame_latency_deadline = frame_latency_signal ? ::GetTickCount64() + frame_latency_timeout_ms : 0u;
    const auto presentation_generation = m_presentation_generation;
    const auto reconcile_transition = [&]
    {
        if (m_window_tracker.transition_revision() == observed_transition)
            return true;
        synchronize_window_bounds();
        synchronize_content_visibility();
        observed_transition = m_window_tracker.transition_revision();
        return m_presentation_attached && m_overlay_visible && m_presentation_generation == presentation_generation;
    };
    while (true)
    {
        const auto now_tick = ::GetTickCount64();
        const DWORD wait_timeout =
            frame_latency_signal
                ? (now_tick >= frame_latency_deadline ? 0u : static_cast<DWORD>(frame_latency_deadline - now_tick))
                : INFINITE;
        const auto wait_result =
            ::MsgWaitForMultipleObjectsEx(1, &signal, wait_timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait_result == WAIT_OBJECT_0)
        {
            if (frame_latency_signal)
                m_latency_timeouts = 0;
            return pump_messages() && reconcile_transition();
        }
        if (wait_result == WAIT_OBJECT_0 + 1)
        {
            if (!pump_messages() || !reconcile_transition())
                return false;
            continue;
        }
        if (wait_result == WAIT_TIMEOUT && frame_latency_signal)
        {
            // One late signal (a loading hitch, a busy GPU) only lets this frame go ahead. Only a signal that stays
            // silent for several frames in a row switches to timer pacing, and that is probed again later
            // (enter_latency_fallback).
            if (++m_latency_timeouts >= k_latency_timeouts_for_fallback)
                enter_latency_fallback(diagnostics::event::presentation_frame_latency_stalled);
            return reconcile_transition();
        }
        return false;
    }
}

// The exit key: releases everything Vesta may be holding in the game and asks the overlay to stop.
void overlay_t::release_input_for_exit()
{
    ::ClipCursor(nullptr);
    set_menu_hit_testing(false);
    features::aimbot::aim().reset();
    const std::array movement_releases{
        platform::windows::input_gateway::key_transition{VK_CONTROL, false},
        platform::windows::input_gateway::key_transition{VK_F24, false},
    };
    platform::windows::bound_input().keys(movement_releases);
    platform::windows::bound_input().pointer(
        0, 0, platform::windows::pointer_action::primary_up | platform::windows::pointer_action::secondary_up);
    config::storage.write_cache();
    request_shutdown();
}

// Window, menu and capture state for the coming frame, and how many player poses presentation needs.
void overlay_t::update_frame_state()
{
    ui::bound_menu().poll_hotkey();
    ensure_gpu_effects();
    synchronize_window_bounds();
    synchronize_content_visibility();
    const auto feature_plan =
        ui::plan_features(m_presentation_attached, config::visual_settings.m_player.active(),
                          config::visual_settings.m_chams.enabled, m_use_gpu, config::general_settings.use_gpu);
    game::render_poses().set_presentation_state(
        feature_plan.sample_players,
        ui::pose_sample_limit(config::general_settings.limit_fps,
                              static_cast<std::uint32_t>(std::max(0, config::general_settings.fps_limit))));
    synchronize_menu_focus();
    apply_capture_policy();
    apply_resource_priority();
}

// Carries out a file chosen in the config/Lua dialog (which runs on its own thread). Failures are logged by the
// storage and the script runtime.
void overlay_t::apply_pending_file_request()
{
    if (!m_cfg_ready.exchange(false))
        return;
    std::string path;
    file_request request{};
    {
        std::lock_guard<std::mutex> lock(m_cfg_mutex);
        path = m_cfg_path;
        request = m_cfg_request;
    }
    if (path.empty())
        return;
    const auto chosen = std::filesystem::u8path(path);
    switch (request)
    {
        case file_request::import_lua:
            static_cast<void>(scripting::runtime().import_script(chosen));
            break;
        case file_request::import_config:
            config::library().note(config::library().import_file(chosen), "Imported");
            break;
        case file_request::choose_config_folder:
            config::library().note(config::library().set_folder(chosen), "Folder selected");
            break;
    }
}

// Switches to timer pacing at the monitor rate while the swap chain's latency signal is silent; pace_frame() probes the
// signal every two seconds and returns to it. Journaled once per session.
void overlay_t::enter_latency_fallback(const diagnostics::event reason)
{
    if (m_frame_latency_unreliable)
        return;
    m_frame_latency_unreliable = true;
    m_latency_timeouts = 0;
    m_latency_probe_at = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (!m_latency_fallback_reported)
    {
        m_latency_fallback_reported = true;
        write_overlay_lifecycle_event(reason);
    }
}

// Holds the frame back for the swap chain's latency signal and the frame-rate cap (the user's limit, or the monitor
// rate when the latency signal is unreliable). False when the frame must be skipped.
bool overlay_t::pace_frame(foundation::frame_schedule& clock, HANDLE timer)
{
    const auto& general = config::general_settings;
    // While timer pacing, the latency signal is checked every two seconds; once it is signalled the frames follow the
    // compositor again.
    if (m_frame_latency_unreliable && m_frame_latency_waitable &&
        std::chrono::steady_clock::now() >= m_latency_probe_at)
    {
        m_latency_probe_at = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        if (::WaitForSingleObject(m_frame_latency_waitable, 0) == WAIT_OBJECT_0)
        {
            m_frame_latency_unreliable = false;
            m_latency_timeouts = 0;
            return true; // the signal just consumed paces this frame
        }
    }
    const auto rate = general.limit_fps && general.fps_limit > 0
                          ? static_cast<std::uint32_t>(general.fps_limit)
                          : (m_frame_latency_unreliable ? std::max(m_window_tracker.refresh_rate(), 60u) : 0u);
    clock.set_rate(rate, std::chrono::steady_clock::now());
    if (m_frame_latency_waitable && !m_frame_latency_unreliable &&
        !wait_for_signal(m_frame_latency_waitable, m_window_tracker.transition_revision()))
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (rate && now < clock.deadline())
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(clock.deadline() - now).count();
        LARGE_INTEGER due{};
        due.QuadPart = -std::max<LONGLONG>(1, (remaining + 99) / 100); // relative, in 100 ns units
        if (timer && ::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
        {
            if (!wait_for_signal(timer, m_window_tracker.transition_revision()))
                return false;
        }
        else
        {
            std::this_thread::sleep_until(clock.deadline());
        }
    }
    clock.advance(std::chrono::steady_clock::now());
    return true;
}

// The game-world layer of a frame: chams, ESP and the other in-world visuals, from one presentation pose.
void overlay_t::draw_world(render::draw::draw_list& draw_list)
{
    const auto pose_frame = game::render_poses().acquire_for_presentation();
    game::presentation_camera_sample camera{};
    if (game::camera().sample_presentation(camera))
        game::camera().begin_presentation_frame(camera, m_render_width, m_render_height);
    else if (pose_frame)
        game::camera().begin_presentation_frame(pose_frame->camera, m_render_width, m_render_height);

    // Performance zones are compile-time no-ops in production Release.
    if (m_use_gpu && config::general_settings.use_gpu)
    {
        VESTA_PERF_SCOPE(chams);
        features::chams::g_renderer.render_world_effects(m_rtv, m_render_width, m_render_height);
        features::chams::g_renderer.render_frame(m_rtv, m_render_width, m_render_height, pose_frame);
    }
    {
        VESTA_PERF_SCOPE(player_esp);
        features::visuals::player().render(draw_list, pose_frame);
    }
    {
        VESTA_PERF_SCOPE(world_visuals);
        features::visuals::sound().on_render(draw_list);
        features::visuals::items().on_render(draw_list);
        features::visuals::projectiles().on_render(draw_list);
        features::visuals::bomb().on_render(draw_list);
        features::visuals::radar().on_render(draw_list);
        features::visuals::crosshair().on_render(draw_list);
        features::visuals::grenade_prediction().on_render(draw_list);
        features::misc::nade_helper().on_render(draw_list);
        features::misc::walk_bot().on_render(draw_list);
        features::visuals::bullet_impacts().on_render(draw_list);
        features::aimbot::aim().on_render(draw_list);
    }
}

// Records the whole ImGui frame: world visuals (hidden while spectated, when the user asked for it), scripts, HUD
// panels and the menu.
void overlay_t::build_frame()
{
    constexpr float clear[4]{0.0f, 0.0f, 0.0f, 0.0f};
    m_context->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_context->ClearRenderTargetView(m_rtv, clear);

    ImGui_ImplDX11_NewFrame();
    ui::imgui_frame::begin(static_cast<float>(m_render_width), static_cast<float>(m_render_height));
    route_overlay_input();

    ImGui::NewFrame();
    render::draw::draw_list draw_list{ImGui::GetBackgroundDrawList()};
    features::chams::g_renderer.begin_2d_bloom_frame(gpu_effects_enabled());

    const auto spectator_suppressed =
        config::visual_settings.m_player.spectator_sync && game::world().local_spectated();
    if (game::local_player().valid() && !spectator_suppressed)
        draw_world(draw_list);

    scripting::runtime().render(draw_list, m_render_width, m_render_height);
    // Outlined ESP text queued by the features above goes out as one block.
    render::esp_text::bind(m_context);
    render::esp_text::flush(draw_list.m_im_draw_list);

    {
        VESTA_PERF_SCOPE(overlay_panels);
        const auto interactive = render::menu_open();
        m_hud_panels.draw_system_notices(interactive);
        m_hud_panels.draw_watermark(interactive);
        m_hud_panels.draw_spectator_list(interactive);
        if (!spectator_suppressed)
        {
            m_hud_panels.draw_event_log(interactive);
            m_hud_panels.draw_keybind_list(interactive);
            m_hud_panels.draw_bomb_info(interactive);
        }
    }

    {
        VESTA_PERF_SCOPE(menu);
        ui::bound_menu().draw();
    }
    // The window procedure answers WM_SETCURSOR with the menu's cursor; nudge it when that cursor changes.
    const auto menu_cursor = static_cast<int>(ImGui::GetMouseCursor());
    const auto previous_menu_cursor = overlay_t::s_menu_cursor.exchange(menu_cursor, std::memory_order_acq_rel);
    if (menu_cursor != previous_menu_cursor && overlay_t::s_menu_hit_testing.load(std::memory_order_acquire))
        ::PostMessageW(m_hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(m_hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
}

void overlay_t::submit_frame()
{
    {
        VESTA_PERF_SCOPE(imgui_render);
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    if (m_use_gpu && config::general_settings.use_gpu && !render::menu_open())
    {
        VESTA_PERF_SCOPE(bloom_2d);
        features::chams::g_renderer.render_2d_bloom(m_rtv, m_render_width, m_render_height);
    }
}

// Presents without blocking the render thread for long. While the queue is full (WAS_STILL_DRAWING) the finished image
// is offered again for up to one refresh period, and only then dropped: dropping at once and rendering a new frame
// queued more GPU work behind a GPU that was already behind, and under a busy game (the uncapped menu scene in
// windowed mode) the overlay then lost nearly every frame. A refused tearing flag is turned off once. A failed
// Present either schedules a surface rebuild or ends the overlay.
overlay_t::present_outcome overlay_t::present_frame(ui::present_recovery& recovery)
{
    const auto present_started = std::chrono::steady_clock::now();
    const auto patience = std::chrono::microseconds(1'000'000 / std::clamp(m_window_tracker.refresh_rate(), 30u, 500u));
    HRESULT present_result{};
    bool dropped{};
    while (true)
    {
        VESTA_PERF_SCOPE(present);
        const UINT present_flags =
            (m_present_tearing_enabled ? DXGI_PRESENT_ALLOW_TEARING : 0) | DXGI_PRESENT_DO_NOT_WAIT;
        present_result = m_swap_chain->Present(0, present_flags);
        if (present_result == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            if (std::chrono::steady_clock::now() - present_started < patience)
            {
                ::MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                continue;
            }
            dropped = true;
            break;
        }
        if (present_result == DXGI_ERROR_INVALID_CALL && m_present_tearing_enabled)
        {
            m_present_tearing_enabled = false;
            continue;
        }
        break;
    }
    if (std::chrono::steady_clock::now() - present_started > std::chrono::milliseconds(250))
    {
        if (!m_frame_latency_unreliable)
            enter_latency_fallback(diagnostics::event::presentation_present_stalled);
        else if (!m_nonwaitable_stall_reported)
        {
            m_nonwaitable_stall_reported = true;
            write_overlay_lifecycle_event(diagnostics::event::presentation_nonwaitable_stalled);
        }
    }
    note_present_outcome(dropped);
    if (dropped)
        return present_outcome::dropped;
    if (SUCCEEDED(present_result))
    {
        if (present_result == S_OK)
            diagnostics::performance::record_present();
        return present_outcome::presented;
    }

    const auto device_result = m_device->GetDeviceRemovedReason();
    write_overlay_lifecycle_event(diagnostics::event::presentation_present_failed, present_result);
    write_overlay_lifecycle_event(diagnostics::event::presentation_device_status, device_result);
    const auto now = std::chrono::steady_clock::now();
    if (!recovery.retry_surface(present_result, device_result, now))
        return present_outcome::failed;
    detach_presentation();
    m_presentation_retry.failed(now);
    m_resize_pending = true;
    write_overlay_lifecycle_event(diagnostics::event::presentation_recovery_scheduled);
    return present_outcome::recovering;
}

// Journals a two-second window in which the overlay showed fewer than 20 frames a second while frames were being
// dropped (at most every 30 s): code = presented frames << 16 | dropped frames.
void overlay_t::note_present_outcome(const bool dropped)
{
    const auto now = std::chrono::steady_clock::now();
    if (m_present_window_start == std::chrono::steady_clock::time_point{})
        m_present_window_start = now;
    ++(dropped ? m_window_dropped : m_window_presented);
    const auto elapsed = now - m_present_window_start;
    if (elapsed < std::chrono::seconds(2))
        return;
    const auto seconds = std::chrono::duration<float>(elapsed).count();
    if (m_window_dropped > 0 && static_cast<float>(m_window_presented) / seconds < 20.0f && m_starved_note.allow(now))
        diagnostics::emit(diagnostics::event::presentation_starved, diagnostics::severity::warning,
                          diagnostics::code_domain::mode,
                          (std::min(m_window_presented, 0xffffu) << 16) | std::min(m_window_dropped, 0xffffu));
    m_window_presented = 0;
    m_window_dropped = 0;
    m_present_window_start = now;
}

// The render thread's loop; false when presentation failed beyond recovery.
bool overlay_t::run()
{
    m_render_thread_id.store(::GetCurrentThreadId(), std::memory_order_release);
    HANDLE fps_timer =
        ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!fps_timer)
        fps_timer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
    const foundation::unique_handle fps_timer_owner(fps_timer);
    foundation::frame_schedule frame_clock;
    config_watch observed_config;
    ui::present_recovery present_recovery;
    constexpr DWORD idle_wait_ms = 16;

    bool exit_was_down = false;
    bool completed = true;
    while (!m_shutdown_requested.load(std::memory_order_acquire))
    {
        const auto exit_is_down = (::GetAsyncKeyState(config::lifecycle_keys().exit) & 0x8000) != 0;
        if (exit_is_down && !exit_was_down)
        {
            release_input_for_exit();
            break;
        }
        exit_was_down = exit_is_down;

        if (!pump_messages())
            break;
        update_frame_state();
        if (!m_overlay_visible)
        {
            ::MsgWaitForMultipleObjectsEx(0, nullptr, idle_wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        apply_pending_file_request();
        if (m_content_suppressed)
        {
            ::MsgWaitForMultipleObjectsEx(0, nullptr, idle_wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        if (!pace_frame(frame_clock, fps_timer))
            continue;

        std::optional<diagnostics::performance::scope> render_profile{};
        render_profile.emplace(diagnostics::performance::zone::render_frame);
        const auto render_transition = m_window_tracker.transition_revision();
        const auto render_generation = m_presentation_generation;
        build_frame();
        {
            VESTA_PERF_SCOPE(config_fingerprint);
            const auto now = std::chrono::steady_clock::now();
            if (observed_config.update())
            {
                config::publish_runtime_snapshot();
                m_config_dirty = true;
                m_config_changed_at = now;
            }
            // The live configuration follows every change: written a moment after the last edit, or as soon as the
            // menu closes.
            if (m_config_dirty &&
                (now - m_config_changed_at >= std::chrono::milliseconds(1500) || !render::menu_open()))
            {
                m_config_dirty = false;
                static_cast<void>(config::storage.write_cache());
            }
        }
        submit_frame();
        // Keep CPU/GPU command construction separate from message dispatch and
        // Present retry waits in the telemetry.
        render_profile.reset();

        if (!pump_messages())
            break;
        // A window transition during the frame may have replaced or hidden the surface it was drawn for.
        if (m_window_tracker.transition_revision() != render_transition)
        {
            synchronize_window_bounds();
            synchronize_content_visibility();
            if (!m_presentation_attached || !m_overlay_visible || m_presentation_generation != render_generation)
                continue;
        }
        const auto outcome = present_frame(present_recovery);
        if (outcome == present_outcome::failed)
        {
            completed = false;
            break;
        }
        if (outcome == present_outcome::presented)
            diagnostics::performance::flush_if_due();
    }

    m_render_thread_id.store(0, std::memory_order_release);
    set_menu_hit_testing(false);
    diagnostics::performance::flush_if_due(true);
    config::storage.write_cache();
    return completed;
}

void overlay_t::shutdown() noexcept
{
    if (m_shutdown_active.exchange(true, std::memory_order_acq_rel))
        return;

    write_overlay_lifecycle_event(diagnostics::event::shutdown_begin);
    // Render work has stopped and the launch scope quiesced game/Lua workers.
    // Futures retaining process/archive data must finish before those owners die.
    features::visuals::bomb().shutdown_background();
    features::chams::g_renderer.drain_background();

    stop_config_dialog();
    set_menu_hit_testing(false);
    if (m_input_router_active)
    {
        m_input_router.shutdown();
        m_input_router_active = false;
    }
    if (m_hwnd)
    {
        ::SetWindowDisplayAffinity(m_hwnd, WDA_NONE);
        ::ShowWindow(m_hwnd, SW_HIDE);
    }
    m_overlay_visible = false;
    detach_presentation();
    m_window_tracker.shutdown();
    m_window_tracking_active = false;
    write_overlay_lifecycle_event(diagnostics::event::window_hidden_hooks_removed);

    // Retire every command that can still reference a composition back buffer
    // before removing that buffer from the visual tree.
    if (m_context)
    {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
        m_context->ClearState();
    }
    const HRESULT gpu_idle_result = wait_for_gpu_idle(m_device, m_context);
    write_overlay_lifecycle_event(diagnostics::event::graphics_idle, gpu_idle_result);

    if (m_chams_preview_initialized)
    {
        features::chams::g_preview.shutdown();
        m_chams_preview_initialized = false;
    }
    if (m_chams_renderer_initialized)
    {
        features::chams::g_renderer.shutdown();
        m_chams_renderer_initialized = false;
    }
    if (m_imgui_dx11_active)
    {
        render::esp_text::release();
        ImGui_ImplDX11_Shutdown();
        m_imgui_dx11_active = false;
    }
    if (m_imgui_win32_active)
    {
        ImGui_ImplWin32_Shutdown();
        m_imgui_win32_active = false;
    }
    if (m_imgui_context_active)
    {
        m_fonts = {};
        render::draw::shutdown_fonts();
        ImGui::DestroyContext();
        m_imgui_context_active = false;
    }

    const auto release = [](auto*& object)
    {
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    };
    release(m_ct_preview_texture);
    for (auto& [steamid, avatar] : m_steam_avatars)
        release(avatar);
    m_steam_avatars.clear();
    m_missing_steam_avatars.clear();
    release(m_dsv);
    release(m_rtv);
    release(m_back_buffer);
    if (m_frame_latency_waitable)
    {
        ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
    }
    release(m_swap_chain);
    release(m_composition_visual);
    release(m_composition_target);
    release(m_composition_device);
    release(m_context);
    release(m_device);
    m_composition_active = false;
    m_presentation_attached = false;
    write_overlay_lifecycle_event(diagnostics::event::graphics_released);

    const HWND destroyed_window = m_hwnd;
    overlay_t* expected = this;
    overlay_t::s_active_instance.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    if (m_hwnd)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_atom)
    {
        ::UnregisterClassW(k_class_name, ::GetModuleHandleW(nullptr));
        m_atom = 0;
    }
    write_overlay_lifecycle_event(diagnostics::event::window_destroyed);
    write_overlay_lifecycle_event(diagnostics::event::shutdown_complete);
}

void overlay_t::apply_capture_policy()
{
    const auto enabled = config::general_settings.obs_bypass;
    if (m_obs_bypass_initialized && m_obs_bypass_observed == enabled)
    {
        return;
    }

    // WDA_EXCLUDEFROMCAPTURE keeps the layered overlay fully visible locally while
    // presenting it as transparent to supported desktop/window capture paths.
    const auto affinity = enabled ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    ::SetWindowDisplayAffinity(m_hwnd, affinity);
    m_obs_bypass_observed = enabled;
    m_obs_bypass_initialized = true;
}

namespace
{
// The game's CPU priority class as a rank; normal while no game is attached or the class cannot be read.
[[nodiscard]] ui::cpu_class_rank game_priority_rank()
{
    const auto process_id = game::bound_memory().process.process_id();
    if (!process_id)
        return ui::cpu_class_rank::normal;
    const foundation::unique_handle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    switch (process.get() ? ::GetPriorityClass(process.get()) : 0)
    {
        case IDLE_PRIORITY_CLASS:
            return ui::cpu_class_rank::idle;
        case BELOW_NORMAL_PRIORITY_CLASS:
            return ui::cpu_class_rank::below_normal;
        case ABOVE_NORMAL_PRIORITY_CLASS:
            return ui::cpu_class_rank::above_normal;
        case HIGH_PRIORITY_CLASS:
        case REALTIME_PRIORITY_CLASS:
            return ui::cpu_class_rank::high;
        default:
            return ui::cpu_class_rank::normal;
    }
}

[[nodiscard]] DWORD windows_priority_class(const ui::cpu_class_rank rank) noexcept
{
    switch (rank)
    {
        case ui::cpu_class_rank::idle:
        case ui::cpu_class_rank::below_normal:
            return BELOW_NORMAL_PRIORITY_CLASS;
        case ui::cpu_class_rank::above_normal:
            return ABOVE_NORMAL_PRIORITY_CLASS;
        case ui::cpu_class_rank::high:
            return HIGH_PRIORITY_CLASS;
        case ui::cpu_class_rank::normal:
        default:
            return NORMAL_PRIORITY_CLASS;
    }
}
} // namespace

// Misc > Engine CPU and GPU priorities. Runs on the render thread, so the render-thread boost lands on the right
// thread. The game's class is re-read every two seconds, since it can be changed in Task Manager while both run.
void overlay_t::apply_resource_priority()
{
    const auto& general = config::general_settings;
    const auto now = std::chrono::steady_clock::now();
    const bool recheck_game = now >= m_next_priority_check;
    if (!recheck_game && general.cpu_priority == m_cpu_priority_applied &&
        general.gpu_priority == m_gpu_priority_applied)
        return;

    if (recheck_game)
        m_next_priority_check = now + std::chrono::seconds(2);
    const auto game_rank = recheck_game || m_game_priority_rank < 0
                               ? game_priority_rank()
                               : static_cast<ui::cpu_class_rank>(m_game_priority_rank);
    if (general.cpu_priority != m_cpu_priority_applied || static_cast<int>(game_rank) != m_game_priority_rank)
    {
        const auto plan = ui::plan_cpu_priority(ui::to_resource_priority(general.cpu_priority), game_rank);
        ::SetPriorityClass(::GetCurrentProcess(), windows_priority_class(plan.priority_class));
        // ControlMask 0 hands the decision back to Windows; EXECUTION_SPEED with StateMask 0 opts out of EcoQoS.
        PROCESS_POWER_THROTTLING_STATE throttling{
            PROCESS_POWER_THROTTLING_CURRENT_VERSION,
            plan.disable_power_throttling ? static_cast<ULONG>(PROCESS_POWER_THROTTLING_EXECUTION_SPEED) : 0ul, 0ul};
        (void)::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
        ::SetThreadPriority(::GetCurrentThread(),
                            plan.raise_render_thread ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL);
        m_cpu_priority_applied = general.cpu_priority;
        m_game_priority_rank = static_cast<int>(game_rank);
    }
    if (general.gpu_priority != m_gpu_priority_applied && m_device)
    {
        const auto result = ui::set_overlay_gpu_priority(
            m_device, ui::plan_gpu_priority(ui::to_resource_priority(general.gpu_priority)));
        if (FAILED(result))
            write_overlay_lifecycle_event(diagnostics::event::graphics_low_priority_failed, result);
        m_gpu_priority_applied = general.gpu_priority;
    }
}

void overlay_t::synchronize_window_bounds()
{
    if (!m_window_tracking_active)
        return;

    window_tracker::update update{};
    const bool tracker_changed = m_window_tracker.poll(update);
    if (!tracker_changed && !m_resize_pending && m_presentation_attached == m_window_tracker.visible())
        return;

    const bool target_ready = m_window_tracker.visible();
    const auto recovery_now = std::chrono::steady_clock::now();
    if (target_ready && !m_presentation_attached && !m_presentation_retry.ready(recovery_now))
        return;

    if (!target_ready)
    {
        detach_presentation();
    }
    if (!target_ready)
    {
        m_resize_pending = true;
        return;
    }

    const auto width = m_window_tracker.width();
    const auto height = m_window_tracker.height();
    if (width < 64 || height < 64)
    {
        m_resize_pending = true;
        return;
    }
    m_ui_fullscreen_canvas = m_window_tracker.covers_monitor();
    if (m_ui_fullscreen_canvas && (m_ui_reference_width == 0 || m_ui_reference_height == 0))
    {
        auto reference_width = width;
        auto reference_height = height;
        const auto monitor = ::MonitorFromWindow(m_window_tracker.target(), MONITOR_DEFAULTTONULL);
        MONITORINFOEXW information{};
        information.cbSize = sizeof(information);
        DEVMODEW desktop_mode{};
        desktop_mode.dmSize = sizeof(desktop_mode);
        if (monitor && ::GetMonitorInfoW(monitor, &information) &&
            ::EnumDisplaySettingsW(information.szDevice, ENUM_REGISTRY_SETTINGS, &desktop_mode) &&
            desktop_mode.dmPelsWidth > 0 && desktop_mode.dmPelsHeight > 0)
        {
            reference_width = desktop_mode.dmPelsWidth;
            reference_height = desktop_mode.dmPelsHeight;
        }
        m_ui_reference_width = reference_width;
        m_ui_reference_height = reference_height;
    }

    if (!m_presentation_attached || width != m_render_width || height != m_render_height)
    {
        detach_presentation();
        const bool dimensions_changed = width != m_render_width || height != m_render_height;
        if (dimensions_changed)
        {
            if (m_composition_active)
            {
                if (!resize_graphics(width, height))
                {
                    m_resize_pending = true;
                    return;
                }
            }
            else
            {
                // No HWND-bound swap chain exists while detached; the dimensions
                // become the creation parameters for attach_presentation().
                m_render_width = width;
                m_render_height = height;
            }
        }
        if (!attach_presentation())
        {
            m_presentation_retry.failed(recovery_now);
            m_resize_pending = true;
            return;
        }
        m_presentation_retry.succeeded();
        m_resize_pending = false;
    }

    if (m_presentation_attached && (update.geometry_changed || update.z_order_changed || update.visibility_changed))
    {
        m_window_tracker.place_overlay();
    }
}

void overlay_t::synchronize_content_visibility()
{
    const bool content_requested = render::menu_open() || configured_overlay_content();
    const bool suppressed = !content_requested;
    const bool target_visible = m_window_tracking_active && m_window_tracker.visible();
    m_combat_input_ready.store(target_visible && m_presentation_attached, std::memory_order_release);
    const bool should_show = content_requested && target_visible && m_presentation_attached;

    if (suppressed == m_content_suppressed && should_show == m_overlay_visible)
    {
        return;
    }

    m_content_suppressed = suppressed;
    if (should_show)
    {
        if (m_window_tracking_active)
            m_window_tracker.place_overlay();
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        write_overlay_lifecycle_event(diagnostics::event::content_shown);
    }
    else
    {
        ::ShowWindow(m_hwnd, SW_HIDE);
        write_overlay_lifecycle_event(diagnostics::event::content_hidden);
    }
    m_overlay_visible = should_show;
}

void overlay_t::route_overlay_input()
{
    if (m_input_router_active && m_input_router.capture_ready())
    {
        const auto origin = m_window_tracking_active ? m_window_tracker.client() : RECT{};
        const auto menu_open = render::menu_open();

        m_input_router.pump_imgui(origin.left, origin.top, !menu_open);
        if (menu_open)
        {
            POINT cursor{};
            if (::GetCursorPos(&cursor) && ::ScreenToClient(m_hwnd, &cursor))
            {
                auto x = static_cast<float>(cursor.x);
                auto y = static_cast<float>(cursor.y);
                const auto display = ImGui::GetIO().DisplaySize;
                ui::bound_menu().map_pointer_to_layout(x, y, display.x, display.y);
                ImGui::GetIO().AddMousePosEvent(x, y);
            }
        }
        return;
    }

    auto& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    if (!render::menu_open())
    {
        return;
    }

    POINT cursor{};
    if (!::GetCursorPos(&cursor))
    {
        return;
    }

    const HWND hit = ::WindowFromPoint(cursor);
    const HWND hit_root = hit ? ::GetAncestor(hit, GA_ROOT) : nullptr;
    const HWND target = m_window_tracker.target();
    if (hit_root != target && hit_root != m_hwnd)
    {
        io.AddMouseButtonEvent(0, false);
        io.AddMouseButtonEvent(1, false);
        io.AddMouseButtonEvent(2, false);
        return;
    }

    if (::ScreenToClient(m_hwnd, &cursor))
    {
        auto x = static_cast<float>(cursor.x);
        auto y = static_cast<float>(cursor.y);
        ui::bound_menu().map_pointer_to_layout(x, y, io.DisplaySize.x, io.DisplaySize.y);
        io.AddMousePosEvent(x, y);
    }
    io.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Super,
                   (::GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (::GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);

    for (int key = 1; key < 256; ++key)
    {
        const auto imgui_key = ui::key_from_virtual_key(key);
        if (imgui_key == ImGuiKey_None)
            continue;
        const auto down = (::GetAsyncKeyState(key) & 0x8000) != 0;
        const auto was_down = m_overlay_key_states[key];
        if (down != was_down)
        {
            io.AddKeyEvent(imgui_key, down);
            m_overlay_key_states[key] = down;
        }

        if (down && !was_down &&
            ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || key == VK_SPACE ||
             (key >= VK_OEM_1 && key <= VK_OEM_3) || (key >= VK_OEM_4 && key <= VK_OEM_7) || key == VK_OEM_PLUS ||
             key == VK_OEM_COMMA || key == VK_OEM_MINUS || key == VK_OEM_PERIOD))
        {
            BYTE keyboard_state[256]{};
            if (::GetKeyboardState(keyboard_state))
            {
                wchar_t characters[8]{};
                const auto scan_code = ::MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
                const auto count = ::ToUnicodeEx(key, scan_code, keyboard_state, characters,
                                                 static_cast<int>(std::size(characters)), 0, ::GetKeyboardLayout(0));
                for (auto index = 0; index < count; ++index)
                {
                    io.AddInputCharacterUTF16(characters[index]);
                }
            }
        }
    }
}

void overlay_t::synchronize_menu_focus()
{
    const auto menu_open = render::menu_open();
    const auto interactive = menu_open && m_overlay_visible;
    const auto target = m_window_tracking_active ? m_window_tracker.target() : nullptr;
    const HWND foreground = ::GetForegroundWindow();
    const HWND foreground_root = foreground ? ::GetAncestor(foreground, GA_ROOT) : nullptr;
    DWORD target_process{};
    DWORD foreground_process{};
    if (target)
        ::GetWindowThreadProcessId(target, &target_process);
    if (foreground_root)
        ::GetWindowThreadProcessId(foreground_root, &foreground_process);
    const bool target_is_active = target_process != 0 && target_process == foreground_process;
    const bool capture = interactive && target_is_active && !overlay_t::s_modal_active.load(std::memory_order_relaxed);
    const bool interaction_started = interactive && !m_menu_interactive;
    m_menu_interactive = interactive;
    const bool escape_is_down = (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool physical_escape_pressed = escape_is_down && !m_escape_was_down;
    m_escape_was_down = escape_is_down;

    const auto tap_escape = []()
    {
        static_cast<void>(platform::windows::bound_input().key(VK_ESCAPE, false));
        const std::array escape_tap{
            platform::windows::input_gateway::key_transition{VK_ESCAPE, true},
            platform::windows::input_gateway::key_transition{VK_ESCAPE, false},
        };
        const auto sent = platform::windows::bound_input().keys(escape_tap);
        static_cast<void>(platform::windows::bound_input().key(VK_ESCAPE, false));
        return sent;
    };
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    const bool game_cursor_visible = ::GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0;
    if (interaction_started && target_is_active && !game_cursor_visible)
    {
        m_game_menu_opened_by_overlay = tap_escape();
    }
    if (menu_open && m_game_menu_opened_by_overlay && physical_escape_pressed)
    {
        m_game_menu_opened_by_overlay = false;
    }
    if (!menu_open && m_game_menu_opened_by_overlay && target_is_active)
    {
        static_cast<void>(tap_escape());
        m_game_menu_opened_by_overlay = false;
    }

    set_menu_hit_testing(false);
    if (m_input_router_active)
    {
        m_input_router.set_capture(capture, capture ? foreground_root : nullptr);
    }
}

void overlay_t::set_menu_hit_testing(const bool enabled)
{
    if (m_menu_hit_testing == enabled || !m_hwnd)
        return;

    const auto style = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    const auto no_activate_style = style | static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
    const auto desired = enabled ? no_activate_style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT)
                                 : no_activate_style | static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    if (enabled)
        overlay_t::s_menu_hit_testing.store(true, std::memory_order_release);

    bool applied = true;
    if (desired != style)
    {
        ::SetLastError(ERROR_SUCCESS);
        const auto previous = ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, desired);
        applied = previous != 0 || ::GetLastError() == ERROR_SUCCESS;
    }

    if (!enabled || !applied)
        overlay_t::s_menu_hit_testing.store(false, std::memory_order_release);
    m_menu_hit_testing = enabled && applied;
    if (m_menu_hit_testing)
    {
        ::PostMessageW(m_hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(m_hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }
}

UINT_PTR CALLBACK overlay_t::config_dialog_hook(const HWND hwnd, const UINT msg, const WPARAM, const LPARAM lp)
{
    auto* self = reinterpret_cast<overlay_t*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_INITDIALOG)
    {
        const auto* ofn = reinterpret_cast<const OPENFILENAMEW*>(lp);
        self = ofn ? reinterpret_cast<overlay_t*>(ofn->lCustData) : nullptr;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self)
        {
            const auto dialog = ::GetParent(hwnd) ? ::GetParent(hwnd) : hwnd;
            self->m_cfg_dialog_hwnd.store(dialog, std::memory_order_release);
            if (self->m_cfg_dialog_cancel.load(std::memory_order_acquire))
                ::PostMessageW(dialog, WM_CLOSE, 0, 0);
        }
    }
    else if (msg == WM_NCDESTROY && self)
    {
        const auto dialog = ::GetParent(hwnd) ? ::GetParent(hwnd) : hwnd;
        auto expected = dialog;
        self->m_cfg_dialog_hwnd.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    return 0;
}

void overlay_t::stop_config_dialog() noexcept
{
    m_cfg_dialog_cancel.store(true, std::memory_order_release);
    if (m_cfg_dialog_thread.joinable())
    {
        m_cfg_dialog_thread.request_stop();
        if (const auto dialog = m_cfg_dialog_hwnd.load(std::memory_order_acquire))
        {
            ::PostMessageW(dialog, WM_CLOSE, 0, 0);
        }
        if (const auto thread_id = m_cfg_dialog_thread_id.load(std::memory_order_acquire))
        {
            ::EnumThreadWindows(
                thread_id,
                [](const HWND window, LPARAM) -> BOOL
                {
                    ::PostMessageW(window, WM_CLOSE, 0, 0);
                    return TRUE;
                },
                0);
        }
        m_cfg_dialog_thread.join();
    }
    m_cfg_dialog_hwnd.store(nullptr, std::memory_order_release);
    m_cfg_dialog_thread_id.store(0, std::memory_order_release);
    overlay_t::s_modal_active.store(false, std::memory_order_release);
}

namespace
{
// Shows the modern folder picker on the calling (dialog) thread. Empty when cancelled.
[[nodiscard]] std::optional<std::wstring> pick_folder(const HWND owner, const wchar_t* title)
{
    const auto initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized))
        return std::nullopt;
    const foundation::scope_exit uninitialize{[]() noexcept { ::CoUninitialize(); }};
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog{};
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                  FOS_NOCHANGEDIR | FOS_DONTADDTORECENT)))
        return std::nullopt;
    static_cast<void>(dialog->SetTitle(title));
    if (FAILED(dialog->Show(owner)))
        return std::nullopt;
    Microsoft::WRL::ComPtr<IShellItem> item{};
    PWSTR chosen{};
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &chosen)) || !chosen)
        return std::nullopt;
    std::wstring result{chosen};
    ::CoTaskMemFree(chosen);
    return result;
}
} // namespace

void overlay_t::request_file(const file_request kind)
{
    if (overlay_t::s_modal_active.exchange(true))
        return;
    if (m_cfg_dialog_thread.joinable())
        m_cfg_dialog_thread.join();
    m_cfg_dialog_cancel.store(false, std::memory_order_release);

    m_cfg_dialog_thread = std::jthread(
        [this, kind](const std::stop_token stop)
        {
            m_cfg_dialog_thread_id.store(::GetCurrentThreadId(), std::memory_order_release);
            const foundation::scope_exit finished{[this]() noexcept
                                                  {
                                                      m_cfg_dialog_hwnd.store(nullptr, std::memory_order_release);
                                                      m_cfg_dialog_thread_id.store(0, std::memory_order_release);
                                                      overlay_t::s_modal_active.store(false, std::memory_order_release);
                                                  }};
            if (stop.stop_requested())
                return;

            std::optional<std::wstring> chosen{};
            if (kind == file_request::choose_config_folder)
            {
                chosen = pick_folder(m_hwnd, L"Choose a folder for Vesta configs");
            }
            else
            {
                std::wstring path(32768, L'\0');
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = m_hwnd;
                ofn.lpstrFile = path.data();
                ofn.nMaxFile = static_cast<DWORD>(path.size());
                ofn.lCustData = reinterpret_cast<LPARAM>(this);
                ofn.lpfnHook = &overlay_t::config_dialog_hook;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ENABLEHOOK |
                            OFN_DONTADDTORECENT;
                if (kind == file_request::import_lua)
                {
                    ofn.lpstrFilter = L"Lua scripts (*.lua)\0*.lua\0";
                    ofn.lpstrTitle = L"Import Lua script into Vesta";
                }
                else
                {
                    ofn.lpstrFilter = L"Vesta config (*.cfg)\0*.cfg\0";
                    ofn.lpstrTitle = L"Import a Vesta config";
                }
                if (::GetOpenFileNameW(&ofn))
                    chosen = std::wstring{path.c_str()};
            }

            if (!chosen || stop.stop_requested() || m_cfg_dialog_cancel.load(std::memory_order_acquire))
                return;
            if (auto utf8 = utf8_from_wide(chosen->c_str()))
            {
                std::lock_guard<std::mutex> lock(m_cfg_mutex);
                m_cfg_path = std::move(*utf8);
                m_cfg_request = kind;
                m_cfg_ready.store(true, std::memory_order_release);
            }
        });
}

bool overlay_t::open_composition_backend()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device{};
    ComPtr<ID3D11DeviceContext> context{};
    if (FAILED(ui::create_overlay_device(::MonitorFromWindow(m_window_tracker.target(), MONITOR_DEFAULTTOPRIMARY),
                                         &device, &context, true)))
        return false;

    ComPtr<IDXGIDevice> dxgi_device{};
    ComPtr<IDXGIAdapter> adapter{};
    ComPtr<IDXGIFactory2> factory{};
    if (FAILED(device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        return false;
    }
    m_allow_tearing = false;
    ComPtr<IDXGIFactory5> factory5{};
    BOOL tearing_supported = FALSE;
    if (SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_supported,
                                                sizeof(tearing_supported))))
    {
        m_allow_tearing = tearing_supported == TRUE;
    }
    m_present_tearing_enabled = m_allow_tearing;

    ComPtr<IDCompositionDevice> composition_device{};
    if (FAILED(::DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(&composition_device))))
    {
        return false;
    }

    m_device = device.Detach();
    m_context = context.Detach();
    m_composition_device = composition_device.Detach();
    return true;
}

bool overlay_t::open_composition_swap_chain()
{
    if (m_swap_chain)
        return true;
    if (!m_device || !m_context || m_render_width < 64 || m_render_height < 64)
    {
        return false;
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIDevice> dxgi_device{};
    ComPtr<IDXGIAdapter> adapter{};
    ComPtr<IDXGIFactory2> factory{};
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        return false;
    }

    // A slow signal changes pacing, not the swapchain model.
    const bool waitable = true;
    auto description =
        ui::composition_description(m_render_width, m_render_height, m_allow_tearing && waitable, waitable);
    if (!waitable)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice1> device1;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&device1))))
            device1->SetMaximumFrameLatency(2);
    }

    ComPtr<IDXGISwapChain1> swap_chain1{};
    ComPtr<IDXGISwapChain2> swap_chain2{};
    ComPtr<IDXGISwapChain> swap_chain{};
    ComPtr<ID3D11Texture2D> back_buffer{};
    ComPtr<ID3D11RenderTargetView> render_target{};
    auto creation = factory->CreateSwapChainForComposition(m_device, &description, nullptr, &swap_chain1);
    if (FAILED(creation) && (description.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
    {
        swap_chain1.Reset();
        description.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        creation = factory->CreateSwapChainForComposition(m_device, &description, nullptr, &swap_chain1);
        if (SUCCEEDED(creation))
            m_allow_tearing = false;
    }
    if (FAILED(creation))
        write_overlay_lifecycle_event(diagnostics::event::presentation_create_failed, creation);
    if (FAILED(creation) ||
        (waitable && (FAILED(swap_chain1.As(&swap_chain2)) || FAILED(swap_chain2->SetMaximumFrameLatency(1)))) ||
        FAILED(swap_chain1.As(&swap_chain)) || FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) ||
        FAILED(m_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target)))
    {
        return false;
    }

    const auto waitable_handle = waitable ? swap_chain2->GetFrameLatencyWaitableObject() : nullptr;
    if (waitable && !waitable_handle)
        return false;

    m_swap_chain = swap_chain.Detach();
    m_back_buffer = back_buffer.Detach();
    m_rtv = render_target.Detach();
    m_frame_latency_waitable = waitable_handle;
    m_present_tearing_enabled = m_allow_tearing && waitable;

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_render_width);
    viewport.Height = static_cast<float>(m_render_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    write_overlay_lifecycle_event(diagnostics::event::presentation_source_created);
    return true;
}

void overlay_t::close_composition_swap_chain() noexcept
{
    if (!m_composition_active)
        return;
    if (m_context)
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    if (m_rtv)
    {
        m_rtv->Release();
        m_rtv = nullptr;
    }
    if (m_back_buffer)
    {
        m_back_buffer->Release();
        m_back_buffer = nullptr;
    }
    if (m_frame_latency_waitable)
    {
        ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
    }
    if (m_swap_chain)
    {
        m_swap_chain->Release();
        m_swap_chain = nullptr;
        write_overlay_lifecycle_event(diagnostics::event::presentation_source_destroyed);
    }
}

bool overlay_t::attach_presentation()
{
    if (m_presentation_attached)
        return true;
    if (!m_hwnd || !m_window_tracker.visible())
    {
        return false;
    }
    if (!m_composition_active)
    {
        // The HWND-bound swap chain is itself the presentation attachment. It is
        // created only after readiness and destroyed on every detach.
        const auto style = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, style & ~static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP));
        m_window_tracker.place_overlay();
        if (!open_swap_chain_backend())
        {
            ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, style | static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP));
            return false;
        }
        m_presentation_attached = true;
        ++m_presentation_generation;
        const bool should_show = !m_content_suppressed;
        if (should_show)
            ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        m_overlay_visible = should_show;
        write_overlay_lifecycle_event(diagnostics::event::presentation_hwnd_attached);
        return true;
    }
    if (!m_composition_device || !open_composition_swap_chain())
        return false;

    IDCompositionTarget* target{};
    IDCompositionVisual* visual{};
    HRESULT result = m_composition_device->CreateVisual(&visual);
    if (SUCCEEDED(result))
        result = m_composition_device->CreateTargetForHwnd(m_hwnd, TRUE, &target);
    if (SUCCEEDED(result))
        result = visual->SetContent(m_swap_chain);
    if (SUCCEEDED(result))
        result = target->SetRoot(visual);
    if (SUCCEEDED(result))
        result = m_composition_device->Commit();
    if (FAILED(result))
    {
        if (target)
            target->SetRoot(nullptr);
        if (visual)
            visual->SetContent(nullptr);
        if (m_composition_device)
        {
            m_composition_device->Commit();
        }
        if (target)
            target->Release();
        if (visual)
            visual->Release();
        close_composition_swap_chain();
        write_overlay_lifecycle_event(diagnostics::event::presentation_attach_failed, result);
        return false;
    }

    m_composition_target = target;
    m_composition_visual = visual;
    m_presentation_attached = true;
    ++m_presentation_generation;
    m_window_tracker.place_overlay();
    const bool should_show = !m_content_suppressed;
    if (should_show)
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_overlay_visible = should_show;
    write_overlay_lifecycle_event(diagnostics::event::presentation_attached, result);
    return true;
}

void overlay_t::detach_presentation() noexcept
{
    m_combat_input_ready.store(false, std::memory_order_release);
    game::render_poses().set_presentation_state(false, m_window_tracker.refresh_rate());
    if (!m_presentation_attached && !m_composition_target)
        return;
    ++m_presentation_generation;
    if (m_hwnd)
        ::ShowWindow(m_hwnd, SW_HIDE);
    m_overlay_visible = false;
    m_presentation_attached = false;
    if (!m_composition_active)
    {
        if (m_context)
            m_context->OMSetRenderTargets(0, nullptr, nullptr);
        if (m_rtv)
        {
            m_rtv->Release();
            m_rtv = nullptr;
        }
        if (m_back_buffer)
        {
            m_back_buffer->Release();
            m_back_buffer = nullptr;
        }
        if (m_frame_latency_waitable)
        {
            ::CloseHandle(m_frame_latency_waitable);
            m_frame_latency_waitable = nullptr;
        }
        if (m_swap_chain)
        {
            m_swap_chain->Release();
            m_swap_chain = nullptr;
        }
        if (m_hwnd)
        {
            const auto style = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
            ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, style | static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP));
        }
        write_overlay_lifecycle_event(diagnostics::event::presentation_hwnd_detached);
        return;
    }

    HRESULT result = S_OK;
    if (m_composition_target)
        result = m_composition_target->SetRoot(nullptr);
    if (m_composition_visual)
    {
        const auto content_result = m_composition_visual->SetContent(nullptr);
        if (SUCCEEDED(result))
            result = content_result;
    }
    if (m_composition_device)
    {
        const auto commit_result = m_composition_device->Commit();
        if (SUCCEEDED(result))
            result = commit_result;
    }
    write_overlay_lifecycle_event(diagnostics::event::presentation_detached, result);
    if (m_composition_target)
    {
        m_composition_target->Release();
        m_composition_target = nullptr;
    }
    if (m_composition_visual)
    {
        m_composition_visual->Release();
        m_composition_visual = nullptr;
    }
    close_composition_swap_chain();
}

bool overlay_t::open_device_backend()
{
    return SUCCEEDED(ui::create_overlay_device(::MonitorFromWindow(m_window_tracker.target(), MONITOR_DEFAULTTOPRIMARY),
                                               &m_device, &m_context, true));
}

bool overlay_t::open_swap_chain_backend()
{
    if (!m_device || !m_hwnd || m_swap_chain || m_render_width < 64 || m_render_height < 64)
    {
        return false;
    }

    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIDevice> dxgi_device{};
    ComPtr<IDXGIAdapter> adapter{};
    ComPtr<IDXGIFactory2> factory{};
    if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = m_render_width;
    description.Height = m_render_height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc = {1, 0};
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = m_frame_latency_unreliable ? 0u : DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> swap_chain1{};
    if (FAILED(factory->CreateSwapChainForHwnd(m_device, m_hwnd, &description, nullptr, nullptr, &swap_chain1)))
    {
        return false;
    }

    ComPtr<IDXGISwapChain> swap_chain{};
    if (FAILED(swap_chain1.As(&swap_chain)))
        return false;
    m_swap_chain = swap_chain.Detach();
    if (!m_frame_latency_unreliable)
    {
        ComPtr<IDXGISwapChain2> swap_chain2{};
        if (FAILED(swap_chain1.As(&swap_chain2)) || FAILED(swap_chain2->SetMaximumFrameLatency(1)))
        {
            m_swap_chain->Release();
            m_swap_chain = nullptr;
            return false;
        }
        m_frame_latency_waitable = swap_chain2->GetFrameLatencyWaitableObject();
        if (!m_frame_latency_waitable)
        {
            m_swap_chain->Release();
            m_swap_chain = nullptr;
            return false;
        }
    }
    else
    {
        ComPtr<IDXGIDevice1> device1{};
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&device1))))
            device1->SetMaximumFrameLatency(2);
    }
    if (!create_color_target())
    {
        if (m_frame_latency_waitable)
            ::CloseHandle(m_frame_latency_waitable);
        m_frame_latency_waitable = nullptr;
        if (m_rtv)
            m_rtv->Release();
        m_rtv = nullptr;
        if (m_back_buffer)
            m_back_buffer->Release();
        m_back_buffer = nullptr;
        m_swap_chain->Release();
        m_swap_chain = nullptr;
        return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_render_width);
    viewport.Height = static_cast<float>(m_render_height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_present_tearing_enabled = false;
    return true;
}

bool overlay_t::initialize_graphics()
{
    m_composition_active = k_use_composition_backend && open_composition_backend();
    if (!m_composition_active && !open_device_backend())
    {
        return false;
    }

    IDXGIDevice1* dxgi_device1{};
    if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dxgi_device1))))
    {
        dxgi_device1->SetMaximumFrameLatency(1);
        dxgi_device1->Release();
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imgui_context_active = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags = 0;
    io.IniFilename = nullptr;
    // ESP text rebuilds its outline from the neighbouring atlas texels (render/text/esp_text): two texels of
    // padding keep a neighbouring glyph from leaking into the ring.
    io.Fonts->TexGlyphPadding = 2;

    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        return false;
    }
    m_imgui_win32_active = true;
    if (!ImGui_ImplDX11_Init(m_device, m_context))
    {
        return false;
    }
    m_imgui_dx11_active = true;

    if (!load_preview_asset())
    {
        game::bound_memory().log.warning("failed to load embedded CT preview texture.");
    }

    {
        auto* atlas = ImGui::GetIO().Fonts;
        if (!atlas)
        {
            game::bound_memory().log.warning("ImGui font atlas is unavailable.");
            return false;
        }
        m_fonts.notosans_medium_12 = render::draw::add_font_from_memory(
            resources::fonts::notosans_medium, static_cast<int>(sizeof(resources::fonts::notosans_medium)), 12.0f,
            render::draw::font_raster_profile::smooth, atlas->GetGlyphRangesCyrillic());
        m_fonts.esp_text_11 = render::draw::add_font_from_memory(
            resources::fonts::notosans_medium, static_cast<int>(sizeof(resources::fonts::notosans_medium)), 11.0f,
            render::draw::font_raster_profile::esp_text, atlas->GetGlyphRangesCyrillic());

        static constexpr ImWchar no_fallback_preload[]{0};
        if (std::filesystem::exists("C:/Windows/Fonts/seguisym.ttf"))
        {
            if (!render::draw::merge_font_from_file(m_fonts.esp_text_11, "C:/Windows/Fonts/seguisym.ttf", 11.0f,
                                                    render::draw::font_raster_profile::esp_text, no_fallback_preload))
            {
                game::bound_memory().log.warning("failed to merge Segoe UI Symbol ESP fallback.");
            }
        }
        if (std::filesystem::exists("C:/Windows/Fonts/seguiemj.ttf"))
        {
            if (!render::draw::merge_font_from_file(m_fonts.esp_text_11, "C:/Windows/Fonts/seguiemj.ttf", 11.0f,
                                                    render::draw::font_raster_profile::esp_text, no_fallback_preload))
            {
                game::bound_memory().log.warning("failed to merge Segoe UI Emoji ESP fallback.");
            }
        }
        m_fonts.weapons_15 = render::draw::add_font_from_memory(resources::fonts::weapons,
                                                                static_cast<int>(sizeof(resources::fonts::weapons)),
                                                                16.0f, render::draw::font_raster_profile::smooth);
        m_fonts.weapons_esp_15 = render::draw::add_font_from_memory(resources::fonts::weapons,
                                                                    static_cast<int>(sizeof(resources::fonts::weapons)),
                                                                    16.0f, render::draw::font_raster_profile::esp_icon);
        if (m_fonts.weapons_esp_15 && m_fonts.weapons_15)
        {
            m_fonts.weapons_esp_15->plain_im_font = m_fonts.weapons_15->im_font;
        }
        const auto dpi_window = m_window_tracker.target() ? m_window_tracker.target() : m_hwnd;
        m_ui_dpi_scale = std::clamp(static_cast<float>(::GetDpiForWindow(dpi_window)) / 96.0f, 1.0f, 1.5f);
        const auto menu_dpi_scale = m_ui_dpi_scale;
        m_fonts.menu_regular_12 =
            render::draw::add_font_from_file("C:/Windows/Fonts/segoeui.ttf", 16.0f * menu_dpi_scale);
        m_fonts.menu_semibold_13 =
            render::draw::add_font_from_file("C:/Windows/Fonts/seguisb.ttf", 16.0f * menu_dpi_scale);
        m_fonts.menu_brand_30 =
            render::draw::add_font_from_file("C:/Windows/Fonts/segoeuib.ttf", 38.0f * menu_dpi_scale);
        const auto font_ready = [](const render::draw::font* font) { return font && font->im_font; };
        if (!font_ready(m_fonts.notosans_medium_12) || !font_ready(m_fonts.esp_text_11) ||
            !font_ready(m_fonts.weapons_15) || !font_ready(m_fonts.weapons_esp_15) ||
            !font_ready(m_fonts.menu_regular_12) || !font_ready(m_fonts.menu_semibold_13) ||
            !font_ready(m_fonts.menu_brand_30))
        {
            game::bound_memory().log.warning("one or more required ImGui fonts failed to load.");
            return false;
        }
        if (!atlas->Build())
        {
            game::bound_memory().log.warning("failed to build ImGui font atlas.");
            return false;
        }
        if (!ImGui_ImplDX11_CreateDeviceObjects())
        {
            game::bound_memory().log.warning("failed to create ImGui DX11 font/device resources.");
            return false;
        }
    }

    return true;
}

bool overlay_t::create_color_target()
{
    if (FAILED(m_swap_chain->GetBuffer(0, IID_PPV_ARGS(&m_back_buffer))))
    {
        return false;
    }

    if (FAILED(m_device->CreateRenderTargetView(m_back_buffer, nullptr, &m_rtv)))
    {
        m_back_buffer->Release();
        m_back_buffer = nullptr;
        return false;
    }
    return true;
}

bool overlay_t::resize_graphics(const std::uint32_t width, const std::uint32_t height)
{
    if (width < 64 || height < 64)
    {
        return false;
    }
    if (m_composition_active)
    {
        if (m_presentation_attached)
            return false;
        close_composition_swap_chain();
        m_render_width = width;
        m_render_height = height;
        return true;
    }
    if (!m_swap_chain)
        return false;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_context->ClearState();
    m_context->Flush();
    if (m_rtv)
    {
        m_rtv->Release();
        m_rtv = nullptr;
    }
    if (m_back_buffer)
    {
        m_back_buffer->Release();
        m_back_buffer = nullptr;
    }

    if (FAILED(m_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
    {
        create_color_target();
        return false;
    }
    if (!create_color_target())
    {
        return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    m_render_width = width;
    m_render_height = height;
    return true;
}

bool overlay_t::load_preview_asset()
{
    const auto com_result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
    {
        return false;
    }
    const auto uninitialize_com = SUCCEEDED(com_result);

    IWICImagingFactory* factory{};
    IWICStream* stream{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    ID3D11Texture2D* texture{};
    bool loaded{};

    do
    {
        if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            break;
        if (FAILED(factory->CreateStream(&stream)))
            break;
        if (FAILED(stream->InitializeFromMemory(
                const_cast<BYTE*>(reinterpret_cast<const BYTE*>(resources::images::ct_png)),
                static_cast<DWORD>(resources::images::ct_png_size))))
            break;
        if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
            break;
        if (FAILED(decoder->GetFrame(0, &frame)))
            break;
        if (FAILED(factory->CreateFormatConverter(&converter)))
            break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            break;

        UINT width{}, height{};
        if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
            break;
        const auto stride = width * 4u;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
        if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
            break;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial_data{};
        initial_data.pSysMem = pixels.data();
        initial_data.SysMemPitch = stride;
        if (FAILED(m_device->CreateTexture2D(&description, &initial_data, &texture)))
            break;
        if (FAILED(m_device->CreateShaderResourceView(texture, nullptr, &m_ct_preview_texture)))
            break;

        m_ct_preview_width = width;
        m_ct_preview_height = height;
        loaded = true;
    } while (false);

    if (texture)
        texture->Release();
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (uninitialize_com)
        ::CoUninitialize();
    return loaded;
}

ID3D11ShaderResourceView* overlay_t::steam_avatar_texture(const std::uint64_t steamid,
                                                          const std::string_view display_name)
{
    if (!m_device)
        return nullptr;
    const auto avatar_key =
        steamid ? steamid
                : (0x8000000000000000ull |
                   (static_cast<std::uint64_t>(std::hash<std::string_view>{}(display_name) % 15u) + 1u));
    if (const auto found = m_steam_avatars.find(avatar_key); found != m_steam_avatars.end())
        return found->second;
    const auto now = std::chrono::steady_clock::now();
    if (const auto missing = m_missing_steam_avatars.find(avatar_key); missing != m_missing_steam_avatars.end())
    {
        if (now < missing->second)
            return nullptr;
        m_missing_steam_avatars.erase(missing);
    }
    const auto retry_at = now + std::chrono::seconds(30);

    if (!steamid)
    {
        auto& renderer = features::chams::g_renderer;
        if (!renderer.ensure_vpk())
            return nullptr;
        const auto variant = static_cast<unsigned>(avatar_key & 0xffu);
        const auto path = std::format("panorama/images/avatars/avatar_sub_{:02}_psd.vtex_c", variant);
        const auto data = features::chams::load_texture(renderer.vpk(), path);
        if (!data.valid())
        {
            m_missing_steam_avatars[avatar_key] = retry_at;
            return nullptr;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = data.width;
        desc.Height = data.height;
        desc.MipLevels = data.mip_count;
        desc.ArraySize = 1;
        desc.Format = static_cast<DXGI_FORMAT>(data.dxgi_format);
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> subresources(data.mip_count);
        for (std::uint32_t level = 0; level < data.mip_count; ++level)
        {
            const auto width = std::max<std::uint32_t>(1, data.width >> level);
            const auto blocks = std::max<std::uint32_t>(1, (width + 3) / 4);
            subresources[level].pSysMem = data.mips[level].data();
            subresources[level].SysMemPitch = data.block_size ? blocks * data.block_size : width * 4;
        }
        ID3D11Texture2D* texture{};
        ID3D11ShaderResourceView* view{};
        if (FAILED(m_device->CreateTexture2D(&desc, subresources.data(), &texture)) ||
            FAILED(m_device->CreateShaderResourceView(texture, nullptr, &view)))
        {
            if (texture)
                texture->Release();
            m_missing_steam_avatars[avatar_key] = retry_at;
            return nullptr;
        }
        texture->Release();
        m_steam_avatars.emplace(avatar_key, view);
        return view;
    }
    const auto com_result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct com_scope
    {
        bool owned{};
        ~com_scope()
        {
            if (owned)
                ::CoUninitialize();
        }
    } com{SUCCEEDED(com_result)};
    if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE)
        return nullptr;

    wchar_t steam_path[1024]{};
    DWORD steam_path_bytes = sizeof(steam_path);
    HKEY key{};
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS ||
        ::RegQueryValueExW(key, L"SteamPath", nullptr, nullptr, reinterpret_cast<BYTE*>(steam_path),
                           &steam_path_bytes) != ERROR_SUCCESS)
    {
        if (key)
            ::RegCloseKey(key);
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }
    ::RegCloseKey(key);
    const auto base = std::filesystem::path(steam_path) / L"config" / L"avatarcache";
    std::filesystem::path avatar{};
    for (const auto* extension : {L".png", L".jpg", L".jpeg"})
    {
        auto candidate = base / (std::to_wstring(steamid) + extension);
        std::error_code error{};
        if (std::filesystem::is_regular_file(candidate, error))
        {
            avatar = std::move(candidate);
            break;
        }
    }
    if (avatar.empty())
    {
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory{};
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder{};
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame{};
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter{};
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(avatar.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                  &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
    {
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }
    UINT width{}, height{};
    if (FAILED(converter->GetSize(&width, &height)) || !width || !height || width > 1024 || height > 1024)
    {
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }
    const auto stride = width * 4u;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
    {
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = stride;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture{};
    ID3D11ShaderResourceView* view{};
    if (FAILED(m_device->CreateTexture2D(&description, &initial, &texture)) ||
        FAILED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &view)))
    {
        m_missing_steam_avatars[avatar_key] = retry_at;
        return nullptr;
    }
    m_steam_avatars.emplace(avatar_key, view);
    return view;
}

LRESULT CALLBACK overlay_t::window_callback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_DISPLAYCHANGE:
            if (auto* overlay = overlay_t::s_active_instance.load(std::memory_order_acquire))
                overlay->m_window_tracker.notify_display_change();
            return ::DefWindowProcW(hwnd, msg, wp, lp);
        case WM_MOUSEACTIVATE:
            return overlay_t::s_menu_hit_testing.load(std::memory_order_acquire) ? MA_NOACTIVATEANDEAT : MA_NOACTIVATE;
        case WM_NCHITTEST:
            return overlay_t::s_menu_hit_testing.load(std::memory_order_acquire) ? HTCLIENT : HTTRANSPARENT;
        case WM_SETCURSOR:
            if (overlay_t::s_menu_hit_testing.load(std::memory_order_acquire) && LOWORD(lp) == HTCLIENT)
            {
                LPCSTR cursor_id = IDC_ARROW;
                switch (static_cast<ImGuiMouseCursor>(overlay_t::s_menu_cursor.load(std::memory_order_relaxed)))
                {
                    case ImGuiMouseCursor_TextInput:
                        cursor_id = IDC_IBEAM;
                        break;
                    case ImGuiMouseCursor_ResizeAll:
                        cursor_id = IDC_SIZEALL;
                        break;
                    case ImGuiMouseCursor_ResizeNS:
                        cursor_id = IDC_SIZENS;
                        break;
                    case ImGuiMouseCursor_ResizeEW:
                        cursor_id = IDC_SIZEWE;
                        break;
                    case ImGuiMouseCursor_ResizeNESW:
                        cursor_id = IDC_SIZENESW;
                        break;
                    case ImGuiMouseCursor_ResizeNWSE:
                        cursor_id = IDC_SIZENWSE;
                        break;
                    case ImGuiMouseCursor_Hand:
                        cursor_id = IDC_HAND;
                        break;
                    case ImGuiMouseCursor_NotAllowed:
                        cursor_id = IDC_NO;
                        break;
                    default:
                        break;
                }
                ::SetCursor(::LoadCursorA(nullptr, cursor_id));
                return TRUE;
            }
            return ::DefWindowProcW(hwnd, msg, wp, lp);
        case WM_DESTROY:
            overlay_t::s_menu_hit_testing.store(false, std::memory_order_release);
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace ui
