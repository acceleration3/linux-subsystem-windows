#include "sanity/interop/gdi_d3d9_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/swap_rig.hpp"

namespace lsw::suites::sanity::interop {

namespace {

constexpr int kEdge = 64;

void gdi_fill(HDC dc, const RECT& rect, std::uint32_t xrgb) {
    HBRUSH brush = ::CreateSolidBrush(Kit::colorref(xrgb));
    RECT area = rect;
    ::FillRect(dc, &area, brush);
    ::DeleteObject(brush);
}

std::vector<std::uint32_t> masked(const SceneImage& image) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(image.width) * image.height);
    for (UINT y = 0; y < image.height; ++y) {
        for (UINT x = 0; x < image.width; ++x) {
            out[static_cast<std::size_t>(y) * image.width + x] = D3d9Tools::pixel(image, x, y) & 0xFFFFFFu;
        }
    }
    return out;
}

bool compare_images(Context& context, Problems& problems, const std::string& label, const std::vector<std::uint32_t>& got,
                    const std::vector<std::uint32_t>& want, int width, int height) {
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (got[i] != want[i]) {
            problems.add(label + ": pixel (" + std::to_string(i % static_cast<std::size_t>(width)) + "," + std::to_string(i / static_cast<std::size_t>(width)) + ") is " +
                         Kit::rgb(got[i]) + ", expected " + Kit::rgb(want[i]));
            Kit::keep_pixels(context, label + ".actual", got, width, height);
            Kit::keep_pixels(context, label + ".expected", want, width, height);
            return false;
        }
    }
    return true;
}

}  // namespace

Outcome GdiD3d9Checks::surface_getdc(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(false, error) || !rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    const RECT gdi_rect{12, 10, 44, 30};
    const std::uint32_t gdi_color = 0xC81E5Au;
    for (int kind = 0; kind < 2; ++kind) {
        const std::string label = kind == 0 ? "system memory offscreen surface" : "lockable render target";
        Com<IDirect3DSurface9> surface;
        HRESULT made = S_OK;
        if (kind == 0) {
            made = device->CreateOffscreenPlainSurface(kEdge, kEdge, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, surface.put(), nullptr);
        } else {
            surface = D3d9Tools::target(device, kEdge, kEdge, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, true, &made);
        }
        if (!surface) {
            problems.add(label + " cannot be created: " + D3d9Tools::hr(made));
            continue;
        }
        const std::vector<std::uint32_t> before = D3d9Tools::pattern(Kit::seed(41, kind), kEdge, kEdge);
        if (!D3d9Tools::write_surface(surface.get(), nullptr, before, kEdge, error)) {
            problems.add(label + ": " + error);
            continue;
        }
        HDC dc = nullptr;
        const HRESULT got = surface->GetDC(&dc);
        if (FAILED(got) || dc == nullptr) {
            problems.add(label + ": IDirect3DSurface9::GetDC returned " + D3d9Tools::hr(got));
            continue;
        }
        gdi_fill(dc, gdi_rect, gdi_color);
        HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HGDIOBJ old = ::SelectObject(dc, pen);
        ::MoveToEx(dc, 4, 50, nullptr);
        ::LineTo(dc, 60, 50);
        ::SelectObject(dc, old);
        ::DeleteObject(pen);
        const HRESULT released = surface->ReleaseDC(dc);
        if (FAILED(released)) {
            problems.add(label + ": ReleaseDC returned " + D3d9Tools::hr(released));
            continue;
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, surface.get(), image, error)) {
            problems.add(label + ": " + error);
            continue;
        }
        std::vector<std::uint32_t> want(before.size());
        for (std::size_t i = 0; i < before.size(); ++i) {
            want[i] = before[i] & 0xFFFFFFu;
        }
        Kit::paint(want, kEdge, gdi_rect, gdi_color);
        Kit::paint(want, kEdge, {4, 50, 60, 51}, 0xFFFFFFu);
        compare_images(context, problems, label, masked(image), want, kEdge, kEdge);
        HDC again = nullptr;
        if (FAILED(surface->GetDC(&again)) || FAILED(surface->ReleaseDC(again))) {
            problems.add(label + ": a second GetDC and ReleaseDC pair fails");
        }
    }
    if (!D3d9Tools::wait_event(device, error)) {
        problems.add(error);
    }
    return problems.outcome();
}

Outcome GdiD3d9Checks::flip_getdc(Context& context) {
    D3d9Rig rig;
    std::string error;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    D3d9Settings settings;
    settings.width = 128;
    settings.height = 128;
    settings.back = D3DFMT_X8R8G8B8;
    settings.buffers = 2;
    settings.effect = D3DSWAPEFFECT_FLIPEX;
    settings.depth = false;
    if (!rig.make_window(settings, error)) {
        return Outcome::fail(error);
    }
    const HRESULT created = rig.try_create(settings);
    if (FAILED(created)) {
        return Outcome::fail("a windowed D3D9Ex FLIPEX device with 2 back buffers cannot be created: " + D3d9Tools::hr(created));
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Com<IDirect3DSurface9> canvas;
    const HRESULT canvas_made = device->CreateOffscreenPlainSurface(128, 128, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, canvas.put(), nullptr);
    if (FAILED(canvas_made)) {
        return Outcome::fail("an X8R8G8B8 offscreen plain surface for GDI cannot be created: " + D3d9Tools::hr(canvas_made));
    }
    const RECT d3d_before{8, 8, 72, 72};
    const RECT gdi_rect{40, 40, 104, 104};
    const RECT d3d_after{80, 20, 120, 60};
    for (int frame = 0; frame < 12 && problems.empty(); ++frame) {
        const std::uint32_t background = FrameImage::color(frame, 0) & 0xFFFFFFu;
        const std::uint32_t before_color = FrameImage::color(frame, 1) & 0xFFFFFFu;
        const std::uint32_t gdi_color = FrameImage::color(frame, 2) & 0xFFFFFFu;
        const std::uint32_t after_color = FrameImage::color(frame, 3) & 0xFFFFFFu;
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, 0xFF000000u | background);
        if (!D3d9Tools::draw_quad(device, 8, 8, 72, 72, 0xFF000000u | before_color)) {
            problems.add("frame " + std::to_string(frame) + ": the D3D quad before GDI cannot be drawn");
            break;
        }
        HDC dc = nullptr;
        HRESULT result = canvas->GetDC(&dc);
        if (FAILED(result) || dc == nullptr) {
            problems.add("frame " + std::to_string(frame) + ": GetDC on the offscreen plain surface returned " + D3d9Tools::hr(result));
            break;
        }
        gdi_fill(dc, gdi_rect, gdi_color);
        result = canvas->ReleaseDC(dc);
        if (FAILED(result)) {
            problems.add("frame " + std::to_string(frame) + ": ReleaseDC returned " + D3d9Tools::hr(result));
            break;
        }
        Com<IDirect3DSurface9> back;
        result = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, back.put());
        result = SUCCEEDED(result) ? device->StretchRect(canvas.get(), &gdi_rect, back.get(), &gdi_rect, D3DTEXF_NONE) : result;
        back.reset();
        if (FAILED(result)) {
            problems.add("frame " + std::to_string(frame) + ": StretchRect of the GDI surface into the FLIPEX back buffer returned " + D3d9Tools::hr(result));
            break;
        }
        if (!D3d9Tools::draw_quad(device, 80, 20, 120, 60, 0xFF000000u | after_color)) {
            problems.add("frame " + std::to_string(frame) + ": the D3D quad after GDI cannot be drawn");
            break;
        }
        result = device->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(result)) {
            problems.add("frame " + std::to_string(frame) + ": Present returned " + D3d9Tools::hr(result));
            break;
        }
        if (!D3d9Tools::wait_event(device, error)) {
            problems.add(error);
            break;
        }
        std::vector<std::uint32_t> want = Kit::uniform(128, 128, background);
        Kit::paint(want, 128, d3d_before, before_color);
        Kit::paint(want, 128, gdi_rect, gdi_color);
        Kit::paint(want, 128, d3d_after, after_color);
        std::string detail;
        if (!Kit::settle_screen(context, "flipex_frame_" + std::to_string(frame), ScreenCapture::client_on_screen(rig.window.hwnd()), want, 0, 3000, detail)) {
            problems.add("frame " + std::to_string(frame) + " does not show the D3D, GDI and later D3D content in order: " + detail);
        }
    }
    return problems.outcome();
}

Outcome GdiD3d9Checks::after_device_lost(Context& context) {
    Problems problems;
    constexpr std::uint32_t kPaint = 0x2A9D8Fu;
    constexpr std::uint32_t kGdi = 0xE76F51u;
    auto client_of = [](HWND window) { return ScreenCapture::client_on_screen(window); };
    auto uniform_client = [&](HWND window, std::uint32_t color) {
        const RECT area = client_of(window);
        return Kit::uniform(static_cast<int>(area.right - area.left), static_cast<int>(area.bottom - area.top), color);
    };
    {
        D3d9Rig rig;
        std::string error;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig.open(true, error) || !rig.create(settings, error)) {
            return Outcome::fail(error);
        }
        IDirect3DDevice9Ex* device = rig.device_ex.get();
        D3d9Tools::clear(device, 0xFF336699u);
        static_cast<void>(device->Present(nullptr, nullptr, nullptr, nullptr));
        static_cast<void>(D3d9Tools::wait_event(device, error));
        rig.window.paint_with(Kit::colorref(kPaint));
        settings.width = 96;
        settings.height = 80;
        D3DPRESENT_PARAMETERS pp = rig.parameters(settings);
        const HRESULT reset = device->ResetEx(&pp, nullptr);
        if (FAILED(reset)) {
            problems.add("ResetEx of the windowed D3D9Ex device returned " + D3d9Tools::hr(reset));
        }
        HDC dc = ::GetDC(rig.window.hwnd());
        RECT area = client_of(rig.window.hwnd());
        const int width = static_cast<int>(area.right - area.left);
        const int height = static_cast<int>(area.bottom - area.top);
        gdi_fill(dc, {0, 0, width, height}, kGdi);
        ::ReleaseDC(rig.window.hwnd(), dc);
        std::string detail;
        if (!Kit::settle_screen(context, "after_reset_gdi", area, Kit::uniform(width, height, kGdi), 0, 3000, detail)) {
            problems.add("GDI on the window after a device reset does not show: " + detail);
        }
        ::RedrawWindow(rig.window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        if (!Kit::settle_screen(context, "after_reset_paint", area, Kit::uniform(width, height, kPaint), 0, 3000, detail)) {
            problems.add("the window is not repainted fully on WM_PAINT after a device reset: " + detail);
        }
        static_cast<void>(D3d9Tools::wait_event(device, error));
        rig.close_device();
        dc = ::GetDC(rig.window.hwnd());
        gdi_fill(dc, {0, 0, width, height}, kGdi);
        ::ReleaseDC(rig.window.hwnd(), dc);
        area = client_of(rig.window.hwnd());
        if (!Kit::settle_screen(context, "after_destroy_gdi", area, Kit::uniform(width, height, kGdi), 0, 3000, detail)) {
            problems.add("GDI on the window after the D3D9 device was destroyed does not show: " + detail);
        }
        ::RedrawWindow(rig.window.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        if (!Kit::settle_screen(context, "after_destroy_paint", area, uniform_client(rig.window.hwnd(), kPaint), 0, 3000, detail)) {
            problems.add("the window is not repainted fully on WM_PAINT after the D3D9 device was destroyed: " + detail);
        }
    }
    {
        SwapRig rig;
        std::string error;
        SwapOptions options;
        options.width = 128;
        options.height = 96;
        options.effect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        if (!rig.open(options, error)) {
            problems.add(error);
        } else {
            static_cast<void>(rig.fill(0.2f, 0.4f, 0.6f, 1.0f, error));
            static_cast<void>(rig.chain->Present(1, 0));
            static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
            rig.window.paint_with(Kit::colorref(kPaint));
            rig.chain.reset();
            rig.device.destroy();
            HWND window = rig.window.hwnd();
            HDC dc = ::GetDC(window);
            gdi_fill(dc, {0, 0, 128, 96}, kGdi);
            ::ReleaseDC(window, dc);
            const RECT area = client_of(window);
            std::string detail;
            if (!Kit::settle_screen(context, "d3d11_destroyed_gdi", area, Kit::uniform(128, 96, kGdi), 0, 3000, detail)) {
                problems.add("GDI on the window after the DXGI swap chain and D3D11 device were destroyed does not show: " + detail);
            }
            ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            if (!Kit::settle_screen(context, "d3d11_destroyed_paint", area, Kit::uniform(128, 96, kPaint), 0, 3000, detail)) {
                problems.add("the window is not repainted fully on WM_PAINT after the D3D11 swap chain was destroyed: " + detail);
            }
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
