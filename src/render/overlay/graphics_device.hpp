#pragma once
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

namespace render
{
[[nodiscard]] inline HRESULT create_overlay_device(HMONITOR monitor, ID3D11Device **device,
                                                   ID3D11DeviceContext **context)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> selected;
    if (monitor && SUCCEEDED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        for (UINT i = 0;; ++i)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(i, &adapter)))
                break;
            for (UINT j = 0;; ++j)
            {
                ComPtr<IDXGIOutput> output;
                if (FAILED(adapter->EnumOutputs(j, &output)))
                    break;
                DXGI_OUTPUT_DESC desc{};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor)
                {
                    selected = adapter;
                    break;
                }
            }
            if (selected)
                break;
        }
    }
    constexpr UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    const auto driver = selected ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    auto result = ::D3D11CreateDevice(selected.Get(), driver, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                      device, nullptr, context);
    if (result == E_INVALIDARG)
        result = ::D3D11CreateDevice(selected.Get(), driver, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION,
                                     device, nullptr, context);
    return result;
}

[[nodiscard]] inline DXGI_SWAP_CHAIN_DESC1 composition_description(UINT width, UINT height, bool tearing)
{
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = width;
    d.Height = height;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc = {1, 0};
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
              (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    return d;
}
} // namespace render
