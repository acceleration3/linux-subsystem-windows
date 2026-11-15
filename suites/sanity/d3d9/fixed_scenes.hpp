#pragma once

#include "sanity/d3d9/kit.hpp"

#include <functional>
#include <string>
#include <vector>

namespace lsw::suites::sanity::d3d9 {
struct FixedScene {
    std::string name;
    std::function<bool(IDirect3DDevice9*)> draw;
    TwinScene twin;
    Tolerance tolerance;
    std::string unsupported;
};

class FixedScenes {
public:
    static constexpr UINT kSize = 128;

    [[nodiscard]] static std::vector<FixedScene> build(IDirect3DDevice9* device, const D3DCAPS9& caps);
};
}  // namespace lsw::suites::sanity::d3d9
