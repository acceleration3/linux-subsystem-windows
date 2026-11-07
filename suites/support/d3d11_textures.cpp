#include "support/d3d11_textures.hpp"

#include "core/text.hpp"

#include <cstring>

namespace lsw::suites {

UINT D3d11Textures::bytes_per_pixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM:
        return 1;
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_UINT:
        return 16;
    default:
        return 4;
    }
}

ID3D11Texture2D* D3d11Textures::make2d(D3d11Device& device, UINT width, UINT height, DXGI_FORMAT format,
                                       D3D11_USAGE usage, UINT bind, UINT cpu, HRESULT& result) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    ID3D11Texture2D* texture = nullptr;
    result = device.device->CreateTexture2D(&desc, nullptr, &texture);
    return SUCCEEDED(result) ? texture : nullptr;
}

ID3D11Texture3D* D3d11Textures::make3d(D3d11Device& device, UINT width, UINT height, UINT depth, DXGI_FORMAT format,
                                       HRESULT& result) {
    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Depth = depth;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture3D* texture = nullptr;
    result = device.device->CreateTexture3D(&desc, nullptr, &texture);
    return SUCCEEDED(result) ? texture : nullptr;
}

bool D3d11Textures::read2d(D3d11Device& device, ID3D11Texture2D* texture, UINT width, UINT height,
                           UINT bytes_per_pixel_value, std::vector<std::uint8_t>& tight, std::string& error) {
    HRESULT result = S_OK;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    format = desc.Format;
    ID3D11Texture2D* staging = make2d(device, width, height, format, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, result);
    if (staging == nullptr) {
        error = "staging texture creation failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return false;
    }
    device.context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        staging->Release();
        error = "Map failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
        return false;
    }
    tight.assign(static_cast<std::size_t>(width) * height * bytes_per_pixel_value, 0);
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(tight.data() + static_cast<std::size_t>(y) * width * bytes_per_pixel_value,
                    static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(width) * bytes_per_pixel_value);
    }
    device.context->Unmap(staging, 0);
    staging->Release();
    return true;
}

bool D3d11Textures::read3d(D3d11Device& device, ID3D11Texture3D* texture, UINT width, UINT height, UINT depth,
                           UINT bytes_per_pixel_value, std::vector<std::uint8_t>& tight, std::string& error) {
    D3D11_TEXTURE3D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture3D* staging = nullptr;
    HRESULT result = device.device->CreateTexture3D(&desc, nullptr, &staging);
    if (FAILED(result)) {
        error = "staging volume creation failed";
        return false;
    }
    device.context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        staging->Release();
        error = "Map failed";
        return false;
    }
    tight.assign(static_cast<std::size_t>(width) * height * depth * bytes_per_pixel_value, 0);
    for (UINT z = 0; z < depth; ++z) {
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(tight.data() + (static_cast<std::size_t>(z) * height + y) * width * bytes_per_pixel_value,
                        static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(z) * mapped.DepthPitch +
                            static_cast<std::size_t>(y) * mapped.RowPitch,
                        static_cast<std::size_t>(width) * bytes_per_pixel_value);
        }
    }
    device.context->Unmap(staging, 0);
    staging->Release();
    return true;
}

}  // namespace lsw::suites
