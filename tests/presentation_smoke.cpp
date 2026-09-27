#include "test_support.hpp"
#include <render/overlay/graphics_device.hpp>
#include <dcomp.h>

int main()
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const auto monitor = ::MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    VESTA_CHECK(SUCCEEDED(render::create_overlay_device(monitor, &device, &context)));
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    VESTA_CHECK(SUCCEEDED(device.As(&dxgi)));
    VESTA_CHECK(SUCCEEDED(render::set_overlay_gpu_priority(device.Get())));
    INT priority{};
    VESTA_CHECK(SUCCEEDED(dxgi->GetGPUThreadPriority(&priority)));
    VESTA_CHECK(priority == -2);
    std::cout << "gpu_priority=-2 PASS\n";
    VESTA_CHECK(SUCCEEDED(dxgi->GetAdapter(&adapter)));
    VESTA_CHECK(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
    DXGI_ADAPTER_DESC hardware{};
    VESTA_CHECK(SUCCEEDED(adapter->GetDesc(&hardware)));
    std::wcout << L"adapter=" << hardware.Description << L"\n";
    ComPtr<IDCompositionDevice> composition;
    VESTA_CHECK(SUCCEEDED(::DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition))));
    for (auto size : {64u, 128u, 640u})
    {
        auto desc = render::composition_description(size, size, false);
        VESTA_CHECK(desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
        ComPtr<IDXGISwapChain1> chain;
        VESTA_CHECK(SUCCEEDED(factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &chain)));
        ComPtr<IDXGISwapChain2> chain2;
        VESTA_CHECK(SUCCEEDED(chain.As(&chain2)));
        VESTA_CHECK(SUCCEEDED(chain2->SetMaximumFrameLatency(1)));
        const auto event = chain2->GetFrameLatencyWaitableObject();
        VESTA_CHECK(event != nullptr);
        VESTA_CHECK(::CloseHandle(event));
    }
    std::cout << "composition_create_destroy=3 PASS\\n";
    ComPtr<ID3D11Device> software_device;
    ComPtr<ID3D11DeviceContext> software_context;
    VESTA_CHECK(SUCCEEDED(render::create_overlay_device(
        monitor, &software_device, &software_context, false)));
    ComPtr<IDXGIDevice> software_dxgi;
    ComPtr<IDXGIAdapter> software_adapter;
    ComPtr<IDXGIFactory2> software_factory;
    VESTA_CHECK(SUCCEEDED(software_device.As(&software_dxgi)));
    VESTA_CHECK(SUCCEEDED(software_dxgi->GetAdapter(&software_adapter)));
    ComPtr<IDXGIAdapter1> software_adapter1;
    VESTA_CHECK(SUCCEEDED(software_adapter.As(&software_adapter1)));
    DXGI_ADAPTER_DESC1 software_description{};
    VESTA_CHECK(SUCCEEDED(software_adapter1->GetDesc1(&software_description)));
    VESTA_CHECK((software_description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0);
    VESTA_CHECK(SUCCEEDED(software_adapter->GetParent(
        IID_PPV_ARGS(&software_factory))));
    ComPtr<IDCompositionDevice> software_composition;
    VESTA_CHECK(SUCCEEDED(::DCompositionCreateDevice(
        software_dxgi.Get(), IID_PPV_ARGS(&software_composition))));
    auto software_swap_description = render::composition_description(128, 128, false);
    ComPtr<IDXGISwapChain1> software_chain;
    VESTA_CHECK(SUCCEEDED(software_factory->CreateSwapChainForComposition(
        software_device.Get(), &software_swap_description, nullptr, &software_chain)));
    std::cout << "software_composition=PASS\\n";
    for (auto* tested_device : {device.Get(), software_device.Get()})
    {
        ComPtr<IDXGIDevice> tested_dxgi;
        ComPtr<IDXGIAdapter> tested_adapter;
        ComPtr<IDXGIFactory2> tested_factory;
        VESTA_CHECK(SUCCEEDED(tested_device->QueryInterface(IID_PPV_ARGS(&tested_dxgi))));
        VESTA_CHECK(SUCCEEDED(tested_dxgi->GetAdapter(&tested_adapter)));
        VESTA_CHECK(SUCCEEDED(tested_adapter->GetParent(IID_PPV_ARGS(&tested_factory))));
        for (int i = 0; i < 8; ++i)
        {
            auto desc = render::composition_description(128, 128, false, false);
            VESTA_CHECK((desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) == 0);
            ComPtr<IDXGISwapChain1> chain;
            VESTA_CHECK(SUCCEEDED(tested_factory->CreateSwapChainForComposition(
                tested_device, &desc, nullptr, &chain)));
            ComPtr<IDXGISwapChain2> chain2;
            VESTA_CHECK(SUCCEEDED(chain.As(&chain2)));
            VESTA_CHECK(chain2->GetFrameLatencyWaitableObject() == nullptr);
        }
    }
    std::cout << "nonwaitable_recreate=16 PASS\\n";
}
