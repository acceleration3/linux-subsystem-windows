#include "support/d3d10_tools.hpp"

#include "support/d3d11_textures.hpp"

#include <cstring>

namespace lsw::suites {

bool D3d10Tools::compile(const std::string& source, const char* target, std::vector<std::uint8_t>& code, std::string& error) {
    return D3d11Tools::compile(source, "main", target, code, error);
}

Com<ID3D10VertexShader> D3d10Tools::vs(D3d10Device& device, const std::string& source, std::string& error, const char* target) {
    Com<ID3D10VertexShader> out;
    std::vector<std::uint8_t> code;
    if (!compile(source, target, code, error)) {
        return out;
    }
    const HRESULT result = device.device->CreateVertexShader(code.data(), code.size(), out.put());
    if (FAILED(result)) {
        error = "CreateVertexShader failed: " + hr(result);
    }
    return out;
}

Com<ID3D10PixelShader> D3d10Tools::ps(D3d10Device& device, const std::string& source, std::string& error, const char* target) {
    Com<ID3D10PixelShader> out;
    std::vector<std::uint8_t> code;
    if (!compile(source, target, code, error)) {
        return out;
    }
    const HRESULT result = device.device->CreatePixelShader(code.data(), code.size(), out.put());
    if (FAILED(result)) {
        error = "CreatePixelShader failed: " + hr(result);
    }
    return out;
}

Com<ID3D10GeometryShader> D3d10Tools::gs(D3d10Device& device, const std::string& source, std::string& error, const char* target) {
    Com<ID3D10GeometryShader> out;
    std::vector<std::uint8_t> code;
    if (!compile(source, target, code, error)) {
        return out;
    }
    const HRESULT result = device.device->CreateGeometryShader(code.data(), code.size(), out.put());
    if (FAILED(result)) {
        error = "CreateGeometryShader failed: " + hr(result);
    }
    return out;
}

std::vector<std::uint8_t> D3d10Tools::vs_code(const std::string& source, std::string& error, const char* target) {
    std::vector<std::uint8_t> code;
    if (!compile(source, target, code, error)) {
        code.clear();
    }
    return code;
}

Com<ID3D10Buffer> D3d10Tools::buffer(D3d10Device& device, UINT size, D3D10_USAGE usage, UINT bind, UINT cpu, UINT misc,
                                     const void* initial, HRESULT* result) {
    D3D10_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    desc.MiscFlags = misc;
    D3D10_SUBRESOURCE_DATA data{};
    data.pSysMem = initial;
    Com<ID3D10Buffer> out;
    const HRESULT made = device.device->CreateBuffer(&desc, initial != nullptr ? &data : nullptr, out.put());
    if (result != nullptr) {
        *result = made;
    }
    return out;
}

D3D10_TEXTURE2D_DESC D3d10Tools::desc2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind, D3D10_USAGE usage, UINT cpu,
                                        UINT mips, UINT array_size, UINT samples) {
    D3D10_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mips;
    desc.ArraySize = array_size;
    desc.Format = format;
    desc.SampleDesc.Count = samples;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    return desc;
}

Com<ID3D10Texture2D> D3d10Tools::texture(D3d10Device& device, const D3D10_TEXTURE2D_DESC& desc, const D3D10_SUBRESOURCE_DATA* initial,
                                         HRESULT* result) {
    Com<ID3D10Texture2D> out;
    const HRESULT made = device.device->CreateTexture2D(&desc, initial, out.put());
    if (result != nullptr) {
        *result = made;
    }
    return out;
}

Com<ID3D10Texture2D> D3d10Tools::target(D3d10Device& device, UINT width, UINT height, DXGI_FORMAT format) {
    return texture(device, desc2d(width, height, format, D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE));
}

Com<ID3D10RenderTargetView> D3d10Tools::rtv(D3d10Device& device, ID3D10Resource* resource) {
    Com<ID3D10RenderTargetView> out;
    device.device->CreateRenderTargetView(resource, nullptr, out.put());
    return out;
}

bool D3d10Tools::read_texture(D3d10Device& device, ID3D10Texture2D* texture, TexelImage& out, std::string& error) {
    if (texture == nullptr) {
        error = "there is no texture to read back";
        return false;
    }
    D3D10_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    D3D10_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D10_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    Com<ID3D10Texture2D> staging = D3d10Tools::texture(device, staging_desc);
    if (!staging) {
        error = "the staging texture cannot be created";
        return false;
    }
    device.device->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture, 0, nullptr);
    D3D10_MAPPED_TEXTURE2D mapped{};
    const HRESULT result = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map(READ) of the staging texture failed: " + hr(result);
        return false;
    }
    const UINT bytes = D3d11Textures::bytes_per_pixel(desc.Format);
    out.width = desc.Width;
    out.height = desc.Height;
    out.depth = 1;
    out.row_bytes = desc.Width * bytes;
    out.rows = desc.Height;
    out.data.resize(static_cast<std::size_t>(out.row_bytes) * out.rows);
    for (UINT row = 0; row < out.rows; ++row) {
        std::memcpy(out.data.data() + static_cast<std::size_t>(row) * out.row_bytes,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, out.row_bytes);
    }
    staging->Unmap(0);
    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        for (std::size_t i = 0; i + 3 < out.data.size(); i += 4) {
            std::swap(out.data[i], out.data[i + 2]);
        }
    }
    return true;
}

bool D3d10Tools::read_buffer(D3d10Device& device, ID3D10Buffer* buffer, std::vector<std::uint8_t>& out, std::string& error) {
    D3D10_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    Com<ID3D10Buffer> staging = D3d10Tools::buffer(device, desc.ByteWidth, D3D10_USAGE_STAGING, 0, D3D10_CPU_ACCESS_READ, 0, nullptr);
    if (!staging) {
        error = "the staging buffer cannot be created";
        return false;
    }
    device.device->CopyResource(staging.get(), buffer);
    void* mapped = nullptr;
    const HRESULT result = staging->Map(D3D10_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map(READ) of the staging buffer failed: " + hr(result);
        return false;
    }
    out.assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + desc.ByteWidth);
    staging->Unmap();
    return true;
}

void D3d10Tools::set_viewport(D3d10Device& device, UINT width, UINT height) {
    D3D10_VIEWPORT viewport{};
    viewport.Width = width;
    viewport.Height = height;
    viewport.MaxDepth = 1.0f;
    device.device->RSSetViewports(1, &viewport);
}

void D3d10Tools::clear(D3d10Device& device, ID3D10RenderTargetView* view, float r, float g, float b, float a) {
    const float color[4] = {r, g, b, a};
    device.device->ClearRenderTargetView(view, color);
}

}  // namespace lsw::suites
