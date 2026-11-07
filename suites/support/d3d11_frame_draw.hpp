#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d11_tools.hpp"

#include <cstdint>
#include <string>

namespace lsw::suites {
class D3d11FrameDraw {
public:
    [[nodiscard]] bool open(D3d11Device& device, std::string& error);

    void fill(ID3D11RenderTargetView* view, const RECT& rect, std::uint32_t xrgb, float alpha = 1.0f);

    void frame(ID3D11RenderTargetView* view, int width, int height, int index);

    void release();

private:
    D3d11Device* device_ = nullptr;
    Com<ID3D11VertexShader> vertex_;
    Com<ID3D11PixelShader> pixel_;
    Com<ID3D11Buffer> constants_;
};
}  // namespace lsw::suites
