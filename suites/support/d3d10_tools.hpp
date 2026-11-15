#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d10_device.hpp"
#include "support/d3d11_tools.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class D3d10Tools {
public:
    [[nodiscard]] static std::string hr(HRESULT result) { return D3d11Tools::hr(result); }

    [[nodiscard]] static bool compile(const std::string& source, const char* target, std::vector<std::uint8_t>& code,
                                      std::string& error);

    [[nodiscard]] static Com<ID3D10VertexShader> vs(D3d10Device& device, const std::string& source, std::string& error,
                                                    const char* target = "vs_4_0");

    [[nodiscard]] static Com<ID3D10PixelShader> ps(D3d10Device& device, const std::string& source, std::string& error,
                                                   const char* target = "ps_4_0");

    [[nodiscard]] static Com<ID3D10GeometryShader> gs(D3d10Device& device, const std::string& source, std::string& error,
                                                      const char* target = "gs_4_0");

    [[nodiscard]] static std::vector<std::uint8_t> vs_code(const std::string& source, std::string& error,
                                                           const char* target = "vs_4_0");

    [[nodiscard]] static Com<ID3D10Buffer> buffer(D3d10Device& device, UINT size, D3D10_USAGE usage, UINT bind, UINT cpu,
                                                  UINT misc, const void* initial, HRESULT* result = nullptr);

    [[nodiscard]] static D3D10_TEXTURE2D_DESC desc2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind,
                                                     D3D10_USAGE usage = D3D10_USAGE_DEFAULT, UINT cpu = 0, UINT mips = 1,
                                                     UINT array_size = 1, UINT samples = 1);

    [[nodiscard]] static Com<ID3D10Texture2D> texture(D3d10Device& device, const D3D10_TEXTURE2D_DESC& desc,
                                                      const D3D10_SUBRESOURCE_DATA* initial = nullptr,
                                                      HRESULT* result = nullptr);

    [[nodiscard]] static Com<ID3D10Texture2D> target(D3d10Device& device, UINT width, UINT height,
                                                     DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

    [[nodiscard]] static Com<ID3D10RenderTargetView> rtv(D3d10Device& device, ID3D10Resource* resource);

    [[nodiscard]] static bool read_texture(D3d10Device& device, ID3D10Texture2D* texture, TexelImage& out,
                                           std::string& error);

    [[nodiscard]] static bool read_buffer(D3d10Device& device, ID3D10Buffer* buffer, std::vector<std::uint8_t>& out,
                                          std::string& error);

    static void set_viewport(D3d10Device& device, UINT width, UINT height);

    static void clear(D3d10Device& device, ID3D10RenderTargetView* view, float r, float g, float b, float a);

    [[nodiscard]] static std::uint32_t bgra(int r, int g, int b, int a) {
        return static_cast<std::uint32_t>(b) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(r) << 16) |
               (static_cast<std::uint32_t>(a) << 24);
    }
};
}  // namespace lsw::suites
