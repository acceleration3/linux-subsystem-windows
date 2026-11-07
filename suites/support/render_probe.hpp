#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d11_device.hpp"

#include <d3d11.h>

#include <string>

namespace lsw::suites {
class RenderProbe {
public:
    [[nodiscard]] bool open(std::string& error);

    [[nodiscard]] bool cycle(unsigned step, std::string& error);

private:
    D3d11Device device_;
    Com<ID3D11Texture2D> target_;
    Com<ID3D11RenderTargetView> view_;
};
}  // namespace lsw::suites
