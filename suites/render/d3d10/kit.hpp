#pragma once

#include "render/d3d/warp_check.hpp"
#include "support/d3d10_device.hpp"
#include "support/d3d10_tools.hpp"

#include <vector>

namespace lsw::suites::render::d3d10 {
struct Target {
    Com<ID3D10Texture2D> texture;
    Com<ID3D10Texture2D> resolved;
    Com<ID3D10RenderTargetView> view;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
    UINT samples = 1;
};

struct Depth {
    Com<ID3D10Texture2D> texture;
    Com<ID3D10DepthStencilView> view;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
};

class Kit {
public:
    [[nodiscard]] OpenStatus open(bool use_warp, const rr::CaseSpec& spec, std::string& error);

    [[nodiscard]] ID3D10Device* device() { return dev.device; }

    [[nodiscard]] Com<ID3D10VertexShader> vertex_shader(const std::string& source, std::vector<std::uint8_t>& code, std::string& error);
    [[nodiscard]] Com<ID3D10PixelShader> pixel_shader(const std::string& source, std::string& error);
    [[nodiscard]] Com<ID3D10Buffer> buffer(UINT size, D3D10_USAGE usage, UINT bind, UINT cpu, const void* initial, std::string& error);

    [[nodiscard]] bool make_target(Target& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples, std::string& error);
    [[nodiscard]] bool make_depth(Depth& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples, std::string& error);

    [[nodiscard]] bool read_color(Target& target, rr::Image& out, std::string& error);
    [[nodiscard]] bool read_depth(Depth& depth, rr::Image& out, std::string& error);
    [[nodiscard]] bool read_stencil(Depth& depth, rr::Image& out, std::string& error);

    [[nodiscard]] bool read_raw(ID3D10Texture2D* texture, TexelImage& out, std::string& error);

    void set_viewport(UINT width, UINT height);

    bool warp = false;
    std::string name;
    D3d10Device dev;
};
}  // namespace lsw::suites::render::d3d10
