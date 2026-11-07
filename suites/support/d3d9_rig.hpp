#pragma once

#include "support/av_adapters.hpp"
#include "support/com_ptr.hpp"
#include "support/test_window.hpp"

#include <d3d9.h>

#include <string>

namespace lsw::suites {
struct D3d9Settings {
    UINT width = 128;
    UINT height = 128;
    D3DFORMAT back = D3DFMT_X8R8G8B8;
    UINT buffers = 1;
    D3DSWAPEFFECT effect = D3DSWAPEFFECT_DISCARD;
    D3DMULTISAMPLE_TYPE multisample = D3DMULTISAMPLE_NONE;
    bool depth = true;
    D3DFORMAT depth_format = D3DFMT_D24S8;
    UINT interval = D3DPRESENT_INTERVAL_DEFAULT;
    DWORD behavior = 0;
    DWORD extra_behavior = 0;
    int x = 60;
    int y = 60;
    bool visible = true;
};

class D3d9Rig {
public:
    [[nodiscard]] bool open(bool use_ex, std::string& error);

    [[nodiscard]] bool create(const D3d9Settings& wanted, std::string& error);

    [[nodiscard]] HRESULT try_create(const D3d9Settings& wanted);

    [[nodiscard]] bool make_window(const D3d9Settings& wanted, std::string& error);

    [[nodiscard]] D3DPRESENT_PARAMETERS parameters(const D3d9Settings& wanted) const;

    [[nodiscard]] DWORD auto_behavior() const;

    [[nodiscard]] bool caps(D3DCAPS9& out) const;

    [[nodiscard]] RECT monitor_rect() const;

    [[nodiscard]] POINT client_origin() const;

    void close_device();

    [[nodiscard]] IDirect3DDevice9* dev() const { return device.get(); }

    [[nodiscard]] static bool create_ex_available();

    bool ex = false;
    Com<IDirect3D9> d3d;
    Com<IDirect3D9Ex> d3d_ex;
    Com<IDirect3DDevice9> device;
    Com<IDirect3DDevice9Ex> device_ex;
    AvAdapter adapter;
    UINT ordinal = 0;
    TestWindow window;
    D3d9Settings settings;
};
}  // namespace lsw::suites
