#include "test_support.hpp"
#include <render/overlay/graphics_device.hpp>
#include <render/menu/card_metrics.hpp>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <cstdint>

int main()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    VESTA_CHECK(SUCCEEDED(render::create_overlay_device(nullptr,&device,&context,false)));
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter1> adapter1;
    VESTA_CHECK(SUCCEEDED(device.As(&dxgi)));
    VESTA_CHECK(SUCCEEDED(dxgi->GetAdapter(&adapter)));
    VESTA_CHECK(SUCCEEDED(adapter.As(&adapter1)));
    DXGI_ADAPTER_DESC1 adapter_desc{};
    VESTA_CHECK(SUCCEEDED(adapter1->GetDesc1(&adapter_desc)));
    VESTA_CHECK(adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=256;desc.Height=128;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;
    ComPtr<ID3D11RenderTargetView> rtv;
    VESTA_CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    VESTA_CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    VESTA_CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={256,128};io.DeltaTime=1.f/60;
    VESTA_CHECK(io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",16,nullptr,io.Fonts->GetGlyphRangesCyrillic()) != nullptr);
    VESTA_CHECK(ImGui_ImplDX11_Init(device.Get(),context.Get()));
    ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
    auto* draw=ImGui::GetBackgroundDrawList();
    draw->AddRect({10,10},{110,110},IM_COL32(255,255,255,255),0,0,2);
    draw->AddLine({60,30},{60,90},IM_COL32(255,255,255,255),2);
    draw->AddText({125,20},IM_COL32(255,255,255,255),"ESP CPU");
    unsigned layout_cases{};
    for(const char* text : {
        "Restart to change the renderer. Off keeps boxes, skeletons and text on CPU; chams, no flash, no smoke and bloom are disabled. GPU mode may cause ESP stutter on weak PCs.",
        "Смена рендера требует перезапуска. Выключено: боксы, скелеты и текст работают на CPU; chams, no flash, no smoke и bloom отключены. На слабых ПК режим GPU может вызывать рывки ESP."}) {
        for(float size : {16.f,24.f,32.f}) {
            ImGui::PushFont(nullptr,size);
            for(float width : {240.f,281.f,320.f}) {
                const auto height=ImGui::CalcTextSize(text,nullptr,false,width).y;
                const auto rows=render::menu::wrapped_text_rows(height,ImGui::GetStyle().ItemSpacing.y,42);
                VESTA_CHECK(rows*42 >= height+ImGui::GetStyle().ItemSpacing.y+8);
                ++layout_cases;
            }
            ImGui::PopFont();
        }
    }
    ImGui::Render();
    const float clear[4]{};
    context->ClearRenderTargetView(rtv.Get(),clear);
    auto* raw=rtv.Get();context->OMSetRenderTargets(1,&raw,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context->CopyResource(readback.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE pixels{};
    VESTA_CHECK(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&pixels)));
    unsigned box_pixels{},text_pixels{};
    for(unsigned y=0;y<128;++y) {
        const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(pixels.pData)+y*pixels.RowPitch);
        for(unsigned x=0;x<256;++x) if(row[x]&0x00ffffff) { if(x<120)++box_pixels;else ++text_pixels; }
    }
    context->Unmap(readback.Get(),0);
    VESTA_CHECK(box_pixels>400);VESTA_CHECK(text_pixels>50);
    ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
    std::cout << "cpu_esp_smoke: WARP box/skeleton pixels=" << box_pixels << " text pixels=" << text_pixels << " layout cases=" << layout_cases << " PASS\n";
}
