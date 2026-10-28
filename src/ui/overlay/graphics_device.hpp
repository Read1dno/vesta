#pragma once
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <ui/overlay/render_policy.hpp>
#include <ui/overlay/resource_priority.hpp>
#include <algorithm>

namespace ui
{
// Orders the overlay's GPU work against the game's: the scheduling class is process-wide (gdi32
// D3DKMTSetProcessSchedulingPriorityClass), the thread priority orders this device within the class. A refused class
// falls back to normal so a failure can never leave the overlay above the game by accident.
[[nodiscard]] inline HRESULT set_overlay_gpu_priority(ID3D11Device* device, const gpu_priority_plan plan)
{
    using set_class_fn = LONG(APIENTRY*)(HANDLE, INT);
    static const auto set_class = []
    {
        const auto gdi32 = ::LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return gdi32
                   ? reinterpret_cast<set_class_fn>(::GetProcAddress(gdi32, "D3DKMTSetProcessSchedulingPriorityClass"))
                   : nullptr;
    }();
    HRESULT result = S_OK;
    if (set_class && set_class(::GetCurrentProcess(), static_cast<INT>(plan.scheduling_class)) < 0)
    {
        result = E_ACCESSDENIED;
        set_class(::GetCurrentProcess(), static_cast<INT>(gpu_scheduling_class::normal));
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    if (const auto queried = device->QueryInterface(IID_PPV_ARGS(&dxgi)); FAILED(queried))
        return queried;
    if (const auto priority = dxgi->SetGPUThreadPriority(plan.thread_priority); FAILED(priority))
    {
        (void)dxgi->SetGPUThreadPriority(std::min(plan.thread_priority, 0));
        return priority;
    }
    return result;
}

[[nodiscard]] inline HRESULT create_overlay_device(HMONITOR monitor, ID3D11Device** device,
                                                   ID3D11DeviceContext** context, bool use_gpu = true)
{
    using Microsoft::WRL::ComPtr;
    const UINT flags =
        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT
#if defined(VESTA_D3D_DEBUG)
        | (GetEnvironmentVariableW(L"VESTA_TEST_NO_D3D_DEBUG", nullptr, 0) == 0 ? D3D11_CREATE_DEVICE_DEBUG : 0)
#endif
        ;
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    if (!use_gpu)
    {
        auto result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                          device, nullptr, context);
        if (result == E_INVALIDARG)
            result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels + 1, 1,
                                         D3D11_SDK_VERSION, device, nullptr, context);
        return result;
    }
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
    const auto driver = selected ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    auto result = ::D3D11CreateDevice(selected.Get(), driver, nullptr, flags, levels, 2, D3D11_SDK_VERSION, device,
                                      nullptr, context);
    if (result == E_INVALIDARG)
        result = ::D3D11CreateDevice(selected.Get(), driver, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, device,
                                     nullptr, context);
    return result;
}

[[nodiscard]] inline DXGI_SWAP_CHAIN_DESC1 composition_description(UINT width, UINT height, bool tearing,
                                                                   bool waitable = true)
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
    d.Flags = (waitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0u) |
              (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    return d;
}
} // namespace ui
