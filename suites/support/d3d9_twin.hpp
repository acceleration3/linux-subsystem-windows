#pragma once

#include "core/context.hpp"
#include "support/d3d11_device.hpp"
#include "support/oracle_rig.hpp"
#include "support/problems.hpp"

#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct TwinElement {
    const char* semantic;
    DXGI_FORMAT format;
    UINT offset;
};

struct TwinTexture {
    UINT width = 0;
    UINT height = 0;
    std::vector<std::uint8_t> bgra;
};

struct TwinScene {
    std::vector<std::uint8_t> vertices;
    UINT stride = 0;
    UINT vertex_count = 0;
    std::vector<TwinElement> elements;
    std::vector<std::uint16_t> indices;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    std::string vs;
    std::string ps;
    std::vector<float> constants;
    std::vector<TwinTexture> textures;
    bool linear = false;
    UINT width = 64;
    UINT height = 64;
    UINT targets = 1;
    UINT samples = 1;
    float clear[4] = {0, 0, 0, 1};
};

class D3d9Twin {
public:
    [[nodiscard]] bool open(std::string& error);

    [[nodiscard]] bool render(const TwinScene& scene, std::vector<SceneImage>& out, std::string& error);

    void compare(Context& context, Problems& problems, const std::string& label, const SceneImage& actual,
                 const TwinScene& scene, const Tolerance& tolerance, UINT target = 0);

    D3d11Device warp;
};
}  // namespace lsw::suites
