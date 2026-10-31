#pragma once
#include <ui/overlay/hud.hpp>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <dxgi1_5.h>
#include <dcomp.h>
#include <render/draw.hpp>
#include <render/fonts.hpp>
#include <ui/overlay/window.hpp>
#include <ui/overlay/recovery.hpp>
#include <ui/overlay/input.hpp>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <foundation/scope_exit.hpp>
#include <foundation/frame_schedule.hpp>
#include <ui/overlay/present_policy.hpp>
#include <diagnostics/rate_gate.hpp>
#include <diagnostics/events.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace ui
{
class overlay_t
{
public:
    using loaded_fonts = render::font_set;

    ID3D11Device* device() const { return m_device; }
    ID3D11DeviceContext* context() const { return m_context; }

    ID3D11RenderTargetView* rtv() const { return m_rtv; }
    ID3D11DepthStencilView* dsv() const { return m_dsv; }
    ID3D11ShaderResourceView* ct_preview_texture() const { return m_ct_preview_texture; }
    ID3D11ShaderResourceView* steam_avatar_texture(std::uint64_t steamid, std::string_view display_name = {});

    bool launch(foundation::shutdown_action quiesce = {});
    [[nodiscard]] bool gpu_effects_enabled() const noexcept;
    void request_shutdown() noexcept
    {
        m_shutdown_requested.store(true, std::memory_order_release);

        if (const auto thread = m_render_thread_id.load(std::memory_order_acquire))
        {
            ::PostThreadMessageW(thread, WM_NULL, 0, 0);
        }
    }

    [[nodiscard]] const loaded_fonts& fonts() const { return m_fonts; }
    [[nodiscard]] float ui_dpi_scale() const noexcept { return m_ui_dpi_scale; }
    [[nodiscard]] std::uint32_t ui_reference_width() const noexcept { return m_ui_reference_width; }
    [[nodiscard]] std::uint32_t ui_reference_height() const noexcept { return m_ui_reference_height; }
    [[nodiscard]] bool ui_uses_fullscreen_canvas() const noexcept { return m_ui_fullscreen_canvas; }
    [[nodiscard]] HWND hwnd() const { return m_hwnd; }
    // The game is in front and in play. A game screen over the world (pause menu, team select, buy menu, console,
    // popups) shows the system cursor, which the game hides in play: aim, triggers and scripted input stay off then.
    [[nodiscard]] bool combat_input_ready() const noexcept
    {
        if (!m_combat_input_ready.load(std::memory_order_acquire))
            return false;
        CURSORINFO cursor{};
        cursor.cbSize = sizeof(cursor);
        return !(::GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr);
    }
    [[nodiscard]] explicit operator bool() const { return m_hwnd != nullptr; }

    static inline std::atomic<bool> s_modal_active{false};

    // What a file dialog started from the menu is for. The dialog runs on its own thread; the render thread carries
    // out the choice at the start of a frame.
    enum class file_request : std::uint8_t
    {
        import_config,
        choose_config_folder,
        import_lua,
    };
    void request_file(file_request kind);

private:
    friend struct menu_render_test_access;
    void ensure_gpu_effects();

    // Render loop (render thread).
    enum class present_outcome : std::uint8_t
    {
        presented,
        dropped,    // the compositor was still busy; the frame is discarded
        recovering, // the surface is rebuilt before the next frame
        failed
    };
    bool run();
    bool pump_messages();
    bool wait_for_signal(HANDLE signal, std::uint64_t observed_transition);
    void release_input_for_exit();
    void update_frame_state();
    void apply_pending_file_request();
    bool pace_frame(foundation::frame_schedule& clock, HANDLE timer);
    void build_frame();
    void draw_world(render::draw::draw_list& draw_list);
    void submit_frame();
    present_outcome present_frame(ui::present_recovery& recovery);
    void note_present_outcome(bool dropped);
    void enter_latency_fallback(diagnostics::event reason);
    void shutdown() noexcept;
    void synchronize_menu_focus();
    void set_menu_hit_testing(bool enabled);
    void synchronize_content_visibility();
    void apply_capture_policy();
    void apply_resource_priority();
    void synchronize_window_bounds();
    void route_overlay_input();
    bool initialize_graphics();
    bool open_device_backend();
    bool open_composition_backend();
    bool open_composition_swap_chain();
    void close_composition_swap_chain() noexcept;
    bool open_swap_chain_backend();
    bool attach_presentation();
    void detach_presentation() noexcept;
    bool resize_graphics(std::uint32_t width, std::uint32_t height);
    bool create_color_target();
    bool load_preview_asset();
    bool register_overlay_class();
    void stop_config_dialog() noexcept;

    static LRESULT CALLBACK window_callback(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static UINT_PTR CALLBACK config_dialog_hook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static inline std::atomic<bool> s_menu_hit_testing{};
    static inline std::atomic<int> s_menu_cursor{};
    static inline std::atomic<overlay_t*> s_active_instance{};

    std::atomic<bool> m_cfg_ready{false};
    bool m_config_dirty{};
    std::chrono::steady_clock::time_point m_config_changed_at{};
    std::mutex m_cfg_mutex{};
    std::string m_cfg_path{};
    file_request m_cfg_request{file_request::import_config};
    std::jthread m_cfg_dialog_thread{};
    std::atomic<HWND> m_cfg_dialog_hwnd{};
    std::atomic<DWORD> m_cfg_dialog_thread_id{};
    std::atomic<bool> m_cfg_dialog_cancel{};

    HWND m_hwnd{};
    ATOM m_atom{};
    bool m_input_visible{false};
    bool m_obs_bypass_observed{};
    bool m_obs_bypass_initialized{};
    // Misc > Engine priority levels as applied (-1: not yet), and the game's CPU class they were planned against.
    int m_cpu_priority_applied{-1};
    int m_gpu_priority_applied{-1};
    int m_game_priority_rank{-1};
    std::chrono::steady_clock::time_point m_next_priority_check{};
    bool m_overlay_visible{};
    bool m_content_suppressed{};
    bool m_window_tracking_active{};
    bool m_window_in_band{}; // the overlay HWND lives in the SYSTEM_TOOLS band (see window_band.hpp)
    bool m_input_router_active{};
    bool m_menu_interactive{};
    bool m_menu_hit_testing{};
    bool m_game_menu_opened_by_overlay{};
    bool m_escape_was_down{};
    bool m_imgui_context_active{};
    bool m_imgui_win32_active{};
    bool m_imgui_dx11_active{};
    bool m_chams_renderer_initialized{};
    bool m_chams_preview_initialized{};
    std::atomic<bool> m_shutdown_requested{};
    std::atomic<bool> m_shutdown_active{};
    std::atomic<bool> m_combat_input_ready{};
    std::atomic<DWORD> m_render_thread_id{};
    std::array<bool, 256> m_overlay_key_states{};
    ui::hud::panels m_hud_panels{};

    ID3D11Device* m_device{};
    ID3D11DeviceContext* m_context{};
    IDXGISwapChain* m_swap_chain{};
    IDCompositionDevice* m_composition_device{};
    IDCompositionTarget* m_composition_target{};
    IDCompositionVisual* m_composition_visual{};
    HANDLE m_frame_latency_waitable{};
    bool m_composition_active{};
    bool m_presentation_attached{};
    bool m_allow_tearing{};
    bool m_present_tearing_enabled{};
    bool m_resize_pending{};
    // Timer pacing while the swap chain's latency signal is silent (see enter_latency_fallback); probed again later.
    bool m_frame_latency_unreliable{};
    bool m_nonwaitable_stall_reported{};
    bool m_latency_fallback_reported{};
    static constexpr int k_latency_timeouts_for_fallback = 6; // consecutive 50 ms timeouts
    int m_latency_timeouts{};
    std::chrono::steady_clock::time_point m_latency_probe_at{};
    // Frames presented and dropped in the current two-second window; a starved window is journaled.
    std::uint32_t m_window_presented{};
    std::uint32_t m_window_dropped{};
    std::chrono::steady_clock::time_point m_present_window_start{};
    diagnostics::rate_gate m_starved_note{std::chrono::seconds(30)};
    bool m_use_gpu{true};
    ui::presentation_retry m_presentation_retry{};
    std::uint64_t m_presentation_generation{};
    ID3D11Texture2D* m_back_buffer{};
    ID3D11RenderTargetView* m_rtv{};
    ID3D11DepthStencilView* m_dsv{};
    ID3D11ShaderResourceView* m_ct_preview_texture{};
    std::unordered_map<std::uint64_t, ID3D11ShaderResourceView*> m_steam_avatars{};
    std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> m_missing_steam_avatars{};
    std::uint32_t m_ct_preview_width{};
    std::uint32_t m_ct_preview_height{};
    std::uint32_t m_render_width{};
    std::uint32_t m_render_height{};
    float m_ui_dpi_scale{1.0f};
    std::uint32_t m_ui_reference_width{};
    std::uint32_t m_ui_reference_height{};
    bool m_ui_fullscreen_canvas{};

    loaded_fonts m_fonts{};
    window_tracker m_window_tracker{};
    input_router m_input_router{};

    static constexpr const wchar_t* k_class_name{L"vesta.overlay"};
};
} // namespace ui
