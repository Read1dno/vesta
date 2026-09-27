#include <stdafx.hpp>
#include "test_support.hpp"
#include <app/context.hpp>
#include <app/workers.hpp>
#include <render/overlay/hud.hpp>
#include <render/menu/internal.hpp>
#include <render/chams/renderer.hpp>
#include <render/chams/preview.hpp>
#include <features/visuals/visuals.hpp>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <timeapi.h>

struct menu_render_test_access {
    static bool initialize(overlay_t& o, HWND window, bool hardware) {
        o.m_hwnd=window; o.m_use_gpu=hardware;
        o.m_render_width=1920; o.m_render_height=1080;
        if (!o.initialize_graphics()) return false;
        if (hardware && config::general_settings.use_gpu) {
            o.ensure_gpu_effects();
            if(!o.m_chams_renderer_initialized || !o.m_chams_preview_initialized)return false;
        }
        return true;
    }
    static void toggle_gpu(overlay_t& o) {
        VESTA_CHECK(!o.m_chams_renderer_initialized && !o.m_chams_preview_initialized);
        config::general_settings.use_gpu=true;
        o.ensure_gpu_effects();
        VESTA_CHECK(o.m_chams_renderer_initialized && o.m_chams_preview_initialized);
        VESTA_CHECK(chams::g_renderer.ready());
        config::general_settings.use_gpu=false;
        o.ensure_gpu_effects();
        VESTA_CHECK(o.m_chams_renderer_initialized && o.m_chams_preview_initialized);
        config::general_settings.use_gpu=true;
        o.ensure_gpu_effects();
        VESTA_CHECK(o.m_chams_renderer_initialized && o.m_chams_preview_initialized);
        std::cout << "gpu_runtime_toggle=PASS\n";
    }
    static void presentation(overlay_t& o) {
        VESTA_CHECK(o.open_composition_swap_chain());
        VESTA_CHECK(SUCCEEDED(o.m_composition_device->CreateVisual(&o.m_composition_visual)));
        VESTA_CHECK(SUCCEEDED(o.m_composition_device->CreateTargetForHwnd(o.m_hwnd,TRUE,&o.m_composition_target)));
        VESTA_CHECK(SUCCEEDED(o.m_composition_visual->SetContent(o.m_swap_chain)));
        VESTA_CHECK(SUCCEEDED(o.m_composition_target->SetRoot(o.m_composition_visual)));
        VESTA_CHECK(SUCCEEDED(o.m_composition_device->Commit()));
        o.m_presentation_attached=true;
    }
    static HRESULT present(overlay_t& o) {
        if(o.m_frame_latency_waitable) WaitForSingleObject(o.m_frame_latency_waitable,50);
        return o.m_swap_chain->Present(0,DXGI_PRESENT_DO_NOT_WAIT |
            (o.m_present_tearing_enabled?DXGI_PRESENT_ALLOW_TEARING:0));
    }
    static void frame(menu_t& m, unsigned n, bool visuals) {
        m.m_open.store(visuals || n%31 != 30);
        m.m_page=visuals?2:(n/12)%4;
        m.m_visual_group=0;
        m.m_misc_group=(n/48)%4;
        m.draw();
    }
    static void cleanup(overlay_t& o) {
        render::menu::detail::g_popup_blur.release_all();
        o.shutdown();
    }
};

int main(int argc, char** argv) {
    timeBeginPeriod(1);
    ::SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const bool visuals=argc>3 && std::string_view(argv[3])=="visuals";
    const bool debug_layer=GetEnvironmentVariableW(L"VESTA_TEST_NO_D3D_DEBUG",nullptr,0)==0;
    const auto mode=argc>1?std::string_view(argv[1]):std::string_view("hardware");
    const bool visible=mode=="present-visible";
    const bool full=mode=="present-full" || visible;
    const bool map_benchmark=mode=="map-benchmark";
    const bool live=mode=="live" || mode=="present-live" || full || map_benchmark;
    const bool presenting=mode.starts_with("present");
    const unsigned frame_count=argc>2?static_cast<unsigned>(std::stoi(argv[2])):240;
    const bool hardware=argc>1 && std::string_view(argv[1])!="warp";
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    config::apply_default_config();
    if(live) {
        VESTA_CHECK(app::context().process.attach(L"cs2.exe"));
        VESTA_CHECK(app::context().modules.discover(app::context().process));
        VESTA_CHECK(app::context().addresses.initialize());
        VESTA_CHECK(game::fields().initialize());
        config::storage.read_cache();
        config::general_settings.auto_accept=false;
        config::publish_runtime_snapshot();
        std::cout<<"LIVE READ ONLY: game memory reads; no input gateway or hooks"<<std::endl;
    }
    if(map_benchmark) {
        const auto began=std::chrono::steady_clock::now();
        game::collision_world map{};
        const bool ready=map.build_from_map_file("maps/de_inferno.vpk");
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-began).count();
        std::cout << "inferno_static_bvh ready=" << ready
            << " triangles=" << map.count() << " elapsed_ms=" << elapsed << '\n';
        return ready && map.valid() ? 0 : 1;
    }
    config::general_settings.use_gpu=hardware && mode!="toggle-gpu";
    config::general_settings.auto_accept=false;
    if(full) std::thread(app::workers::game).detach();
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"Vesta.Menu.Render.Test";
    VESTA_CHECK(RegisterClassW(&wc));
    HWND window=CreateWindowExW(visible?(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP):0,
        wc.lpszClassName,L"Vesta diagnostic menu test",visible?WS_POPUP:WS_OVERLAPPEDWINDOW,
        0,0,1920,1080,nullptr,nullptr,instance,nullptr);
    VESTA_CHECK(window);
    auto& o=app::context().overlay;
    VESTA_CHECK(menu_render_test_access::initialize(o,window,hardware));
    if(mode=="toggle-gpu") menu_render_test_access::toggle_gpu(o);
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11InfoQueue> queue;
    if(debug_layer) VESTA_CHECK(SUCCEEDED(o.device()->QueryInterface(IID_PPV_ARGS(&queue))));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=1920;desc.Height=1080;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;ComPtr<ID3D11RenderTargetView> rtv;
    VESTA_CHECK(SUCCEEDED(o.device()->CreateTexture2D(&desc,nullptr,&target)));
    VESTA_CHECK(SUCCEEDED(o.device()->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    // Offscreen rendering; no input gateway or application workers.
    if(presenting) menu_render_test_access::presentation(o);
    if(visible) {
        SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);
        ShowWindow(window,SW_SHOWNOACTIVATE);
        SetWindowPos(window,HWND_TOPMOST,0,0,1920,1080,SWP_NOACTIVATE);
    }
    unsigned errors{},warnings{},frames{},preview_frames{},presented{},busy{};
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<frame_count;++n) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
        const float clear[4]{};auto* raw=presenting?o.rtv():rtv.Get();
        o.context()->OMSetRenderTargets(1,&raw,nullptr);
        o.context()->ClearRenderTargetView(raw,clear);
        ImGui_ImplDX11_NewFrame();ImGui::GetIO().DisplaySize={1920,1080};ImGui::GetIO().DeltaTime=1.f/120;
        ImGui::NewFrame();
        chams::g_renderer.begin_2d_bloom_frame(hardware);
        if(live) {
            if(!full) {
                game::local_player().update();
                game::entity_index().refresh();
                game::world().run();
            }
            game::presentation_camera_sample camera{};
            if(game::camera().sample_presentation(camera))
                game::camera().begin_presentation_frame(camera,1920,1080);
            auto pose=std::make_shared<game::player_pose_frame>();
            pose->world=game::world().players();
            pose->camera=camera;
            for(std::size_t i=0;i<pose->world->size();++i) {
                const auto& player=(*pose->world)[i];
                if(player.health<=0 || !player.bone_cache)continue;
                pose->players.push_back({i,player.pawn,player.bone_cache,player.model_path,
                    game::skeletons().get(player.bone_cache),false});
            }
            zdraw::draw_list actual_esp{ImGui::GetBackgroundDrawList()};
            std::shared_ptr<const game::player_pose_frame> presentation_pose=pose;
            if(full) {
                game::render_poses().set_presentation_state(true,240);
                presentation_pose=game::render_poses().acquire_for_presentation();
                chams::g_renderer.render_world_effects(raw,1920,1080);
            }
            chams::g_renderer.render_frame(raw,1920,1080,presentation_pose);
            features::visuals::player().render(actual_esp,presentation_pose);
            if(full) {
                features::visuals::sound().on_render(actual_esp);
                features::visuals::items().on_render(actual_esp);
                features::visuals::projectiles().on_render(actual_esp);
                features::visuals::bomb().on_render(actual_esp);
                features::visuals::radar().on_render(actual_esp);
                features::visuals::crosshair().on_render(actual_esp);
                render::hud::draw_watermark(true);
                render::hud::draw_spectator_list(true);
                render::hud::draw_event_log(true);
                render::hud::draw_keybind_list(true);
                render::hud::draw_bomb_info(true);
            }
            if(n%60==0)std::cout<<"LIVE frame="<<n<<" players="<<pose->players.size()<<std::endl;
        }
        zdraw::draw_list esp{ImGui::GetBackgroundDrawList()};
        for(unsigned player=0;!visuals && player<32;++player) {
            const float x=20.f+player*50.f, y=20.f+float(n%17);
            esp.add_rect(x,y,32,80,{255,255,255,255});
            auto font=*o.fonts().esp_text_11;
            font.font_size=9.f+float((n+player)%20);
            esp.add_text(x,y+85,"ESP АБВ Player ★",&font,{255,255,255,255},zdraw::text_style::outlined);
            chams::g_renderer.add_2d_bloom_segment(x,y,x+25,y+35,2,4,{255,0,0,200});
        }
        if(!visuals) config::general_settings.menu_scale=std::array{0.5f,0.75f,1.f,1.25f,1.5f}[(n/48)%5];
        if(!visuals) render::localization::set(n%2 ? render::localization::id::ru : render::localization::id::en);
        menu_render_test_access::frame(app::context().menu,n,visuals);
        if(chams::g_preview.current_mesh()) ++preview_frames;
        if(!visuals && n%5==0) {
            auto* dl=ImGui::GetForegroundDrawList();
            render::menu::detail::draw_popup_surface(dl,{300,200},{650,500},12);
        }
        ImGui::Render();ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if(hardware && n%31==30)chams::g_renderer.render_2d_bloom(raw,1920,1080);
        if(presenting) {
            const auto result=menu_render_test_access::present(o);
            if(result==DXGI_ERROR_WAS_STILL_DRAWING)++busy;
            else if(SUCCEEDED(result))++presented;
            else {std::cout<<"PRESENT failed="<<std::hex<<result<<std::dec<<std::endl;++errors;}
        }
        o.context()->Flush();
        for(UINT64 i=0;queue && i<queue->GetNumStoredMessages();++i) {
            SIZE_T length{};queue->GetMessage(i,nullptr,&length);std::vector<char> bytes(length);
            auto* msg=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,msg,&length);
            if(msg->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) {
                if(msg->Severity<=D3D11_MESSAGE_SEVERITY_ERROR)++errors;else ++warnings;
                std::cout<<"D3D11 "<<msg->Severity<<": "<<msg->pDescription<<std::endl;
            }
        }
        if(queue)queue->ClearStoredMessages();++frames;
        if(errors)break;
        Sleep(2);
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"menu_render "<<(hardware?"hardware":"warp")<<": frames="<<frames<<" errors="<<errors<<" warnings="<<warnings<<" preview_frames="<<preview_frames<<" presented="<<presented<<" busy="<<busy<<" elapsed_ms="<<elapsed<<std::endl;
    rtv.Reset();target.Reset();queue.Reset();
    menu_render_test_access::cleanup(o);
    UnregisterClassW(wc.lpszClassName,instance);
    timeEndPeriod(1);
    if(full) ::ExitProcess(errors ? 1:0);
    return errors ? 1:0;
}
