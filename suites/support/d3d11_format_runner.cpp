#include "support/d3d11_format_runner.hpp"

namespace lsw::suites {

bool D3d11FormatRunner::support(DXGI_FORMAT format, UINT& support1) {
    support1 = 0;
    return SUCCEEDED(device_.device->CheckFormatSupport(format, &support1));
}

bool D3d11FormatRunner::roundtrip(DXGI_FORMAT format, UINT width, UINT height, const Bytes& in, Bytes& out, std::string& error) {
    UINT row_bytes = 0;
    UINT rows = 0;
    layout(format, width, height, row_bytes, rows);
    D3D11_SUBRESOURCE_DATA initial{in.data(), row_bytes, 0};
    HRESULT result = S_OK;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device_, D3d11Tools::desc2d(width, height, format, 0), &initial, &result);
    if (!texture) {
        error = "creating the texture failed: " + D3d11Tools::hr(result);
        return false;
    }
    TexelImage image;
    if (!D3d11Tools::read_texture(device_, texture.get(), 0, image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d11FormatRunner::clear(DXGI_FORMAT format, UINT width, UINT height, const float color[4], Bytes& out, std::string& error) {
    Com<ID3D11Texture2D> texture = D3d11Tools::target(device_, width, height, format, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device_, texture.get());
    if (!texture || !view) {
        error = "creating the render target failed";
        return false;
    }
    device_.context->ClearRenderTargetView(view.get(), color);
    TexelImage image;
    if (!D3d11Tools::read_texture(device_, texture.get(), 0, image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d11FormatRunner::draw(DXGI_FORMAT format, UINT width, UINT height, const std::string& type, const std::string& expression,
                             const float* clear_color, bool alpha_blend, Bytes& out, std::string& error,
                             const std::string& prelude) {
    Com<ID3D11Texture2D> texture = D3d11Tools::target(device_, width, height, format, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device_, texture.get());
    Com<ID3D11PixelShader> pixel = D3d11Tools::ps(device_, pixel_source(type, expression, prelude), error);
    if (!texture || !view) {
        error = "creating the render target failed";
        return false;
    }
    if (!pixel) {
        return false;
    }
    if (clear_color != nullptr) {
        device_.context->ClearRenderTargetView(view.get(), clear_color);
    }
    Com<ID3D11BlendState> blend;
    if (alpha_blend) {
        D3D11_BLEND_DESC desc{};
        desc.RenderTarget[0].BlendEnable = TRUE;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = 0xf;
        const HRESULT result = device_.device->CreateBlendState(&desc, blend.put());
        if (FAILED(result)) {
            error = "creating the blend state failed: " + D3d11Tools::hr(result);
            return false;
        }
    }
    device_.context->OMSetBlendState(blend.get(), nullptr, 0xffffffff);
    const bool drawn = D3d11Tools::draw_fullscreen(device_, pixel.get(), view.get(), width, height, error);
    device_.context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    if (!drawn) {
        return false;
    }
    TexelImage image;
    if (!D3d11Tools::read_texture(device_, texture.get(), 0, image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

bool D3d11FormatRunner::sample(DXGI_FORMAT texture_format, DXGI_FORMAT view_format, const std::string& type, UINT width, UINT height,
                               const Bytes& in, std::vector<std::uint32_t>& words, std::string& error) {
    UINT row_bytes = 0;
    UINT rows = 0;
    layout(texture_format, width, height, row_bytes, rows);
    D3D11_SUBRESOURCE_DATA initial{in.data(), row_bytes, 0};
    HRESULT result = S_OK;
    Com<ID3D11Texture2D> texture =
        D3d11Tools::texture(device_, D3d11Tools::desc2d(width, height, texture_format, D3D11_BIND_SHADER_RESOURCE), &initial, &result);
    if (!texture) {
        error = "creating the texture failed: " + D3d11Tools::hr(result);
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
    view_desc.Format = view_format;
    view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view_desc.Texture2D.MipLevels = 1;
    Com<ID3D11ShaderResourceView> view;
    result = device_.device->CreateShaderResourceView(texture.get(), &view_desc, view.put());
    if (FAILED(result)) {
        error = "creating the shader resource view failed: " + D3d11Tools::hr(result);
        return false;
    }
    const UINT bytes = width * height * 16;
    Com<ID3D11Buffer> output = D3d11Tools::buffer(device_, bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                                  D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, &result);
    Com<ID3D11UnorderedAccessView> uav = output ? D3d11Tools::raw_uav(device_, output.get()) : Com<ID3D11UnorderedAccessView>();
    Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device_, sample_source(type, width, height), error);
    if (!output || !uav) {
        error = "creating the output buffer failed: " + D3d11Tools::hr(result);
        return false;
    }
    if (!shader) {
        return false;
    }
    ID3D11ShaderResourceView* views[1] = {view.get()};
    ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
    device_.context->CSSetShader(shader.get(), nullptr, 0);
    device_.context->CSSetShaderResources(0, 1, views);
    device_.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device_.context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    D3d11Tools::reset_state(device_);
    words = D3d11Tools::read_words(device_, output.get(), error);
    return words.size() == static_cast<std::size_t>(width) * height * 4;
}

}  // namespace lsw::suites
