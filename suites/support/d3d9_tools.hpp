#pragma once

#include "support/com_ptr.hpp"
#include "support/oracle_rig.hpp"

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct TlVertex {
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
    float u;
    float v;
};

class D3d9Tools {
public:
    static constexpr DWORD kTlFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    [[nodiscard]] static std::string hr(HRESULT result);

    [[nodiscard]] static bool compile(const std::string& source, const char* profile, std::vector<std::uint8_t>& code,
                                      std::string& error, const D3D_SHADER_MACRO* macros = nullptr,
                                      const char* entry = "main");

    [[nodiscard]] static Com<IDirect3DVertexShader9> vs(IDirect3DDevice9* device, const std::string& source,
                                                        const char* profile, std::string& error,
                                                        const D3D_SHADER_MACRO* macros = nullptr);

    [[nodiscard]] static Com<IDirect3DPixelShader9> ps(IDirect3DDevice9* device, const std::string& source,
                                                       const char* profile, std::string& error,
                                                       const D3D_SHADER_MACRO* macros = nullptr);

    [[nodiscard]] static Com<IDirect3DSurface9> target(IDirect3DDevice9* device, UINT width, UINT height,
                                                       D3DFORMAT format = D3DFMT_A8R8G8B8,
                                                       D3DMULTISAMPLE_TYPE samples = D3DMULTISAMPLE_NONE,
                                                       bool lockable = false, HRESULT* result = nullptr,
                                                       DWORD quality = 0);

    [[nodiscard]] static Com<IDirect3DTexture9> texture(IDirect3DDevice9* device, UINT width, UINT height, UINT levels,
                                                        DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                        HRESULT* result = nullptr, HANDLE* shared = nullptr);

    [[nodiscard]] static Com<IDirect3DSurface9> level(IDirect3DTexture9* texture, UINT index);

    [[nodiscard]] static bool read_surface(IDirect3DDevice9* device, IDirect3DSurface9* surface, SceneImage& out,
                                           std::string& error);

    [[nodiscard]] static bool wait_event(IDirect3DDevice9* device, std::string& error);

    static void clear(IDirect3DDevice9* device, D3DCOLOR color);

    static void prepare(IDirect3DDevice9* device, bool textured);

    static void sampler(IDirect3DDevice9* device, DWORD stage, bool linear);

    static void tl_quad(TlVertex out[4], float left, float top, float right, float bottom, D3DCOLOR color,
                        float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f, float z = 0.5f);

    [[nodiscard]] static bool draw_quad(IDirect3DDevice9* device, float left, float top, float right, float bottom,
                                        D3DCOLOR color, float z = 0.5f);

    [[nodiscard]] static bool draw_textured(IDirect3DDevice9* device, float left, float top, float right, float bottom,
                                            float u0, float v0, float u1, float v1);

    [[nodiscard]] static bool sample(IDirect3DDevice9* device, IDirect3DBaseTexture9* texture, UINT width, UINT height,
                                     SceneImage& out, std::string& error, bool linear = false);

    [[nodiscard]] static D3DMATRIX half_pixel_projection(UINT width, UINT height);

    [[nodiscard]] static D3DMATRIX identity();

    [[nodiscard]] static std::uint32_t pixel(const SceneImage& image, UINT x, UINT y);

    [[nodiscard]] static std::vector<std::uint32_t> pattern(std::uint64_t seed, UINT width, UINT height);

    [[nodiscard]] static bool write_surface(IDirect3DSurface9* surface, const RECT* rect,
                                            const std::vector<std::uint32_t>& pixels, UINT stride_pixels,
                                            std::string& error, DWORD flags = 0);

    [[nodiscard]] static bool write_texture_level(IDirect3DTexture9* texture, UINT index, std::uint64_t seed,
                                                  std::string& error, DWORD flags = 0);

    [[nodiscard]] static SceneImage image_of(UINT width, UINT height, const std::vector<std::uint32_t>& pixels);

    [[nodiscard]] static bool same(const SceneImage& a, const SceneImage& b, int tolerance, std::string& where);

    [[nodiscard]] static bool uniform(const SceneImage& image, std::uint32_t value, int tolerance, std::string& where,
                                      bool ignore_alpha = false);

    [[nodiscard]] static bool region(const SceneImage& image, UINT x0, UINT y0, UINT x1, UINT y1, std::uint32_t value,
                                     int tolerance, std::string& where, bool ignore_alpha = false);

    [[nodiscard]] static std::string color_text(std::uint32_t value);

    [[nodiscard]] static bool wait_screen_color(HWND window, int dx, int dy, const int want[3], int tolerance,
                                                int timeout_ms, int got[3]);
};
}  // namespace lsw::suites
