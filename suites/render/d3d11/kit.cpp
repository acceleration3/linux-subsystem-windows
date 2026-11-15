#include "render/d3d11/kit.hpp"

#include "render/d3d/convert.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsw::suites::render::d3d11 {

OpenStatus Kit::open(bool use_warp, const rr::CaseSpec& spec, std::string& error) {
    warp = use_warp;
    const UINT flags = std::getenv("LSW_D3D11_DEBUG") != nullptr ? D3D11_CREATE_DEVICE_DEBUG : 0;
    debug = flags != 0;
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_10_0,
                                                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_12_0,  D3D_FEATURE_LEVEL_12_1};
    const D3D_FEATURE_LEVEL level = kLevels[std::min<unsigned>(spec.variant(), 5)];
    if (warp) {
        name = "WARP";
        if (!dev.create_warp(&level, 1, flags, error)) {
            return dev.result == E_INVALIDARG || dev.result == DXGI_ERROR_UNSUPPORTED ? OpenStatus::Unsupported : OpenStatus::Failed;
        }
        return open_swap(spec, error);
    }
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error)) {
        return OpenStatus::Failed;
    }
    name = adapter.name;
    if (!dev.create_ex(adapter.luid, &level, 1, flags, error)) {
        return OpenStatus::Failed;
    }
    return open_swap(spec, error);
}

OpenStatus Kit::open_swap(const rr::CaseSpec& spec, std::string& error) {
    if (!spec.multiframe()) {
        return OpenStatus::Ok;
    }
    return swap.open(dev.device, spec.width(), spec.height(), 2, DXGI_FORMAT_B8G8R8A8_UNORM, false, error) ? OpenStatus::Ok : OpenStatus::Failed;
}

bool Kit::end_frame(std::string& error) { return swap.present(error); }

Com<ID3D11VertexShader> Kit::vertex_shader(const std::string& source, std::vector<std::uint8_t>& code, std::string& error) {
    if (!D3d11Tools::compile(source, "main", "vs_5_0", code, error)) {
        return {};
    }
    Com<ID3D11VertexShader> shader;
    const HRESULT result = dev.device->CreateVertexShader(code.data(), code.size(), nullptr, shader.put());
    if (FAILED(result)) {
        error = "CreateVertexShader: " + D3d11Tools::hr(result);
        return {};
    }
    return shader;
}

Com<ID3D11PixelShader> Kit::pixel_shader(const std::string& source, std::string& error) {
    return D3d11Tools::ps(dev, source, error, "main");
}

Com<ID3D11Buffer> Kit::buffer(UINT size, D3D11_USAGE usage, UINT bind, UINT cpu, const void* initial, std::string& error) {
    HRESULT result = S_OK;
    Com<ID3D11Buffer> out = D3d11Tools::buffer(dev, size, usage, bind, cpu, 0, 0, initial, &result);
    if (!out) {
        error = "CreateBuffer: " + D3d11Tools::hr(result);
    }
    return out;
}

bool Kit::make_target(Target& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples, std::string& error) {
    HRESULT result = S_OK;
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(width, height, format, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                                   D3D11_USAGE_DEFAULT, 0, 1, 1, samples);
    out.texture = D3d11Tools::texture(dev, desc, nullptr, &result);
    if (!out.texture) {
        error = "render target creation: " + D3d11Tools::hr(result);
        return false;
    }
    out.view = D3d11Tools::rtv(dev, out.texture.get());
    if (!out.view) {
        error = "render target view creation failed";
        return false;
    }
    if (samples > 1) {
        desc.SampleDesc.Count = 1;
        out.resolved = D3d11Tools::texture(dev, desc, nullptr, &result);
        if (!out.resolved) {
            error = "resolve target creation: " + D3d11Tools::hr(result);
            return false;
        }
    }
    out.format = format;
    out.width = width;
    out.height = height;
    out.samples = samples;
    return true;
}

bool Kit::make_depth(Depth& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples, std::string& error) {
    HRESULT result = S_OK;
    out.texture = D3d11Tools::texture(dev, D3d11Tools::desc2d(width, height, format, D3D11_BIND_DEPTH_STENCIL, D3D11_USAGE_DEFAULT, 0, 1, 1, samples),
                                      nullptr, &result);
    if (!out.texture) {
        error = "depth texture creation: " + D3d11Tools::hr(result);
        return false;
    }
    result = dev.device->CreateDepthStencilView(out.texture.get(), nullptr, out.view.put());
    if (FAILED(result)) {
        error = "CreateDepthStencilView: " + D3d11Tools::hr(result);
        return false;
    }
    out.format = format;
    out.width = width;
    out.height = height;
    return true;
}

bool Kit::read_color(Target& target, rr::Image& out, std::string& error) {
    ID3D11Texture2D* source = target.texture.get();
    if (target.samples > 1) {
        dev.context->ResolveSubresource(target.resolved.get(), 0, target.texture.get(), 0, target.format);
        source = target.resolved.get();
    }
    TexelImage texels;
    return D3d11Tools::read_texture(dev, source, 0, texels, error) && Convert::image_of(texels, target.format, out, error);
}

bool Kit::read_depth(Depth& depth, rr::Image& out, std::string& error) {
    TexelImage texels;
    return D3d11Tools::read_texture(dev, depth.texture.get(), 0, texels, error) && Convert::image_of(texels, depth.format, out, error);
}

bool Kit::read_stencil(Depth& depth, rr::Image& out, std::string& error) {
    TexelImage texels;
    return D3d11Tools::read_texture(dev, depth.texture.get(), 0, texels, error) && Convert::stencil_of(texels, out, error);
}

void Kit::set_viewport(UINT width, UINT height) { D3d11Tools::set_viewport(dev, width, height); }

}  // namespace lsw::suites::render::d3d11
