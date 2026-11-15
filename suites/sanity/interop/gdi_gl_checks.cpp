#include "sanity/interop/gdi_gl_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/gl_frame_draw.hpp"
#include "support/gl_rig.hpp"

#include <chrono>

namespace lsw::suites::sanity::interop {

namespace {

constexpr int kSize = 128;

std::vector<std::uint32_t> read_back(const GlApi& api, int width, int height) {
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    api.glReadBuffer(glc::BACK);
    api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api.glReadPixels(0, 0, width, height, glc::RGBA, glc::UNSIGNED_BYTE, raw.data());
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = &raw[(static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u];
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = Kit::pack(px[0], px[1], px[2]);
        }
    }
    return out;
}

void unrelated_gdi(HDC window_dc) {
    HDC memory = ::CreateCompatibleDC(nullptr);
    HBITMAP bitmap = ::CreateBitmap(16, 16, 1, 32, nullptr);
    HGDIOBJ old = ::SelectObject(memory, bitmap);
    RECT area{0, 0, 16, 16};
    HBRUSH brush = ::CreateSolidBrush(RGB(10, 200, 30));
    ::FillRect(memory, &area, brush);
    ::DeleteObject(brush);
    ::SetBkMode(memory, TRANSPARENT);
    ::TextOutA(memory, 0, 0, "x", 1);
    ::SelectObject(memory, old);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory);
    TEXTMETRICA metrics{};
    ::GetTextMetricsA(window_dc, &metrics);
    ::GetDeviceCaps(window_dc, BITSPIXEL);
    ::GdiFlush();
}

}  // namespace

Outcome GdiGlChecks::window_owndc(Context& context) {
    Problems problems;
    {
        GlRig rig;
        std::string error;
        GlRigOptions options;
        options.width = kSize;
        options.height = kSize;
        if (!rig.open(options, error)) {
            return Outcome::fail(error);
        }
        rig.record(context);
        rig.window.move(60, 60);
        ::SetWindowPos(rig.window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if ((rig.pfd.dwFlags & PFD_DOUBLEBUFFER) == 0) {
            problems.add("the pixel format chosen for the double-buffered request is single-buffered");
        }
        const GlApi& api = rig.gl;
        const RECT area = ScreenCapture::client_on_screen(rig.window.hwnd());
        for (int frame = 1; frame <= 3; ++frame) {
            GlFrameDraw::frame(api, kSize, kSize, frame);
            const std::vector<std::uint32_t> want = FrameImage::render(frame, kSize, kSize);
            unrelated_gdi(rig.dc());
            const std::vector<std::uint32_t> after_gdi = read_back(api, kSize, kSize);
            std::string where;
            Bitmap held;
            held.width = kSize;
            held.height = kSize;
            held.pixels = after_gdi;
            if (!ScreenCapture::matches(held, want, 0, where)) {
                problems.add("frame " + std::to_string(frame) + ": unrelated GDI calls on the OWNDC device context changed the GL back buffer: " + where);
            }
            if (frame == 2) {
                HBRUSH brush = ::CreateSolidBrush(RGB(255, 0, 255));
                RECT patch{20, 20, 100, 100};
                ::FillRect(rig.dc(), &patch, brush);
                ::DeleteObject(brush);
                ::GdiFlush();
            }
            if (::SwapBuffers(rig.dc()) == FALSE) {
                problems.add("SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")");
                break;
            }
            api.glFinish();
            std::string detail;
            if (!Kit::settle_screen(context, "double_buffered_" + std::to_string(frame), area, want, 0, 3000, detail)) {
                problems.add("frame " + std::to_string(frame) + ": the screen after SwapBuffers does not show the GL frame: " + detail);
            }
        }
        api.glFinish();
    }
    {
        TestWindow window;
        TestWindowConfig config;
        config.x = 60;
        config.y = 60;
        TestWindow::outer_size(config.x, config.y, kSize, kSize, config.width, config.height);
        std::string error;
        if (!window.create(config, error)) {
            problems.add("single-buffered window: " + error);
            return problems.outcome();
        }
        ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        PIXELFORMATDESCRIPTOR want{};
        want.nSize = sizeof(want);
        want.nVersion = 1;
        want.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_SUPPORT_GDI;
        want.iPixelType = PFD_TYPE_RGBA;
        want.cColorBits = 32;
        PIXELFORMATDESCRIPTOR got{};
        const int format = ::ChoosePixelFormat(window.dc(), &want);
        if (format == 0 || ::DescribePixelFormat(window.dc(), format, sizeof(got), &got) == 0 || (got.dwFlags & PFD_SUPPORT_GDI) == 0 ||
            (got.dwFlags & PFD_DOUBLEBUFFER) != 0) {
            context.note("no single-buffered pixel format with PFD_SUPPORT_GDI is offered, so GDI drawing on a single-buffered OpenGL window is not claimed");
            return problems.outcome();
        }
        if (::SetPixelFormat(window.dc(), format, &got) == FALSE) {
            problems.add("SetPixelFormat of the single-buffered GDI format failed (error " + std::to_string(::GetLastError()) + ")");
            return problems.outcome();
        }
        GlContext gl_context;
        GlApi api;
        if (!GlLibrary::instance().ready(error) || !gl_context.create(window.dc(), GlContextRequest{}, nullptr, error) || !gl_context.make_current(window.dc()) ||
            !api.load(GlLibrary::instance(), error)) {
            problems.add("context on the single-buffered format: " + error);
            return problems.outcome();
        }
        GlFrameDraw::frame(api, kSize, kSize, 4);
        api.glFinish();
        const RECT patch{24, 24, 104, 72};
        HBRUSH brush = ::CreateSolidBrush(RGB(255, 0, 255));
        RECT draw = patch;
        ::FillRect(window.dc(), &draw, brush);
        ::DeleteObject(brush);
        ::GdiFlush();
        std::vector<std::uint32_t> want_image = FrameImage::render(4, kSize, kSize);
        Kit::paint(want_image, kSize, patch, 0xFF00FFu);
        std::string detail;
        if (!Kit::settle_screen(context, "single_buffered", ScreenCapture::client_on_screen(window.hwnd()), want_image, 0, 3000, detail)) {
            problems.add("GDI drawing over GL content in a single-buffered window with PFD_SUPPORT_GDI is not visible or disturbed the GL content: " + detail);
        }
        api.glFinish();
        GlContext::release();
    }
    return problems.outcome();
}

Outcome GdiGlChecks::mixed_swap(Context& context) {
    GlRig rig;
    std::string error;
    GlRigOptions options;
    options.width = kSize;
    options.height = kSize;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    rig.record(context);
    rig.window.move(60, 60);
    ::SetWindowPos(rig.window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Problems problems;
    const GlApi& api = rig.gl;
    HFONT font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ previous = ::SelectObject(rig.dc(), font);
    const auto started = std::chrono::steady_clock::now();
    int last = 0;
    for (int i = 0; i < 100; ++i) {
        const std::string text = "GDI " + std::to_string(i);
        ::SetBkMode(rig.dc(), TRANSPARENT);
        ::SetTextColor(rig.dc(), RGB(255, 255, 255));
        ::TextOutA(rig.dc(), 4, 4 + (i % 100), text.c_str(), static_cast<int>(text.size()));
        ::GdiFlush();
        last = i % FrameImage::kFrames;
        GlFrameDraw::frame(api, kSize, kSize, last);
        if (::SwapBuffers(rig.dc()) == FALSE) {
            problems.add("iteration " + std::to_string(i) + ": SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")");
            break;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        if (elapsed > 90) {
            problems.add("iteration " + std::to_string(i) + ": 90 s elapsed, the alternation hangs or crawls");
            break;
        }
    }
    api.glFinish();
    ::SelectObject(rig.dc(), previous);
    std::string detail;
    if (!Kit::settle_screen(context, "last_frame", ScreenCapture::client_on_screen(rig.window.hwnd()), FrameImage::render(last, kSize, kSize), 0, 3000, detail)) {
        problems.add("the final screen does not equal the last GL frame: " + detail);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
