#include "support/d3d9_tools.hpp"

#include "core/text.hpp"
#include "support/d3d11_tools.hpp"
#include "support/pattern.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace lsw::suites {

namespace {

std::uint32_t expand(std::uint32_t value, int bits) {
    if (bits == 0) {
        return 255;
    }
    const std::uint32_t max = (1u << bits) - 1;
    return value * 255u / max;
}

bool decode(const std::uint8_t* row, D3DFORMAT format, UINT width, std::uint8_t* out) {
    for (UINT x = 0; x < width; ++x) {
        std::uint32_t b = 0;
        std::uint32_t g = 0;
        std::uint32_t r = 0;
        std::uint32_t a = 255;
        switch (format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: {
            std::uint32_t v = 0;
            std::memcpy(&v, row + x * 4, 4);
            b = v & 255;
            g = (v >> 8) & 255;
            r = (v >> 16) & 255;
            a = format == D3DFMT_A8R8G8B8 ? (v >> 24) : 255;
            break;
        }
        case D3DFMT_A2R10G10B10: {
            std::uint32_t v = 0;
            std::memcpy(&v, row + x * 4, 4);
            b = (v & 1023) >> 2;
            g = ((v >> 10) & 1023) >> 2;
            r = ((v >> 20) & 1023) >> 2;
            a = expand(v >> 30, 2);
            break;
        }
        case D3DFMT_R5G6B5: {
            std::uint16_t v = 0;
            std::memcpy(&v, row + x * 2, 2);
            b = expand(v & 31u, 5);
            g = expand((v >> 5) & 63u, 6);
            r = expand((v >> 11) & 31u, 5);
            break;
        }
        case D3DFMT_A1R5G5B5:
        case D3DFMT_X1R5G5B5: {
            std::uint16_t v = 0;
            std::memcpy(&v, row + x * 2, 2);
            b = expand(v & 31u, 5);
            g = expand((v >> 5) & 31u, 5);
            r = expand((v >> 10) & 31u, 5);
            a = format == D3DFMT_A1R5G5B5 ? expand(static_cast<std::uint32_t>(v >> 15), 1) : 255;
            break;
        }
        case D3DFMT_A4R4G4B4: {
            std::uint16_t v = 0;
            std::memcpy(&v, row + x * 2, 2);
            b = expand(v & 15u, 4);
            g = expand((v >> 4) & 15u, 4);
            r = expand((v >> 8) & 15u, 4);
            a = expand((v >> 12) & 15u, 4);
            break;
        }
        default:
            return false;
        }
        out[x * 4 + 0] = static_cast<std::uint8_t>(b);
        out[x * 4 + 1] = static_cast<std::uint8_t>(g);
        out[x * 4 + 2] = static_cast<std::uint8_t>(r);
        out[x * 4 + 3] = static_cast<std::uint8_t>(a);
    }
    return true;
}

}  // namespace

std::string D3d9Tools::hr(HRESULT result) {
    const char* name = nullptr;
    switch (static_cast<std::uint32_t>(result)) {
    case 0: name = "D3D_OK"; break;
    case 1: name = "S_FALSE"; break;
    case 0x8876086cu: name = "D3DERR_INVALIDCALL"; break;
    case 0x88760b59u: name = "D3DERR_DEVICELOST"; break;
    case 0x88760b5au: name = "D3DERR_DEVICENOTRESET"; break;
    case 0x8876017cu: name = "D3DERR_OUTOFVIDEOMEMORY"; break;
    case 0x8007000eu: name = "E_OUTOFMEMORY"; break;
    case 0x8876086au: name = "D3DERR_NOTAVAILABLE"; break;
    case 0x8876021au: name = "D3DERR_WASSTILLDRAWING"; break;
    case 0x88760870u: name = "D3DERR_DEVICEHUNG"; break;
    case 0x88760869u: name = "D3DERR_DEVICEREMOVED"; break;
    case 0x8876086bu: name = "D3DERR_MOREDATA"; break;
    default: break;
    }
    return std::string(name != nullptr ? name : "") + "(0x" + Text::hex(static_cast<std::uint32_t>(result), 8) + ")";
}

bool D3d9Tools::compile(const std::string& source, const char* profile, std::vector<std::uint8_t>& code,
                        std::string& error, const D3D_SHADER_MACRO* macros, const char* entry) {
    return D3d11Tools::compile(source, entry, profile, code, error, macros);
}

Com<IDirect3DVertexShader9> D3d9Tools::vs(IDirect3DDevice9* device, const std::string& source, const char* profile,
                                          std::string& error, const D3D_SHADER_MACRO* macros) {
    std::vector<std::uint8_t> code;
    Com<IDirect3DVertexShader9> shader;
    if (!compile(source, profile, code, error, macros)) {
        return shader;
    }
    const HRESULT result = device->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), shader.put());
    if (FAILED(result)) {
        error = std::string("CreateVertexShader(") + profile + ") failed: " + hr(result);
    }
    return shader;
}

Com<IDirect3DPixelShader9> D3d9Tools::ps(IDirect3DDevice9* device, const std::string& source, const char* profile,
                                         std::string& error, const D3D_SHADER_MACRO* macros) {
    std::vector<std::uint8_t> code;
    Com<IDirect3DPixelShader9> shader;
    if (!compile(source, profile, code, error, macros)) {
        return shader;
    }
    const HRESULT result = device->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), shader.put());
    if (FAILED(result)) {
        error = std::string("CreatePixelShader(") + profile + ") failed: " + hr(result);
    }
    return shader;
}

Com<IDirect3DSurface9> D3d9Tools::target(IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format,
                                         D3DMULTISAMPLE_TYPE samples, bool lockable, HRESULT* result, DWORD quality) {
    Com<IDirect3DSurface9> surface;
    const HRESULT made = device->CreateRenderTarget(width, height, format, samples, quality, lockable ? TRUE : FALSE,
                                                    surface.put(), nullptr);
    if (result != nullptr) {
        *result = made;
    }
    return surface;
}

Com<IDirect3DTexture9> D3d9Tools::texture(IDirect3DDevice9* device, UINT width, UINT height, UINT levels, DWORD usage,
                                          D3DFORMAT format, D3DPOOL pool, HRESULT* result, HANDLE* shared) {
    Com<IDirect3DTexture9> out;
    const HRESULT made = device->CreateTexture(width, height, levels, usage, format, pool, out.put(), shared);
    if (result != nullptr) {
        *result = made;
    }
    return out;
}

Com<IDirect3DSurface9> D3d9Tools::level(IDirect3DTexture9* texture, UINT index) {
    Com<IDirect3DSurface9> surface;
    texture->GetSurfaceLevel(index, surface.put());
    return surface;
}

bool D3d9Tools::read_surface(IDirect3DDevice9* device, IDirect3DSurface9* surface, SceneImage& out, std::string& error) {
    D3DSURFACE_DESC desc{};
    surface->GetDesc(&desc);
    Com<IDirect3DSurface9> copy;
    IDirect3DSurface9* source = surface;
    if (desc.Pool != D3DPOOL_SYSTEMMEM) {
        HRESULT result = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, copy.put(), nullptr);
        if (FAILED(result)) {
            error = "CreateOffscreenPlainSurface for the readback failed: " + hr(result);
            return false;
        }
        result = device->GetRenderTargetData(surface, copy.get());
        if (FAILED(result)) {
            error = "GetRenderTargetData failed: " + hr(result);
            return false;
        }
        source = copy.get();
    }
    D3DLOCKED_RECT locked{};
    const HRESULT result = source->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(result)) {
        error = "LockRect (readback) failed: " + hr(result);
        return false;
    }
    out.width = desc.Width;
    out.height = desc.Height;
    out.rgba = false;
    out.data.assign(static_cast<std::size_t>(desc.Width) * desc.Height * 4, 0);
    bool ok = true;
    for (UINT y = 0; y < desc.Height && ok; ++y) {
        ok = decode(static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), desc.Format, desc.Width,
                    out.data.data() + static_cast<std::size_t>(y) * desc.Width * 4);
    }
    source->UnlockRect();
    if (!ok) {
        error = "the surface format " + std::to_string(static_cast<int>(desc.Format)) + " cannot be decoded";
    }
    return ok;
}

bool D3d9Tools::wait_event(IDirect3DDevice9* device, std::string& error) {
    Com<IDirect3DQuery9> query;
    HRESULT result = device->CreateQuery(D3DQUERYTYPE_EVENT, query.put());
    if (FAILED(result)) {
        error = "CreateQuery(EVENT) failed: " + hr(result);
        return false;
    }
    query->Issue(D3DISSUE_END);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    BOOL done = FALSE;
    while ((result = query->GetData(&done, sizeof(done), D3DGETDATA_FLUSH)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            error = "the event query did not complete in 60 s";
            return false;
        }
        ::SwitchToThread();
    }
    if (FAILED(result)) {
        error = "GetData(EVENT) failed: " + hr(result);
        return false;
    }
    return true;
}

void D3d9Tools::clear(IDirect3DDevice9* device, D3DCOLOR color) {
    device->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0);
}

void D3d9Tools::sampler(IDirect3DDevice9* device, DWORD stage, bool linear) {
    const DWORD filter = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    device->SetSamplerState(stage, D3DSAMP_MINFILTER, filter);
    device->SetSamplerState(stage, D3DSAMP_MAGFILTER, filter);
    device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0);
    device->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, 0);
    device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE);
}

void D3d9Tools::prepare(IDirect3DDevice9* device, bool textured) {
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_DEPTHBIAS, 0);
    device->SetRenderState(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFFu);
    device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    sampler(device, 0, false);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, textured ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, textured ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    if (!textured) {
        device->SetTexture(0, nullptr);
    }
    const D3DMATRIX unit = identity();
    device->SetTransform(D3DTS_WORLD, &unit);
    device->SetTransform(D3DTS_VIEW, &unit);
    device->SetTransform(D3DTS_PROJECTION, &unit);
}

void D3d9Tools::tl_quad(TlVertex out[4], float left, float top, float right, float bottom, D3DCOLOR color, float u0,
                        float v0, float u1, float v1, float z) {
    const float l = left - 0.5f;
    const float t = top - 0.5f;
    const float r = right - 0.5f;
    const float b = bottom - 0.5f;
    out[0] = {l, t, z, 1.0f, color, u0, v0};
    out[1] = {r, t, z, 1.0f, color, u1, v0};
    out[2] = {l, b, z, 1.0f, color, u0, v1};
    out[3] = {r, b, z, 1.0f, color, u1, v1};
}

bool D3d9Tools::draw_quad(IDirect3DDevice9* device, float left, float top, float right, float bottom, D3DCOLOR color,
                          float z) {
    TlVertex vertices[4];
    tl_quad(vertices, left, top, right, bottom, color, 0, 0, 1, 1, z);
    device->SetFVF(kTlFvf);
    if (FAILED(device->BeginScene())) {
        return false;
    }
    const HRESULT result = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(TlVertex));
    device->EndScene();
    return SUCCEEDED(result);
}

bool D3d9Tools::draw_textured(IDirect3DDevice9* device, float left, float top, float right, float bottom, float u0,
                              float v0, float u1, float v1) {
    TlVertex vertices[4];
    tl_quad(vertices, left, top, right, bottom, 0xFFFFFFFFu, u0, v0, u1, v1);
    device->SetFVF(kTlFvf);
    if (FAILED(device->BeginScene())) {
        return false;
    }
    const HRESULT result = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(TlVertex));
    device->EndScene();
    return SUCCEEDED(result);
}

bool D3d9Tools::sample(IDirect3DDevice9* device, IDirect3DBaseTexture9* texture, UINT width, UINT height,
                       SceneImage& out, std::string& error, bool linear) {
    Com<IDirect3DSurface9> saved_target;
    Com<IDirect3DSurface9> saved_depth;
    device->GetRenderTarget(0, saved_target.put());
    device->GetDepthStencilSurface(saved_depth.put());
    HRESULT result = S_OK;
    Com<IDirect3DSurface9> rt = target(device, width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
    if (!rt) {
        error = "the sampling target cannot be created: " + hr(result);
        return false;
    }
    device->SetRenderTarget(0, rt.get());
    device->SetDepthStencilSurface(nullptr);
    prepare(device, true);
    sampler(device, 0, linear);
    device->SetTexture(0, texture);
    const bool drawn = draw_textured(device, 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 0, 1, 1);
    device->SetTexture(0, nullptr);
    const bool read = drawn && read_surface(device, rt.get(), out, error);
    device->SetRenderTarget(0, saved_target.get());
    device->SetDepthStencilSurface(saved_depth.get());
    if (!drawn) {
        error = "drawing the sampled quad failed";
    }
    return read;
}

D3DMATRIX D3d9Tools::identity() {
    D3DMATRIX m{};
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    return m;
}

D3DMATRIX D3d9Tools::half_pixel_projection(UINT width, UINT height) {
    D3DMATRIX m = identity();
    m._41 = -1.0f / static_cast<float>(width);
    m._42 = 1.0f / static_cast<float>(height);
    return m;
}

std::uint32_t D3d9Tools::pixel(const SceneImage& image, UINT x, UINT y) {
    std::uint32_t value = 0;
    std::memcpy(&value, image.data.data() + (static_cast<std::size_t>(y) * image.width + x) * 4, 4);
    return value;
}

std::vector<std::uint32_t> D3d9Tools::pattern(std::uint64_t seed, UINT width, UINT height) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * height);
    Pattern::fill_pixels(seed, width, height, width * 4, out.data());
    return out;
}

bool D3d9Tools::write_surface(IDirect3DSurface9* surface, const RECT* rect, const std::vector<std::uint32_t>& pixels,
                              UINT stride_pixels, std::string& error, DWORD flags) {
    D3DSURFACE_DESC desc{};
    surface->GetDesc(&desc);
    D3DLOCKED_RECT locked{};
    const HRESULT result = surface->LockRect(&locked, rect, flags);
    if (FAILED(result)) {
        error = "LockRect failed: " + hr(result);
        return false;
    }
    const UINT width = rect != nullptr ? static_cast<UINT>(rect->right - rect->left) : desc.Width;
    const UINT height = rect != nullptr ? static_cast<UINT>(rect->bottom - rect->top) : desc.Height;
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch),
                    pixels.data() + static_cast<std::size_t>(y) * stride_pixels, static_cast<std::size_t>(width) * 4);
    }
    surface->UnlockRect();
    return true;
}

bool D3d9Tools::write_texture_level(IDirect3DTexture9* texture, UINT index, std::uint64_t seed, std::string& error,
                                    DWORD flags) {
    D3DSURFACE_DESC desc{};
    if (FAILED(texture->GetLevelDesc(index, &desc))) {
        error = "GetLevelDesc failed";
        return false;
    }
    D3DLOCKED_RECT locked{};
    const HRESULT result = texture->LockRect(index, &locked, nullptr, flags);
    if (FAILED(result)) {
        error = "LockRect(level " + std::to_string(index) + ") failed: " + hr(result);
        return false;
    }
    const std::vector<std::uint32_t> pixels = pattern(seed, desc.Width, desc.Height);
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch),
                    pixels.data() + static_cast<std::size_t>(y) * desc.Width, static_cast<std::size_t>(desc.Width) * 4);
    }
    texture->UnlockRect(index);
    return true;
}

SceneImage D3d9Tools::image_of(UINT width, UINT height, const std::vector<std::uint32_t>& pixels) {
    SceneImage image;
    image.width = width;
    image.height = height;
    image.data.resize(pixels.size() * 4);
    std::memcpy(image.data.data(), pixels.data(), image.data.size());
    return image;
}

std::string D3d9Tools::color_text(std::uint32_t value) {
    return "0x" + Text::hex(value, 8);
}

bool D3d9Tools::same(const SceneImage& a, const SceneImage& b, int tolerance, std::string& where) {
    if (a.width != b.width || a.height != b.height || a.data.size() != b.data.size()) {
        where = "the images differ in size";
        return false;
    }
    for (std::size_t i = 0; i < a.data.size(); ++i) {
        if (std::abs(static_cast<int>(a.data[i]) - static_cast<int>(b.data[i])) > tolerance) {
            const std::size_t pixel_index = i / 4;
            where = "pixel (" + std::to_string(pixel_index % a.width) + "," + std::to_string(pixel_index / a.width) + ") is " +
                    color_text(pixel(b, static_cast<UINT>(pixel_index % a.width), static_cast<UINT>(pixel_index / a.width))) + ", expected " +
                    color_text(pixel(a, static_cast<UINT>(pixel_index % a.width), static_cast<UINT>(pixel_index / a.width)));
            return false;
        }
    }
    return true;
}

bool D3d9Tools::region(const SceneImage& image, UINT x0, UINT y0, UINT x1, UINT y1, std::uint32_t value, int tolerance,
                       std::string& where, bool ignore_alpha) {
    for (UINT y = y0; y < y1; ++y) {
        for (UINT x = x0; x < x1; ++x) {
            const std::uint32_t got = pixel(image, x, y);
            for (int c = 0; c < (ignore_alpha ? 3 : 4); ++c) {
                const int a = static_cast<int>((got >> (c * 8)) & 255u);
                const int b = static_cast<int>((value >> (c * 8)) & 255u);
                if (std::abs(a - b) > tolerance) {
                    where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + color_text(got) + ", expected " + color_text(value);
                    return false;
                }
            }
        }
    }
    return true;
}

bool D3d9Tools::uniform(const SceneImage& image, std::uint32_t value, int tolerance, std::string& where, bool ignore_alpha) {
    return region(image, 0, 0, image.width, image.height, value, tolerance, where, ignore_alpha);
}

bool D3d9Tools::wait_screen_color(HWND window, int dx, int dy, const int want[3], int tolerance, int timeout_ms, int got[3]) {
    POINT point{dx, dy};
    ::ClientToScreen(window, &point);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        HDC screen = ::GetDC(nullptr);
        if (screen != nullptr) {
            const COLORREF color = ::GetPixel(screen, point.x, point.y);
            ::ReleaseDC(nullptr, screen);
            if (color != CLR_INVALID) {
                got[0] = GetRValue(color);
                got[1] = GetGValue(color);
                got[2] = GetBValue(color);
                if (std::abs(got[0] - want[0]) <= tolerance && std::abs(got[1] - want[1]) <= tolerance &&
                    std::abs(got[2] - want[2]) <= tolerance) {
                    return true;
                }
            }
        }
        ::Sleep(20);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace lsw::suites
