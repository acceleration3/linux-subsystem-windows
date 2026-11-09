#pragma once

#include "render/d3d/warp_check.hpp"
#include "support/com_ptr.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d9_rig.hpp"
#include "support/test_window.hpp"

#include <d3d9.h>

#include <vector>

namespace lsw::suites::render::d3d9 {
struct Surface {
    Com<IDirect3DSurface9> surface;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
};

class Kit {
public:
    [[nodiscard]] OpenStatus open(bool use_warp, const rr::CaseSpec& spec, std::string& error);

    [[nodiscard]] IDirect3DDevice9* device() { return device_.get(); }

    [[nodiscard]] Com<IDirect3DVertexShader9> vertex_shader(const std::string& source, std::string& error);
    [[nodiscard]] Com<IDirect3DPixelShader9> pixel_shader(const std::string& source, std::string& error);

    [[nodiscard]] bool make_target(Surface& out, UINT width, UINT height, D3DFORMAT format, std::string& error);
    [[nodiscard]] bool make_depth(Surface& out, UINT width, UINT height, std::string& error);
    [[nodiscard]] bool read_color(Surface& target, rr::Image& out, std::string& error);

    [[nodiscard]] bool begin(std::string& error);
    [[nodiscard]] bool end(std::string& error);

    bool warp = false;
    std::string name;
    D3D_FEATURE_LEVEL warp_level = D3D_FEATURE_LEVEL_11_0;

private:
    [[nodiscard]] bool create_device(IDirect3D9* factory, UINT ordinal, bool ex, std::string& error);

    D3d9Rig rig_;
    D3d12Rig warp12_;
    Com<IDirect3D9> on12_factory_;
    Com<IDirect3DDevice9> device_;
    TestWindow window_;
};
}  // namespace lsw::suites::render::d3d9
