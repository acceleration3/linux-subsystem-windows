#include "support/swap_rig.hpp"

#include "support/target_adapter.hpp"

#include <chrono>
#include <cstdlib>

namespace lsw::suites {

bool SwapRig::open(const SwapOptions& wanted, std::string& error) {
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error)) {
        return false;
    }
    if (wanted.warp) {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        if (!device.create_warp(&level, 1, D3D11_CREATE_DEVICE_BGRA_SUPPORT | wanted.device_flags, error)) {
            return false;
        }
    } else if (!device.create_ex(adapter.luid, nullptr, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT | wanted.device_flags, error)) {
        return false;
    }
    TestWindowConfig config;
    config.x = wanted.x;
    config.y = wanted.y;
    if (wanted.popup) {
        config.popup = true;
        config.width = static_cast<int>(wanted.width);
        config.height = static_cast<int>(wanted.height);
    } else {
        TestWindow::fit_config(config, static_cast<int>(wanted.width), static_cast<int>(wanted.height));
    }
    config.visible = wanted.visible;
    if (!window.create(config, error)) {
        return false;
    }
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Com<IDXGIDevice> dxgi_device;
    device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi_device.put_void());
    Com<IDXGIAdapter> dxgi_adapter;
    if (!dxgi_device || FAILED(dxgi_device->GetAdapter(dxgi_adapter.put())) ||
        FAILED(dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()))) {
        error = "the DXGI factory of the device cannot be reached";
        return false;
    }
    return create_chain(wanted, error);
}

HRESULT SwapRig::try_create(const SwapOptions& wanted, HWND hwnd, Com<IDXGISwapChain1>& out) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = wanted.width;
    desc.Height = wanted.height;
    desc.Format = wanted.format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = wanted.usage;
    desc.BufferCount = wanted.buffers;
    desc.SwapEffect = wanted.effect;
    desc.Flags = wanted.flags;
    desc.AlphaMode = wanted.alpha;
    desc.Scaling = DXGI_SCALING_STRETCH;
    if (wanted.effect == DXGI_SWAP_EFFECT_FLIP_DISCARD || wanted.effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
        desc.Scaling = DXGI_SCALING_NONE;
    }
    if (wanted.scaling >= 0) {
        desc.Scaling = static_cast<DXGI_SCALING>(wanted.scaling);
    }
    return factory->CreateSwapChainForHwnd(device.device, hwnd, &desc, nullptr, nullptr, out.put());
}

bool SwapRig::create_chain(const SwapOptions& wanted, std::string& error) {
    chain.reset();
    options = wanted;
    const HRESULT result = try_create(wanted, window.hwnd(), chain);
    if (FAILED(result)) {
        error = "CreateSwapChainForHwnd failed: " + D3d11Tools::hr(result);
        return false;
    }
    return true;
}

Com<ID3D11Texture2D> SwapRig::back_buffer(UINT index) {
    Com<ID3D11Texture2D> out;
    chain->GetBuffer(index, __uuidof(ID3D11Texture2D), out.put_void());
    return out;
}

Com<ID3D11RenderTargetView> SwapRig::back_view(UINT index) {
    Com<ID3D11Texture2D> buffer = back_buffer(index);
    Com<ID3D11RenderTargetView> view;
    if (buffer) {
        device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put());
    }
    return view;
}

bool SwapRig::fill(float r, float g, float b, float a, std::string& error) {
    Com<ID3D11RenderTargetView> view = back_view(0);
    if (!view) {
        error = "no render target view over the back buffer";
        return false;
    }
    D3d11Tools::clear(device, view.get(), r, g, b, a);
    return true;
}

bool SwapRig::client_point(int dx, int dy, POINT& out) const {
    out = {dx, dy};
    return ::ClientToScreen(window.hwnd(), &out) != FALSE;
}

bool SwapRig::screen_rgb(int x, int y, int rgb[3]) {
    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }
    const COLORREF color = ::GetPixel(screen, x, y);
    ::ReleaseDC(nullptr, screen);
    if (color == CLR_INVALID) {
        return false;
    }
    rgb[0] = GetRValue(color);
    rgb[1] = GetGValue(color);
    rgb[2] = GetBValue(color);
    return true;
}

bool SwapRig::wait_for_color(int dx, int dy, const int want[3], int tolerance, int timeout_ms, int got[3]) {
    POINT point{};
    if (!client_point(dx, dy, point)) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        if (screen_rgb(point.x, point.y, got) && std::abs(got[0] - want[0]) <= tolerance &&
            std::abs(got[1] - want[1]) <= tolerance && std::abs(got[2] - want[2]) <= tolerance) {
            return true;
        }
        ::Sleep(20);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace lsw::suites
