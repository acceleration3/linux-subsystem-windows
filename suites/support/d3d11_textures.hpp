#pragma once

#include "support/d3d11_device.hpp"

#include <cstdint>
#include <vector>

namespace lsw::suites {
class D3d11Textures {
public:
    [[nodiscard]] static ID3D11Texture2D* make2d(D3d11Device& device, UINT width, UINT height, DXGI_FORMAT format,
                                                 D3D11_USAGE usage, UINT bind, UINT cpu, HRESULT& result);

    [[nodiscard]] static ID3D11Texture3D* make3d(D3d11Device& device, UINT width, UINT height, UINT depth,
                                                 DXGI_FORMAT format, HRESULT& result);

    [[nodiscard]] static bool read2d(D3d11Device& device, ID3D11Texture2D* texture, UINT width, UINT height,
                                     UINT bytes_per_pixel, std::vector<std::uint8_t>& tight, std::string& error);

    [[nodiscard]] static bool read3d(D3d11Device& device, ID3D11Texture3D* texture, UINT width, UINT height,
                                     UINT depth, UINT bytes_per_pixel, std::vector<std::uint8_t>& tight,
                                     std::string& error);

    [[nodiscard]] static UINT bytes_per_pixel(DXGI_FORMAT format);
};
}  // namespace lsw::suites
