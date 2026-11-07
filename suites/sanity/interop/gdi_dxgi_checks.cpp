#include "sanity/interop/gdi_dxgi_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/swap_rig.hpp"

namespace lsw::suites::sanity::interop {

namespace {

constexpr int kSize = 128;
constexpr std::uint32_t kBackground = 0x204060u;
constexpr std::uint32_t kD3dColor = 0x30C070u;
constexpr std::uint32_t kGdiColor = 0xC81E5Au;
constexpr std::uint32_t kLineColor = 0xFFFFFFu;
constexpr RECT kD3dRect{8, 8, 56, 56};
constexpr RECT kGdiRect{64, 16, 112, 64};
constexpr int kLineY = 70;
constexpr int kLineFrom = 8;
constexpr int kLineTo = 120;
constexpr int kTextX = 12;
constexpr int kTextY = 90;

struct Scene {
    SwapRig rig;
    D3d11FrameDraw draw;
    RECT text{};

    [[nodiscard]] bool open(DXGI_SWAP_EFFECT effect, bool warp, std::string& error) {
        SwapOptions options;
        options.width = kSize;
        options.height = kSize;
        options.buffers = 2;
        options.effect = effect;
        options.flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        options.warp = warp;
        options.visible = !warp;
        return rig.open(options, error) && draw.open(rig.device, error);
    }

    [[nodiscard]] bool d3d_content(std::string& error) {
        Com<ID3D11RenderTargetView> view = rig.back_view(0);
        if (!view) {
            error = "no render target view over the back buffer";
            return false;
        }
        draw.fill(view.get(), {0, 0, kSize, kSize}, kBackground);
        draw.fill(view.get(), kD3dRect, kD3dColor);
        rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
        return D3d11Tools::wait_event(rig.device, rig.device.context, error);
    }

    [[nodiscard]] HRESULT acquire(bool discard, Com<IDXGISurface1>& surface, HDC& dc) {
        surface.reset();
        rig.chain->GetBuffer(0, __uuidof(IDXGISurface1), surface.put_void());
        if (!surface) {
            return E_NOINTERFACE;
        }
        return surface->GetDC(discard ? TRUE : FALSE, &dc);
    }

    void gdi_content(HDC dc) {
        HBRUSH brush = ::CreateSolidBrush(Kit::colorref(kGdiColor));
        RECT area = kGdiRect;
        ::FillRect(dc, &area, brush);
        ::DeleteObject(brush);
        HPEN pen = ::CreatePen(PS_SOLID, 1, Kit::colorref(kLineColor));
        HGDIOBJ old = ::SelectObject(dc, pen);
        ::MoveToEx(dc, kLineFrom, kLineY, nullptr);
        ::LineTo(dc, kLineTo, kLineY);
        ::SelectObject(dc, old);
        ::DeleteObject(pen);
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(255, 255, 0));
        const char label[] = "LSW GDI";
        SIZE extent{};
        ::GetTextExtentPoint32A(dc, label, 7, &extent);
        text = {kTextX, kTextY, kTextX + extent.cx, kTextY + extent.cy};
        ::TextOutA(dc, kTextX, kTextY, label, 7);
    }

    [[nodiscard]] bool run(bool discard, std::string& error) {
        if (!d3d_content(error)) {
            return false;
        }
        Com<IDXGISurface1> surface;
        HDC dc = nullptr;
        const HRESULT got = acquire(discard, surface, dc);
        if (FAILED(got) || dc == nullptr) {
            error = "IDXGISurface1::GetDC returned " + Kit::hr(got);
            return false;
        }
        gdi_content(dc);
        const HRESULT released = surface->ReleaseDC(nullptr);
        if (FAILED(released)) {
            error = "IDXGISurface1::ReleaseDC returned " + Kit::hr(released);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool read(std::vector<std::uint32_t>& out, std::string& error) {
        Com<ID3D11Texture2D> buffer = rig.back_buffer(0);
        TexelImage image;
        if (!buffer || !D3d11Tools::read_texture(rig.device, buffer.get(), 0, image, error)) {
            return false;
        }
        out.resize(static_cast<std::size_t>(kSize) * kSize);
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                out[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)] = image.pixel(static_cast<UINT>(x), static_cast<UINT>(y)) & 0xFFFFFFu;
            }
        }
        return true;
    }

    void close() {
        std::string error;
        if (rig.device.context != nullptr) {
            static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        }
        draw.release();
        rig.chain.reset();
        rig.device.destroy();
        rig.window.destroy();
    }
};

std::vector<std::uint32_t> analytic(bool with_gdi, const RECT& text) {
    std::vector<std::uint32_t> image = Kit::uniform(kSize, kSize, kBackground);
    Kit::paint(image, kSize, kD3dRect, kD3dColor);
    if (with_gdi) {
        Kit::paint(image, kSize, kGdiRect, kGdiColor);
        Kit::paint(image, kSize, {kLineFrom, kLineY, kLineTo, kLineY + 1}, kLineColor);
        (void)text;
    }
    return image;
}

bool equal_outside(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, const RECT& skip, std::string& where) {
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            if (x >= skip.left && x < skip.right && y >= skip.top && y < skip.bottom) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x);
            if (a[at] != b[at]) {
                where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + Kit::rgb(a[at]) + ", expected " + Kit::rgb(b[at]);
                return false;
            }
        }
    }
    return true;
}

const char* effect_name(DXGI_SWAP_EFFECT effect) {
    return effect == DXGI_SWAP_EFFECT_SEQUENTIAL ? "bitblt sequential" : "flip sequential";
}

}  // namespace

Outcome GdiDxgiChecks::getdc_roundtrip(Context& context) {
    Problems problems;
    for (DXGI_SWAP_EFFECT effect : {DXGI_SWAP_EFFECT_SEQUENTIAL, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL}) {
        const std::string label = effect_name(effect);
        Scene av;
        Scene warp;
        std::string error;
        if (!av.open(effect, false, error)) {
            problems.add(label + ": " + error);
            continue;
        }
        if (!warp.open(DXGI_SWAP_EFFECT_SEQUENTIAL, true, error)) {
            problems.add(label + " WARP twin: " + error);
            av.close();
            continue;
        }
        std::vector<std::uint32_t> got;
        std::vector<std::uint32_t> twin;
        if (!av.run(false, error) || !av.read(got, error)) {
            problems.add(label + ": " + error);
        } else if (!warp.run(false, error) || !warp.read(twin, error)) {
            problems.add(label + " WARP twin: " + error);
        } else {
            std::string where;
            if (!equal_outside(got, analytic(true, av.text), av.text, where)) {
                problems.add(label + ": the D3D and GDI content differs from the analytic frame: " + where);
                Kit::keep_pixels(context, label + ".actual", got, kSize, kSize);
            }
            if (!equal_outside(got, twin, {0, 0, 0, 0}, where)) {
                problems.add(label + ": the frame differs from the WARP result: " + where);
                Kit::keep_pixels(context, label + ".warp", twin, kSize, kSize);
            }
            const RECT text = av.text;
            bool text_drawn = false;
            for (int y = text.top; y < text.bottom && y < kSize; ++y) {
                for (int x = text.left; x < text.right && x < kSize; ++x) {
                    text_drawn = text_drawn || got[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)] != kBackground;
                }
            }
            if (!text_drawn) {
                problems.add(label + ": the GDI text left no pixel in its bounding box");
            }
            const HRESULT presented = av.rig.chain->Present(1, 0);
            if (FAILED(presented)) {
                problems.add(label + ": Present returned " + Kit::hr(presented));
            } else {
                static_cast<void>(D3d11Tools::wait_event(av.rig.device, av.rig.device.context, error));
                const RECT area = ScreenCapture::client_on_screen(av.rig.window.hwnd());
                std::string detail;
                if (!Kit::settle_screen(context, label + ".screen", area, got, 0, 3000, detail)) {
                    problems.add(label + ": the presented frame does not show the GDI and D3D content: " + detail);
                }
            }
        }
        av.close();
        warp.close();
    }
    return problems.outcome();
}

Outcome GdiDxgiChecks::getdc_discard_flag(Context& context) {
    Problems problems;
    for (bool discard : {false, true}) {
        const std::string label = discard ? "Discard TRUE" : "Discard FALSE";
        Scene av;
        Scene warp;
        std::string error;
        if (!av.open(DXGI_SWAP_EFFECT_SEQUENTIAL, false, error) || !warp.open(DXGI_SWAP_EFFECT_SEQUENTIAL, true, error)) {
            problems.add(label + ": " + error);
            av.close();
            warp.close();
            continue;
        }
        std::vector<std::uint32_t> got;
        std::vector<std::uint32_t> twin;
        if (!av.run(discard, error) || !av.read(got, error)) {
            problems.add(label + ": " + error);
        } else if (!warp.run(discard, error) || !warp.read(twin, error)) {
            problems.add(label + " WARP twin: " + error);
        } else {
            const std::vector<std::uint32_t> want = analytic(true, av.text);
            std::string where;
            const RECT skip = av.text;
            if (!discard) {
                if (!equal_outside(got, want, skip, where)) {
                    problems.add(label + ": the prior D3D contents were not preserved or the GDI content is wrong: " + where);
                    Kit::keep_pixels(context, label + ".actual", got, kSize, kSize);
                }
                if (!equal_outside(got, twin, {0, 0, 0, 0}, where)) {
                    problems.add(label + ": the frame differs from the WARP result: " + where);
                }
            } else {
                RECT drawn = av.text;
                const RECT parts[] = {kGdiRect, {kLineFrom, kLineY, kLineTo, kLineY + 1}, drawn};
                for (const RECT& part : parts) {
                    for (int y = part.top; y < part.bottom && y < kSize; ++y) {
                        for (int x = part.left; x < part.right && x < kSize; ++x) {
                            const std::size_t at = static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x);
                            if (got[at] != twin[at]) {
                                problems.add(label + ": GDI-drawn pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + Kit::rgb(got[at]) + ", WARP produced " + Kit::rgb(twin[at]));
                                y = kSize;
                                break;
                            }
                        }
                    }
                }
                for (int y = kGdiRect.top; y < kGdiRect.bottom; ++y) {
                    if (got[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(kGdiRect.left + 5)] != kGdiColor) {
                        problems.add(label + ": the filled GDI rectangle is not the GDI colour");
                        break;
                    }
                }
            }
        }
        av.close();
        warp.close();
    }
    return problems.outcome();
}

Outcome GdiDxgiChecks::getdc_lifetime(Context& context) {
    struct Steps {
        HRESULT first = E_FAIL;
        HRESULT second = E_FAIL;
        HRESULT release = E_FAIL;
        HRESULT stray = E_FAIL;
    };
    Problems problems;
    Steps results[2];
    Scene scenes[2];
    std::string error;
    for (int side = 0; side < 2; ++side) {
        Scene& scene = scenes[side];
        if (!scene.open(DXGI_SWAP_EFFECT_SEQUENTIAL, side == 1, error)) {
            problems.add(std::string(side == 0 ? "adapter" : "WARP twin") + ": " + error);
            scene.close();
            return problems.outcome();
        }
        Com<IDXGISurface1> surface;
        HDC first = nullptr;
        HDC second = nullptr;
        results[side].first = scene.acquire(false, surface, first);
        results[side].second = surface ? surface->GetDC(FALSE, &second) : E_NOINTERFACE;
        if (SUCCEEDED(results[side].second)) {
            surface->ReleaseDC(nullptr);
        }
        results[side].release = surface ? surface->ReleaseDC(nullptr) : E_NOINTERFACE;
        results[side].stray = surface ? surface->ReleaseDC(nullptr) : E_NOINTERFACE;
        context.measure(std::string("getdc.") + (side == 0 ? "adapter" : "warp") + ".first", Kit::hr(results[side].first));
        context.measure(std::string("getdc.") + (side == 0 ? "adapter" : "warp") + ".second", Kit::hr(results[side].second));
        context.measure(std::string("getdc.") + (side == 0 ? "adapter" : "warp") + ".release", Kit::hr(results[side].release));
        context.measure(std::string("getdc.") + (side == 0 ? "adapter" : "warp") + ".stray_release", Kit::hr(results[side].stray));
    }
    const Steps& av = results[0];
    const Steps& warp = results[1];
    if (FAILED(av.first)) {
        problems.add("the first GetDC returned " + Kit::hr(av.first));
    }
    if (SUCCEEDED(av.second)) {
        problems.add("a second GetDC without ReleaseDC succeeded");
    } else if (av.second != warp.second) {
        problems.add("a second GetDC without ReleaseDC returned " + Kit::hr(av.second) + ", WARP returned " + Kit::hr(warp.second));
    }
    if (FAILED(av.release)) {
        problems.add("ReleaseDC after the first GetDC returned " + Kit::hr(av.release));
    }
    if (SUCCEEDED(av.stray)) {
        problems.add("ReleaseDC without a matching GetDC succeeded");
    } else if (av.stray != warp.stray) {
        problems.add("ReleaseDC without a matching GetDC returned " + Kit::hr(av.stray) + ", WARP returned " + Kit::hr(warp.stray));
    }
    std::vector<std::uint32_t> got;
    if (!scenes[0].run(false, error) || !scenes[0].read(got, error)) {
        problems.add("the surface is unusable after the misuse: " + error);
    } else {
        std::string where;
        if (!equal_outside(got, analytic(true, scenes[0].text), scenes[0].text, where)) {
            problems.add("the frame drawn after the misuse is wrong: " + where);
        }
    }
    for (Scene& scene : scenes) {
        scene.close();
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
