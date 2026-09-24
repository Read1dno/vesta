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
    std::cout << "composition_create_destroy=3 PASS\n";
}
