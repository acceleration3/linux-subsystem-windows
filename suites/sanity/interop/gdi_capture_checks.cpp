#include "sanity/interop/gdi_capture_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/gdi_dib.hpp"
#include "support/window_presenter.hpp"

#include <chrono>

namespace lsw::suites::sanity::interop {

namespace {

constexpr int kSize = 128;

bool poll_window(const std::function<bool(Bitmap&, std::string&)>& capture, const std::vector<std::uint32_t>& want, int origin_x, int origin_y,
                 int timeout_ms, Bitmap& last, std::string& where) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        if (!capture(last, where)) {
            return false;
        }
        if (ScreenCapture::matches_region(last, origin_x, origin_y, kSize, kSize, want, 0, where)) {
            return true;
        }
        ::Sleep(15);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

}  // namespace

Outcome GdiCaptureChecks::bitblt_each_api(Context& context) {
    Problems problems;
    for (const std::string& api : WindowPresenter::apis()) {
        std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make(api);
        std::string error;
        if (!presenter->open(60, 60, kSize, kSize, error)) {
            problems.add(api + ": " + error);
            presenter->close();
            continue;
        }
        for (int frame : {3, 9}) {
            if (!presenter->present(frame, error)) {
                problems.add(api + " frame " + std::to_string(frame) + ": " + error);
                break;
            }
            const std::vector<std::uint32_t> want = FrameImage::render(frame, kSize, kSize);
            std::string detail;
            const std::string label = api + "_frame_" + std::to_string(frame);
            if (!Kit::settle_screen(context, label + ".desktop", ScreenCapture::client_on_screen(presenter->hwnd()), want, 0, 3000, detail)) {
                problems.add(api + " frame " + std::to_string(frame) + ": BitBlt from the desktop device context differs from the presented frame: " + detail);
            }
            Bitmap last;
            HWND window = presenter->hwnd();
            const bool matched = poll_window([&](Bitmap& out, std::string& e) { return ScreenCapture::window_client(window, out, e); }, want, 0, 0, 3000, last, detail);
            if (presenter->window_dc_has_content()) {
                if (!matched) {
                    problems.add(api + " frame " + std::to_string(frame) + ": BitBlt from the window device context differs from the presented frame: " + detail);
                    if (last.width > 0) {
                        Kit::keep_image(context, label + ".window", last);
                    }
                }
            } else {
                context.measure(api + ".window_dc_matches_frame_" + std::to_string(frame), matched);
            }
        }
        presenter->close();
    }
    return problems.outcome();
}

Outcome GdiCaptureChecks::printwindow_full_content(Context& context) {
    Problems problems;
    for (const std::string& api : WindowPresenter::apis()) {
        for (bool flip : {false, true}) {
            if (flip && api != "d3d11") {
                continue;
            }
            std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make(api, flip);
            const std::string name = presenter->name();
            std::string error;
            if (!presenter->open(60, 60, kSize, kSize, error)) {
                problems.add(name + ": " + error);
                presenter->close();
                continue;
            }
            for (int frame : {5, 11}) {
                if (!presenter->present(frame, error)) {
                    problems.add(name + " frame " + std::to_string(frame) + ": " + error);
                    break;
                }
                HWND window = presenter->hwnd();
                RECT outer{};
                ::GetWindowRect(window, &outer);
                const RECT client = ScreenCapture::client_on_screen(window);
                const int dx = static_cast<int>(client.left - outer.left);
                const int dy = static_cast<int>(client.top - outer.top);
                const std::vector<std::uint32_t> want = FrameImage::render(frame, kSize, kSize);
                Bitmap last;
                std::string detail;
                const bool matched = poll_window([&](Bitmap& out, std::string& e) { return ScreenCapture::print_window(window, ScreenCapture::kRenderFullContent, out, e); }, want, dx, dy, 3000,
                                                 last, detail);
                if (!matched) {
                    problems.add(name + " frame " + std::to_string(frame) + ": PrintWindow(PW_RENDERFULLCONTENT) differs from the presented frame: " + detail);
                    if (last.width > 0) {
                        Kit::keep_image(context, Kit::sanitize(name) + "_frame_" + std::to_string(frame) + ".printwindow", last);
                    }
                }
            }
            presenter->close();
        }
    }
    return problems.outcome();
}

Outcome GdiCaptureChecks::cursor_in_captures(Context& context) {
    Problems problems;
    for (int kind = 0; kind < 2; ++kind) {
        const std::string label = kind == 0 ? "GDI window" : "Direct3D 11 window";
        TestWindow gdi_window;
        std::unique_ptr<WindowPresenter> presenter;
        HWND window = nullptr;
        std::string error;
        std::vector<std::uint32_t> want;
        if (kind == 0) {
            TestWindowConfig config;
            config.x = 60;
            config.y = 60;
            TestWindow::outer_size(config.x, config.y, kSize, kSize, config.width, config.height);
            if (!gdi_window.create(config, error)) {
                problems.add(label + ": " + error);
                continue;
            }
            ::SetWindowPos(gdi_window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            window = gdi_window.hwnd();
            want = FrameImage::render(6, kSize, kSize);
            GdiDib dib;
            if (!dib.create(kSize, kSize, error)) {
                problems.add(label + ": " + error);
                continue;
            }
            std::copy(want.begin(), want.end(), dib.bits());
            HDC dc = ::GetDC(window);
            ::BitBlt(dc, 0, 0, kSize, kSize, dib.dc(), 0, 0, SRCCOPY);
            ::ReleaseDC(window, dc);
        } else {
            presenter = WindowPresenter::make("d3d11");
            if (!presenter->open(60, 60, kSize, kSize, error) || !presenter->present(6, error)) {
                problems.add(label + ": " + error);
                presenter->close();
                continue;
            }
            window = presenter->hwnd();
            want = FrameImage::render(6, kSize, kSize);
        }
        const RECT area = ScreenCapture::client_on_screen(window);
        std::string detail;
        if (!Kit::settle_screen(context, label + ".content", area, want, 0, 3000, detail)) {
            problems.add(label + ": the window never showed its content: " + detail);
        } else {
            POINT original{};
            const bool restore = ::GetCursorPos(&original) != FALSE;
            ::SetCursorPos(static_cast<int>(area.left) + 40, static_cast<int>(area.top) + 50);
            ::Sleep(150);
            POINT position{};
            POINT hotspot{};
            HCURSOR handle = nullptr;
            if (!ScreenCapture::cursor_state(position, hotspot, handle)) {
                context.note(label + ": no visible cursor in this session, so cursor capture is not exercised");
            } else {
                Bitmap plain;
                Bitmap with_cursor;
                if (!ScreenCapture::screen(area, false, plain, error) || !ScreenCapture::screen(area, true, with_cursor, error)) {
                    problems.add(label + ": " + error);
                } else {
                    Kit::judge(context, problems, label + ".without_cursor", plain, 0, 0, kSize, kSize, want, 0);
                    GdiDib reference;
                    if (!reference.create(kSize, kSize, error)) {
                        problems.add(label + ": " + error);
                    } else {
                        std::copy(want.begin(), want.end(), reference.bits());
                        ScreenCapture::draw_cursor(reference.dc(), static_cast<int>(area.left), static_cast<int>(area.top));
                        const Bitmap expected = reference.snapshot();
                        std::vector<std::uint32_t> expected_pixels(expected.pixels.size());
                        bool differs = false;
                        for (std::size_t i = 0; i < expected.pixels.size(); ++i) {
                            expected_pixels[i] = expected.pixels[i] & 0xFFFFFFu;
                            differs = differs || expected_pixels[i] != want[i];
                        }
                        if (!differs) {
                            problems.add(label + ": drawing the cursor at " + std::to_string(position.x) + "," + std::to_string(position.y) + " changed no pixel of the reference");
                        }
                        Kit::judge(context, problems, label + ".with_cursor", with_cursor, 0, 0, kSize, kSize, expected_pixels, 0);
                    }
                }
            }
            if (restore) {
                ::SetCursorPos(original.x, original.y);
            }
        }
        if (presenter) {
            presenter->close();
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
