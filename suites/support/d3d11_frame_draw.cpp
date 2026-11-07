#include "support/d3d11_frame_draw.hpp"

#include "support/frame_image.hpp"

namespace lsw::suites {

bool D3d11FrameDraw::open(D3d11Device& device, std::string& error) {
    device_ = &device;
    vertex_ = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    pixel_ = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 color; }\nfloat4 main() : SV_Target { return color; }\n", error);
    constants_ = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
    if (!vertex_ || !pixel_ || !constants_) {
        if (error.empty()) {
            error = "the constant colour pipeline cannot be created";
        }
        return false;
    }
    return true;
}

void D3d11FrameDraw::release() {
    vertex_.reset();
    pixel_.reset();
    constants_.reset();
    device_ = nullptr;
}

void D3d11FrameDraw::fill(ID3D11RenderTargetView* view, const RECT& rect, std::uint32_t xrgb, float alpha) {
    const float color[4] = {static_cast<float>((xrgb >> 16) & 255u) / 255.0f, static_cast<float>((xrgb >> 8) & 255u) / 255.0f,
                            static_cast<float>(xrgb & 255u) / 255.0f, alpha};
    ID3D11DeviceContext* context = device_->context;
    context->UpdateSubresource(constants_.get(), 0, nullptr, color, 0, 0);
    ID3D11RenderTargetView* views[1] = {view};
    context->OMSetRenderTargets(1, views, nullptr);
    const D3D11_VIEWPORT viewport{static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right - rect.left),
                                  static_cast<float>(rect.bottom - rect.top), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_.get(), nullptr, 0);
    context->PSSetShader(pixel_.get(), nullptr, 0);
    ID3D11Buffer* buffers[1] = {constants_.get()};
    context->PSSetConstantBuffers(0, 1, buffers);
    context->Draw(3, 0);
}

void D3d11FrameDraw::frame(ID3D11RenderTargetView* view, int width, int height, int index) {
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        fill(view, FrameImage::quadrant(width, height, quadrant), FrameImage::color(index, quadrant));
    }
}

}  // namespace lsw::suites
