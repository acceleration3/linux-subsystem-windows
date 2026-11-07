#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d11_tools.hpp"
#include "support/test_window.hpp"

#include <dxgi1_6.h>

#include <string>

namespace lsw::suites {
struct SwapOptions {
    UINT width = 256;
    UINT height = 256;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT buffers = 2;
    DXGI_SWAP_EFFECT effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    UINT flags = 0;
    int x = 60;
    int y = 60;
    bool visible = true;
    UINT device_flags = 0;
};

class SwapRig {
public:
    [[nodiscard]] bool open(const SwapOptions& options, std::string& error);

    [[nodiscard]] bool create_chain(const SwapOptions& options, std::string& error);

    [[nodiscard]] HRESULT try_create(const SwapOptions& options, HWND hwnd, Com<IDXGISwapChain1>& out);

    [[nodiscard]] Com<ID3D11Texture2D> back_buffer(UINT index = 0);

    [[nodiscard]] Com<ID3D11RenderTargetView> back_view(UINT index = 0);

    [[nodiscard]] bool fill(float r, float g, float b, float a, std::string& error);

    [[nodiscard]] bool client_point(int dx, int dy, POINT& out) const;

    [[nodiscard]] static bool screen_rgb(int x, int y, int rgb[3]);

    [[nodiscard]] bool wait_for_color(int dx, int dy, const int want[3], int tolerance, int timeout_ms, int got[3]);

    D3d11Device device;
    TestWindow window;
    Com<IDXGIFactory2> factory;
    Com<IDXGISwapChain1> chain;
    SwapOptions options;
};
}  // namespace lsw::suites
