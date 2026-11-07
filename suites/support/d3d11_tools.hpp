#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d11_device.hpp"

#include <d3d11_4.h>
#include <d3dcommon.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct TexelImage {
    UINT width = 0;
    UINT height = 0;
    UINT depth = 1;
    UINT row_bytes = 0;
    UINT rows = 0;
    std::vector<std::uint8_t> data;

    [[nodiscard]] std::uint32_t pixel(UINT x, UINT y, UINT z = 0) const;
};

class D3d11Tools {
public:
    static constexpr const char* kFullscreenVs =
        "float4 main(uint id : SV_VertexID) : SV_Position {\n"
        "  float2 p = float2((id << 1) & 2, id & 2);\n"
        "  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "}\n";

    [[nodiscard]] static std::string hr(HRESULT result);

    [[nodiscard]] static std::string profile(const D3d11Device& device, const char* stage);

    [[nodiscard]] static bool compile(const std::string& source, const char* entry, const std::string& target,
                                      std::vector<std::uint8_t>& code, std::string& error,
                                      const D3D_SHADER_MACRO* macros = nullptr);

    [[nodiscard]] static Com<ID3D11VertexShader> vs(D3d11Device& device, const std::string& source, std::string& error,
                                                    const char* entry = "main",
                                                    const D3D_SHADER_MACRO* macros = nullptr);
    [[nodiscard]] static Com<ID3D11PixelShader> ps(D3d11Device& device, const std::string& source, std::string& error,
                                                   const char* entry = "main",
                                                   const D3D_SHADER_MACRO* macros = nullptr);
    [[nodiscard]] static Com<ID3D11GeometryShader> gs(D3d11Device& device, const std::string& source,
                                                      std::string& error, const char* entry = "main",
                                                      const D3D_SHADER_MACRO* macros = nullptr);
    [[nodiscard]] static Com<ID3D11HullShader> hs(D3d11Device& device, const std::string& source, std::string& error,
                                                  const char* entry = "main", const D3D_SHADER_MACRO* macros = nullptr);
    [[nodiscard]] static Com<ID3D11DomainShader> ds(D3d11Device& device, const std::string& source,
                                                    std::string& error, const char* entry = "main",
                                                    const D3D_SHADER_MACRO* macros = nullptr);
    [[nodiscard]] static Com<ID3D11ComputeShader> cs(D3d11Device& device, const std::string& source,
                                                     std::string& error, const char* entry = "main",
                                                     const D3D_SHADER_MACRO* macros = nullptr);

    [[nodiscard]] static std::vector<std::uint8_t> vs_code(D3d11Device& device, const std::string& source,
                                                           std::string& error, const char* entry = "main");

    [[nodiscard]] static Com<ID3D11Buffer> buffer(D3d11Device& device, UINT size, D3D11_USAGE usage, UINT bind,
                                                  UINT cpu, UINT misc, UINT stride, const void* initial,
                                                  HRESULT* result = nullptr);

    [[nodiscard]] static D3D11_TEXTURE2D_DESC desc2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind,
                                                     D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0,
                                                     UINT mips = 1, UINT array_size = 1, UINT samples = 1);

    [[nodiscard]] static Com<ID3D11Texture2D> texture(D3d11Device& device, const D3D11_TEXTURE2D_DESC& desc,
                                                      const D3D11_SUBRESOURCE_DATA* initial = nullptr,
                                                      HRESULT* result = nullptr);

    [[nodiscard]] static Com<ID3D11Texture2D> target(D3d11Device& device, UINT width, UINT height,
                                                     DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                                     UINT extra_bind = D3D11_BIND_SHADER_RESOURCE);

    [[nodiscard]] static Com<ID3D11RenderTargetView> rtv(D3d11Device& device, ID3D11Resource* resource,
                                                         DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    [[nodiscard]] static Com<ID3D11ShaderResourceView> srv(D3d11Device& device, ID3D11Resource* resource,
                                                           DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    [[nodiscard]] static Com<ID3D11UnorderedAccessView> uav(D3d11Device& device, ID3D11Resource* resource,
                                                            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    [[nodiscard]] static Com<ID3D11UnorderedAccessView> raw_uav(D3d11Device& device, ID3D11Buffer* buffer, UINT flags = 0);

    [[nodiscard]] static Com<ID3D11UnorderedAccessView> structured_uav(D3d11Device& device, ID3D11Buffer* buffer,
                                                                       UINT elements, UINT flags = 0);

    [[nodiscard]] static Com<ID3D11ShaderResourceView> raw_srv(D3d11Device& device, ID3D11Buffer* buffer);

    [[nodiscard]] static Com<ID3D11ShaderResourceView> structured_srv(D3d11Device& device, ID3D11Buffer* buffer,
                                                                      UINT first, UINT elements);

    [[nodiscard]] static bool read_buffer(D3d11Device& device, ID3D11Buffer* buffer, std::vector<std::uint8_t>& out,
                                          std::string& error);

    [[nodiscard]] static bool read_texture(D3d11Device& device, ID3D11Texture2D* texture, UINT subresource,
                                           TexelImage& out, std::string& error);

    [[nodiscard]] static bool read_volume(D3d11Device& device, ID3D11Texture3D* texture, UINT subresource,
                                          TexelImage& out, std::string& error);

    [[nodiscard]] static bool read_pixels(D3d11Device& device, ID3D11Texture2D* texture, std::vector<std::uint8_t>& out,
                                          std::string& error);

    [[nodiscard]] static std::vector<std::uint32_t> read_words(D3d11Device& device, ID3D11Buffer* buffer,
                                                               std::string& error);

    static void set_viewport(D3d11Device& device, UINT width, UINT height);

    static void clear(D3d11Device& device, ID3D11RenderTargetView* view, float r, float g, float b, float a);

    [[nodiscard]] static bool draw_fullscreen(D3d11Device& device, ID3D11PixelShader* pixel_shader,
                                              ID3D11RenderTargetView* view, UINT width, UINT height,
                                              std::string& error);

    static void reset_state(D3d11Device& device);

    [[nodiscard]] static bool wait_event(D3d11Device& device, ID3D11DeviceContext* context, std::string& error);

    [[nodiscard]] static std::vector<std::uint8_t> block_bytes(const void* data, UINT row_bytes, UINT rows, UINT pitch);
};
}  // namespace lsw::suites
