#include "render/d3d10/kit.hpp"

#include "render/d3d/convert.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::render::d3d10 {

OpenStatus Kit::open(bool use_warp, const rr::CaseSpec& spec, std::string& error) {
    warp = use_warp;
    const unsigned variant = spec.variant();
    const bool level_1 = variant != 0;
    const D3D10_FEATURE_LEVEL1 level = variant == 2 ? D3D10_FEATURE_LEVEL_10_0 : D3D10_FEATURE_LEVEL_10_1;
    level_10_1 = variant == 1;
    bool created = false;
    if (warp) {
        name = "WARP";
        created = level_1 ? dev.create_warp1(0, level, error) : dev.create_warp(0, error);
    } else {
        AvAdapter adapter;
        if (!TargetAdapter::pick(adapter, error)) {
            return OpenStatus::Failed;
        }
        name = adapter.name;
        created = level_1 ? dev.create1(adapter.luid, 0, level, error) : dev.create(adapter.luid, 0, error);
    }
    if (created) {
        if (!spec.multiframe()) {
            return OpenStatus::Ok;
        }
        IUnknown* base = dev.device;
        return swap.open(base, 128, 128, 2, DXGI_FORMAT_R8G8B8A8_UNORM, true, error) ? OpenStatus::Ok : OpenStatus::Failed;
    }
    return dev.result == E_INVALIDARG || dev.result == DXGI_ERROR_UNSUPPORTED ? OpenStatus::Unsupported : OpenStatus::Failed;
}

Com<ID3D10VertexShader> Kit::vertex_shader(const std::string& source, std::vector<std::uint8_t>& code, std::string& error) {
    if (!D3d10Tools::compile(source, "vs_4_0", code, error)) {
        return {};
    }
    Com<ID3D10VertexShader> shader;
    const HRESULT result = dev.device->CreateVertexShader(code.data(), code.size(), shader.put());
    if (FAILED(result)) {
        error = "CreateVertexShader: " + D3d10Tools::hr(result);
        return {};
    }
    return shader;
}

Com<ID3D10PixelShader> Kit::pixel_shader(const std::string& source, std::string& error) {
    return D3d10Tools::ps(dev, source, error, "ps_4_0");
}

Com<ID3D10Buffer> Kit::buffer(UINT size, D3D10_USAGE usage, UINT bind, UINT cpu, const void* initial, std::string& error) {
    HRESULT result = S_OK;
    Com<ID3D10Buffer> out = D3d10Tools::buffer(dev, size, usage, bind, cpu, 0, initial, &result);
    if (!out) {
        error = "CreateBuffer: " + D3d10Tools::hr(result);
    }
    return out;
}

bool Kit::make_target(Target& out, UINT width, UINT height, DXGI_FORMAT format, UINT samples, std::string& error) {
    HRESULT result = S_OK;
    D3D10_TEXTURE2D_DESC desc = D3d10Tools::desc2d(width, height, format, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE,
                                                   D3D10_USAGE_DEFAULT, 0, 1, 1, samples);
    out.texture = D3d10Tools::texture(dev, desc, nullptr, &result);
    if (!out.texture) {
        error = "render target creation: " + D3d10Tools::hr(result);
        return false;
    }
    out.view = D3d10Tools::rtv(dev, out.texture.get());
    if (!out.view) {
        error = "render target view creation failed";
        return false;
    }
    if (samples > 1) {
        desc.SampleDesc.Count = 1;
        out.resolved = D3d10Tools::texture(dev, desc, nullptr, &result);
        if (!out.resolved) {
            error = "resolve target creation: " + D3d10Tools::hr(result);
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
    out.texture = D3d10Tools::texture(dev, D3d10Tools::desc2d(width, height, format, D3D10_BIND_DEPTH_STENCIL, D3D10_USAGE_DEFAULT, 0, 1, 1, samples),
                                      nullptr, &result);
    if (!out.texture) {
        error = "depth texture creation: " + D3d10Tools::hr(result);
        return false;
    }
    result = dev.device->CreateDepthStencilView(out.texture.get(), nullptr, out.view.put());
    if (FAILED(result)) {
        error = "CreateDepthStencilView: " + D3d10Tools::hr(result);
        return false;
    }
    out.format = format;
    out.width = width;
    out.height = height;
    return true;
}

bool Kit::read_color(Target& target, rr::Image& out, std::string& error) {
    ID3D10Texture2D* source = target.texture.get();
    if (target.samples > 1) {
        dev.device->ResolveSubresource(target.resolved.get(), 0, target.texture.get(), 0, target.format);
        source = target.resolved.get();
    }
    TexelImage texels;
    return read_raw(source, texels, error) && Convert::image_of(texels, target.format, out, error);
}

bool Kit::read_depth(Depth& depth, rr::Image& out, std::string& error) {
    TexelImage texels;
    return read_raw(depth.texture.get(), texels, error) && Convert::image_of(texels, depth.format, out, error);
}

bool Kit::read_stencil(Depth& depth, rr::Image& out, std::string& error) {
    TexelImage texels;
    return read_raw(depth.texture.get(), texels, error) && Convert::stencil_of(texels, out, error);
}

bool Kit::read_raw(ID3D10Texture2D* texture, TexelImage& out, std::string& error) {
    D3D10_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    rr::PixelKind kind = rr::PixelKind::Rgba8Unorm;
    const UINT bytes = Convert::kind_of(desc.Format, kind) ? static_cast<UINT>(rr::PixelInfo::bytes(kind)) : 4;
    desc.Usage = D3D10_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Com<ID3D10Texture2D> staging;
    HRESULT result = dev.device->CreateTexture2D(&desc, nullptr, staging.put());
    if (FAILED(result)) {
        error = "staging texture creation: " + D3d10Tools::hr(result);
        return false;
    }
    dev.device->CopyResource(staging.get(), texture);
    D3D10_MAPPED_TEXTURE2D mapped{};
    result = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map of the staging texture: " + D3d10Tools::hr(result);
        return false;
    }
    out.width = desc.Width;
    out.height = desc.Height;
    out.depth = 1;
    out.rows = desc.Height;
    out.row_bytes = desc.Width * bytes;
    out.data.resize(static_cast<std::size_t>(out.row_bytes) * out.rows);
    for (UINT row = 0; row < out.rows; ++row) {
        std::memcpy(out.data.data() + static_cast<std::size_t>(row) * out.row_bytes,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, out.row_bytes);
    }
    staging->Unmap(0);
    return true;
}

bool Kit::end_frame(std::string& error) { return swap.present(error); }

void Kit::set_viewport(UINT width, UINT height) { D3d10Tools::set_viewport(dev, width, height); }

}  // namespace lsw::suites::render::d3d10
