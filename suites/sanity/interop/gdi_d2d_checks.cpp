#include "sanity/interop/gdi_d2d_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/dyn_library.hpp"
#include "support/oracle_rig.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <cmath>

namespace lsw::suites::sanity::interop {

namespace {

constexpr UINT kEdge = 128;

using CreateD2d = HRESULT(WINAPI*)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, void**);
using CreateDWrite = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

struct Direct2d {
    DynLibrary library;
    Com<ID2D1Factory> factory;

    [[nodiscard]] bool open(std::string& error) {
        CreateD2d create = library.load("d2d1.dll") ? library.symbol<CreateD2d>("D2D1CreateFactory") : nullptr;
        if (create == nullptr) {
            error = "d2d1.dll or D2D1CreateFactory is not available";
            return false;
        }
        const HRESULT result = create(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, factory.put_void());
        if (FAILED(result)) {
            error = "D2D1CreateFactory returned " + Kit::hr(result);
            return false;
        }
        return true;
    }

    [[nodiscard]] Com<ID2D1RenderTarget> target(IDXGISurface* surface, std::string& error) {
        D2D1_RENDER_TARGET_PROPERTIES props{};
        props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        props.pixelFormat = {DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED};
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        props.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;
        props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        Com<ID2D1RenderTarget> out;
        const HRESULT result = factory->CreateDxgiSurfaceRenderTarget(surface, &props, out.put());
        if (FAILED(result)) {
            error = "CreateDxgiSurfaceRenderTarget returned " + Kit::hr(result);
        }
        return out;
    }
};

Com<ID3D11Texture2D> gdi_texture(D3d11Device& device) {
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    return D3d11Tools::texture(device, desc);
}

bool finish_scene(D3d11Device& device, ID3D11Texture2D* texture, SceneImage& out, std::string& error) {
    if (!D3d11Tools::wait_event(device, device.context, error)) {
        return false;
    }
    TexelImage image;
    if (!D3d11Tools::read_texture(device, texture, 0, image, error)) {
        return false;
    }
    out.width = kEdge;
    out.height = kEdge;
    out.rgba = false;
    out.data = image.data;
    return true;
}

D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

}  // namespace

Outcome GdiD2dChecks::gdi_interop_target(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error, D3D11_CREATE_DEVICE_BGRA_SUPPORT)) {
        return Outcome::fail(error);
    }
    Direct2d d2d;
    if (!d2d.open(error)) {
        return Context::skip(error);
    }
    Problems problems;
    rig.compare(
        context, problems, "d2d_gdi_sequence",
        [&](D3d11Device& device, SceneImage& out, std::string& scene_error) -> bool {
            Com<ID3D11Texture2D> texture = gdi_texture(device);
            Com<IDXGISurface> surface = Kit::query<IDXGISurface>(texture.get());
            if (!texture || !surface) {
                scene_error = "a GDI compatible BGRA render target cannot be created";
                return false;
            }
            Com<ID2D1RenderTarget> rt = d2d.target(surface.get(), scene_error);
            Com<ID2D1GdiInteropRenderTarget> gdi = Kit::query<ID2D1GdiInteropRenderTarget>(rt.get());
            if (!rt || !gdi) {
                if (scene_error.empty()) {
                    scene_error = "ID2D1GdiInteropRenderTarget is not exposed by the render target";
                }
                return false;
            }
            Com<ID2D1SolidColorBrush> blue;
            Com<ID2D1SolidColorBrush> half_red;
            Com<ID2D1SolidColorBrush> white;
            rt->CreateSolidColorBrush(color(0.2f, 0.5f, 0.8f), blue.put());
            rt->CreateSolidColorBrush(color(0.9f, 0.1f, 0.1f, 0.5f), half_red.put());
            rt->CreateSolidColorBrush(color(1.0f, 1.0f, 1.0f), white.put());
            rt->BeginDraw();
            rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            rt->Clear(color(0.1f, 0.12f, 0.15f));
            rt->FillRectangle(D2D1::RectF(8, 8, 72, 72), blue.get());
            rt->FillRectangle(D2D1::RectF(40, 40, 104, 104), half_red.get());
            HDC dc = nullptr;
            HRESULT result = gdi->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc);
            if (FAILED(result) || dc == nullptr) {
                rt->EndDraw();
                scene_error = "ID2D1GdiInteropRenderTarget::GetDC returned " + Kit::hr(result);
                return false;
            }
            HBRUSH brush = ::CreateSolidBrush(RGB(200, 30, 90));
            RECT patch{80, 8, 120, 36};
            ::FillRect(dc, &patch, brush);
            ::DeleteObject(brush);
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, RGB(255, 255, 0));
            ::TextOutA(dc, 10, 100, "D2D GDI", 7);
            RECT dirty{0, 0, static_cast<LONG>(kEdge), static_cast<LONG>(kEdge)};
            result = gdi->ReleaseDC(&dirty);
            if (FAILED(result)) {
                rt->EndDraw();
                scene_error = "ID2D1GdiInteropRenderTarget::ReleaseDC returned " + Kit::hr(result);
                return false;
            }
            rt->DrawLine(D2D1::Point2F(0, 120), D2D1::Point2F(128, 120), white.get(), 1.0f);
            rt->FillRectangle(D2D1::RectF(100, 100, 124, 124), blue.get());
            result = rt->EndDraw();
            if (FAILED(result)) {
                scene_error = "EndDraw returned " + Kit::hr(result);
                return false;
            }
            return finish_scene(device, texture.get(), out, scene_error);
        },
        Tolerance::bytes(1));
    return problems.outcome();
}

Outcome GdiD2dChecks::dwrite_text_on_surface(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error, D3D11_CREATE_DEVICE_BGRA_SUPPORT)) {
        return Outcome::fail(error);
    }
    Direct2d d2d;
    if (!d2d.open(error)) {
        return Context::skip(error);
    }
    DynLibrary dwrite_library;
    CreateDWrite create = dwrite_library.load("dwrite.dll") ? dwrite_library.symbol<CreateDWrite>("DWriteCreateFactory") : nullptr;
    Com<IDWriteFactory> dwrite;
    if (create == nullptr || FAILED(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.put_void())))) {
        return Context::skip("dwrite.dll or DWriteCreateFactory is not available");
    }
    const wchar_t text[] = L"Hello LSW 123 gjpqy";
    const UINT length = static_cast<UINT>(sizeof(text) / sizeof(text[0]) - 1);
    Com<IDWriteTextFormat> format;
    if (FAILED(dwrite->CreateTextFormat(L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", format.put()))) {
        return Context::skip("the Arial font is not available to DirectWrite");
    }
    Com<IDWriteRenderingParams> params;
    dwrite->CreateCustomRenderingParams(1.0f, 0.0f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_GDI_CLASSIC, params.put());
    Problems problems;
    rig.compare(
        context, problems, "dwrite_gdi_classic",
        [&](D3d11Device& device, SceneImage& out, std::string& scene_error) -> bool {
            Com<ID3D11Texture2D> texture = gdi_texture(device);
            Com<IDXGISurface> surface = Kit::query<IDXGISurface>(texture.get());
            if (!texture || !surface) {
                scene_error = "a GDI compatible BGRA render target cannot be created";
                return false;
            }
            Com<ID2D1RenderTarget> rt = d2d.target(surface.get(), scene_error);
            if (!rt) {
                return false;
            }
            Com<ID2D1SolidColorBrush> ink;
            rt->CreateSolidColorBrush(color(1.0f, 1.0f, 1.0f), ink.put());
            if (params) {
                rt->SetTextRenderingParams(params.get());
            }
            rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            rt->BeginDraw();
            rt->Clear(color(0.0f, 0.0f, 0.0f));
            const D2D1_RECT_F layout{4.0f, 8.0f, 124.0f, 60.0f};
            rt->DrawText(text, length, format.get(), layout, ink.get(), D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
            const HRESULT result = rt->EndDraw();
            if (FAILED(result)) {
                scene_error = "EndDraw returned " + Kit::hr(result);
                return false;
            }
            return finish_scene(device, texture.get(), out, scene_error);
        },
        Tolerance::bytes(2));
    Com<IDWriteTextLayout> layout;
    if (SUCCEEDED(dwrite->CreateGdiCompatibleTextLayout(text, length, format.get(), 400.0f, 100.0f, 1.0f, nullptr, FALSE, layout.put()))) {
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        HFONT font = ::CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
        HDC screen = ::GetDC(nullptr);
        HDC memory = ::CreateCompatibleDC(screen);
        HGDIOBJ previous = ::SelectObject(memory, font);
        SIZE extent{};
        ::GetTextExtentPoint32W(memory, text, static_cast<int>(length), &extent);
        ::SelectObject(memory, previous);
        ::DeleteDC(memory);
        ::ReleaseDC(nullptr, screen);
        ::DeleteObject(font);
        context.measure("dwrite_width", metrics.widthIncludingTrailingWhitespace);
        context.measure("gdi_width", static_cast<long long>(extent.cx));
        if (std::fabs(metrics.widthIncludingTrailingWhitespace - static_cast<float>(extent.cx)) > 1.0f) {
            problems.add("the GDI-compatible DirectWrite layout is " + std::to_string(metrics.widthIncludingTrailingWhitespace) + " wide, GDI measures the same Arial 16 px text as " +
                         std::to_string(extent.cx));
        }
    } else {
        problems.add("CreateGdiCompatibleTextLayout failed");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
