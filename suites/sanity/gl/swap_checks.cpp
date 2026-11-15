#include "sanity/gl/swap_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_scene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace lsw::suites::sanity::gl {

namespace {

using Clock = std::chrono::steady_clock;

double frames_ms(GlRig& rig, int frames) {
    const auto begin = Clock::now();
    for (int i = 0; i < frames; ++i) {
        const float shade = static_cast<float>(i % 16) / 16.0f;
        rig.gl.glClearColor(shade, 0.25f, 1.0f - shade, 1.0f);
        rig.gl.glClear(glc::COLOR_BUFFER_BIT);
        ::SwapBuffers(rig.dc());
    }
    rig.gl.glFinish();
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count() / frames;
}

bool screen_color(HWND window, int x, int y, int rgb[3]) {
    POINT point{x, y};
    if (::ClientToScreen(window, &point) == FALSE) {
        return false;
    }
    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }
    const COLORREF color = ::GetPixel(screen, point.x, point.y);
    ::ReleaseDC(nullptr, screen);
    if (color == CLR_INVALID) {
        return false;
    }
    rgb[0] = GetRValue(color);
    rgb[1] = GetGValue(color);
    rgb[2] = GetBValue(color);
    return true;
}

void bring_up(TestWindow& window) {
    ::SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

struct Shade {
    int r;
    int g;
    int b;
};

Shade frame_shade(int k) {
    return {(k * 11 + 20) & 255, (255 - k * 7) & 255, (40 + k * 5) & 255};
}

void clear_to(const GlApi& api, const Shade& shade) {
    api.glClearColor(static_cast<float>(shade.r) / 255.0f, static_cast<float>(shade.g) / 255.0f, static_cast<float>(shade.b) / 255.0f, 1.0f);
    api.glClear(glc::COLOR_BUFFER_BIT);
}

bool back_pixel(const GlApi& api, int x, int y, int rgb[3]) {
    std::uint8_t pixel[4] = {};
    api.glReadBuffer(glc::BACK);
    api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api.glReadPixels(x, y, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
    rgb[0] = pixel[0];
    rgb[1] = pixel[1];
    rgb[2] = pixel[2];
    return api.drain_errors() == 0;
}

bool near_shade(const int rgb[3], const Shade& shade, int tolerance) {
    return std::abs(rgb[0] - shade.r) <= tolerance && std::abs(rgb[1] - shade.g) <= tolerance && std::abs(rgb[2] - shade.b) <= tolerance;
}

}  // namespace

Outcome SwapChecks::interval_semantics(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 256, 256)) {
        return Outcome::fail(error);
    }
    bring_up(rig.window);
    const std::string list = rig.wgl.extensions(rig.dc());
    const bool control = WglApi::contains(list, "WGL_EXT_swap_control");
    const bool tear = WglApi::contains(list, "WGL_EXT_swap_control_tear");
    Problems problems;
    if (!control) {
        if (rig.wgl.swap_interval != nullptr) {
            problems.add("wglSwapIntervalEXT resolves although WGL_EXT_swap_control is not advertised");
        }
        if (problems.empty()) {
            return Kit::not_advertised("WGL_EXT_swap_control");
        }
        return problems.outcome();
    }
    if (rig.wgl.swap_interval == nullptr || rig.wgl.get_swap_interval == nullptr) {
        return Outcome::fail("WGL_EXT_swap_control is advertised but wglSwapIntervalEXT or wglGetSwapIntervalEXT does not resolve");
    }
    for (const int interval : {0, 1, 2, 1, 0}) {
        if (rig.wgl.swap_interval(interval) == FALSE) {
            problems.add("wglSwapIntervalEXT(" + std::to_string(interval) + ") returned FALSE (error " + std::to_string(::GetLastError()) + ")");
        } else if (rig.wgl.get_swap_interval() != interval) {
            problems.add("wglGetSwapIntervalEXT returns " + std::to_string(rig.wgl.get_swap_interval()) + " after wglSwapIntervalEXT(" + std::to_string(interval) + ")");
        }
    }
    const BOOL negative = rig.wgl.swap_interval(-1);
    if (tear) {
        const int reported = rig.wgl.get_swap_interval();
        if (negative == FALSE) {
            problems.add("wglSwapIntervalEXT(-1) returned FALSE although WGL_EXT_swap_control_tear is advertised");
        } else if (reported != -1 && reported != 1) {
            problems.add("wglGetSwapIntervalEXT returns " + std::to_string(reported) + " after wglSwapIntervalEXT(-1)");
        }
    } else if (negative != FALSE) {
        problems.add("wglSwapIntervalEXT(-1) returned TRUE although WGL_EXT_swap_control_tear is not advertised");
    }
    (void)rig.wgl.swap_interval(0);
    (void)frames_ms(rig, 10);
    const double fast = frames_ms(rig, 90);
    (void)rig.wgl.swap_interval(1);
    (void)frames_ms(rig, 5);
    const double paced = frames_ms(rig, 90);
    (void)rig.wgl.swap_interval(2);
    const double slow = frames_ms(rig, 30);
    context.measure("frame_ms.interval0", fast);
    context.measure("frame_ms.interval1", paced);
    context.measure("frame_ms.interval2", slow);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != FALSE && mode.dmDisplayFrequency > 1) {
        const double period = 1000.0 / static_cast<double>(mode.dmDisplayFrequency);
        context.measure("refresh_period_ms", period);
        if (paced < period * 0.9) {
            problems.add("interval 1 presents at " + std::to_string(paced) + " ms per frame, below 0.9 of the " + std::to_string(period) + " ms refresh period");
        }
        if (slow < period * 1.8) {
            problems.add("interval 2 presents at " + std::to_string(slow) + " ms per frame, below 1.8 of the " + std::to_string(period) + " ms refresh period");
        }
    }
    GlTools::expect_clean(problems, rig.gl, "the swap loops");
    return problems.outcome();
}

Outcome SwapChecks::swapbuffers_content(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    bring_up(rig.window);
    const GlApi& api = rig.gl;
    Problems problems;
    const DWORD flags = rig.pfd.dwFlags;
    const char* method = (flags & PFD_SWAP_COPY) != 0 ? "copy" : (flags & PFD_SWAP_EXCHANGE) != 0 ? "exchange" : "undefined";
    context.measure("swap_method", method);
    const int w = rig.window.width();
    const int h = rig.window.height();
    api.glViewport(0, 0, w, h);
    Shade previous = {-1, -1, -1};
    for (int frame = 0; frame < 16; ++frame) {
        const Shade shade = frame_shade(frame);
        clear_to(api, shade);
        int rgb[3] = {};
        if (!back_pixel(api, w / 2, h / 2, rgb) || !near_shade(rgb, shade, 1)) {
            problems.add("frame " + std::to_string(frame) + ": the back buffer reads (" + std::to_string(rgb[0]) + "," + std::to_string(rgb[1]) + "," + std::to_string(rgb[2]) + ") before SwapBuffers");
            break;
        }
        if (::SwapBuffers(rig.dc()) == FALSE) {
            problems.add("frame " + std::to_string(frame) + ": SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")");
            break;
        }
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        int seen[3] = {};
        bool shown = false;
        do {
            if (screen_color(rig.window.hwnd(), w / 2, h / 2, seen) && near_shade(seen, shade, 2)) {
                shown = true;
                break;
            }
            ::Sleep(15);
        } while (Clock::now() < deadline);
        if (!shown) {
            problems.add("frame " + std::to_string(frame) + ": the screen shows (" + std::to_string(seen[0]) + "," + std::to_string(seen[1]) + "," + std::to_string(seen[2]) +
                         ") instead of the presented (" + std::to_string(shade.r) + "," + std::to_string(shade.g) + "," + std::to_string(shade.b) + ")");
            break;
        }
        int after[3] = {};
        if ((flags & PFD_SWAP_COPY) != 0 && back_pixel(api, w / 2, h / 2, after) && !near_shade(after, shade, 1)) {
            problems.add("frame " + std::to_string(frame) + ": the format reports a copy swap but the back buffer holds (" + std::to_string(after[0]) + "," + std::to_string(after[1]) + "," +
                         std::to_string(after[2]) + ") after the swap");
        }
        if ((flags & PFD_SWAP_EXCHANGE) != 0 && previous.r >= 0 && back_pixel(api, w / 2, h / 2, after) && !near_shade(after, previous, 1)) {
            problems.add("frame " + std::to_string(frame) + ": the format reports an exchange swap but the back buffer holds (" + std::to_string(after[0]) + "," + std::to_string(after[1]) +
                         "," + std::to_string(after[2]) + ") instead of the previous frame");
        }
        previous = shade;
    }
    using namespace wglc;
    if (rig.wgl.get_pixel_format_attribs != nullptr && problems.empty()) {
        const int count = ::DescribePixelFormat(rig.dc(), 1, sizeof(PIXELFORMATDESCRIPTOR), nullptr);
        bool tested[2] = {false, false};
        for (int index = 1; index <= count; ++index) {
            const int names[] = {DRAW_TO_WINDOW, SUPPORT_OPENGL, DOUBLE_BUFFER, PIXEL_TYPE, SWAP_METHOD};
            int values[5] = {};
            PIXELFORMATDESCRIPTOR pfd{};
            if (rig.wgl.get_pixel_format_attribs(rig.dc(), index, 0, 5, names, values) == FALSE || ::DescribePixelFormat(rig.dc(), index, sizeof(pfd), &pfd) == 0) {
                continue;
            }
            const bool copy = values[4] == SWAP_COPY;
            const bool exchange = values[4] == SWAP_EXCHANGE;
            if (values[0] == 0 || values[1] == 0 || values[2] == 0 || values[3] != TYPE_RGBA || (!copy && !exchange) || tested[copy ? 0 : 1]) {
                continue;
            }
            if ((pfd.dwFlags & PFD_GENERIC_FORMAT) != 0 && (pfd.dwFlags & PFD_GENERIC_ACCELERATED) == 0) {
                continue;
            }
            tested[copy ? 0 : 1] = true;
            TestWindow extra;
            if (!extra.create(rig.window_config(64, 64, false), error)) {
                problems.add(error);
                break;
            }
            if (::SetPixelFormat(extra.dc(), index, &pfd) == FALSE) {
                problems.add("SetPixelFormat(" + std::to_string(index) + ") failed for the " + (copy ? "copy" : "exchange") + " format");
                continue;
            }
            GlLibrary& library = GlLibrary::instance();
            HGLRC created = library.create_context(extra.dc());
            if (created == nullptr || library.make_current(extra.dc(), created) == FALSE) {
                problems.add(std::string("no context for the ") + (copy ? "copy" : "exchange") + " swap format " + std::to_string(index));
                continue;
            }
            GlApi extra_api;
            if (extra_api.load(library, error)) {
                Shade before = {-1, -1, -1};
                for (int frame = 0; frame < 4; ++frame) {
                    const Shade shade = frame_shade(frame + 30);
                    clear_to(extra_api, shade);
                    if (::SwapBuffers(extra.dc()) == FALSE) {
                        problems.add("SwapBuffers failed on format " + std::to_string(index));
                        break;
                    }
                    int after[3] = {};
                    if (back_pixel(extra_api, 8, 8, after)) {
                        const Shade& want = copy ? shade : before;
                        if (!copy && before.r < 0) {
                            before = shade;
                            continue;
                        }
                        if (!near_shade(after, want, 1)) {
                            problems.add(std::string("format ") + std::to_string(index) + " reports a " + (copy ? "copy" : "exchange") + " swap method, frame " + std::to_string(frame) +
                                         " leaves (" + std::to_string(after[0]) + "," + std::to_string(after[1]) + "," + std::to_string(after[2]) + ") in the back buffer");
                            break;
                        }
                    }
                    before = shade;
                }
            } else {
                problems.add(error);
            }
            library.make_current(nullptr, nullptr);
            library.delete_context(created);
        }
        context.measure("copy_format_tested", tested[0]);
        context.measure("exchange_format_tested", tested[1]);
    }
    if (!rig.make_current(error)) {
        problems.add(error);
    }
    return problems.outcome();
}

Outcome SwapChecks::resize_and_dc_lifetime(Context& context) {
    GlRig rig;
    std::string error;
    if (!Kit::open(rig, context, error, 128, 128)) {
        return Outcome::fail(error);
    }
    bring_up(rig.window);
    const GlApi& api = rig.gl;
    Problems problems;
    struct Size {
        int w;
        int h;
    };
    const Size sizes[] = {{260, 160}, {240, 140}, {320, 200}, {256, 256}};
    int step = 0;
    for (const Size& size : sizes) {
        RECT outer{0, 0, size.w, size.h};
        ::AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
        rig.window.resize(outer.right - outer.left, outer.bottom - outer.top);
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while ((rig.window.width() != size.w || rig.window.height() != size.h) && Clock::now() < deadline) {
            ::Sleep(10);
        }
        const std::string text = "after resizing to " + std::to_string(size.w) + "x" + std::to_string(size.h);
        if (rig.window.width() != size.w || rig.window.height() != size.h) {
            problems.add(text + " the client area is " + std::to_string(rig.window.width()) + "x" + std::to_string(rig.window.height()));
            continue;
        }
        api.glViewport(0, 0, size.w, size.h);
        int viewport[4] = {};
        api.glGetIntegerv(glc::VIEWPORT, viewport);
        if (viewport[2] != size.w || viewport[3] != size.h) {
            problems.add(text + " GL_VIEWPORT reads " + std::to_string(viewport[2]) + "x" + std::to_string(viewport[3]));
        }
        GlScene scene(size.w, size.h);
        scene.clear_color(GlTools::rgba(10 + step * 30, 200 - step * 20, 90, 255));
        scene.add({2, 2, size.w - 2, size.h / 2, GlTools::rgba(250, 30 + step * 20, 5, 255)});
        scene.add({size.w - 10, size.h - 10, size.w, size.h, GlTools::rgba(1, 2, 3, 255)});
        if (!scene.draw(api, error)) {
            problems.add(text + ": " + error);
            continue;
        }
        std::vector<std::uint8_t> image(static_cast<std::size_t>(size.w) * static_cast<std::size_t>(size.h) * 4, 0xCD);
        api.glReadBuffer(glc::BACK);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glReadPixels(0, 0, size.w, size.h, glc::RGBA, glc::UNSIGNED_BYTE, image.data());
        std::string detail;
        if (!GlTools::same_image(context, "resize_" + std::to_string(step), scene.expected(), image, size.w, size.h, 0, detail)) {
            problems.add(text + ": " + detail);
        }
        scene.release();
        if (::SwapBuffers(rig.dc()) == FALSE) {
            problems.add(text + " SwapBuffers failed (error " + std::to_string(::GetLastError()) + ")");
        }
        ++step;
    }
    const HDC before = rig.dc();
    ::ReleaseDC(rig.window.hwnd(), before);
    HDC again = ::GetDC(rig.window.hwnd());
    context.measure("same_dc_after_release", again == before);
    api.glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    api.glClear(glc::COLOR_BUFFER_BIT);
    std::uint8_t pixel[4] = {};
    api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    api.glReadPixels(4, 4, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
    if (pixel[1] != 255 || pixel[0] != 0) {
        problems.add("after ReleaseDC and GetDC the current context renders (" + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) + ") instead of green");
    }
    if (rig.context.make_current(again) == false) {
        problems.add("wglMakeCurrent with the re-acquired DC failed (error " + std::to_string(::GetLastError()) + ")");
    } else {
        api.glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        api.glClear(glc::COLOR_BUFFER_BIT);
        api.glReadPixels(4, 4, 1, 1, glc::RGBA, glc::UNSIGNED_BYTE, pixel);
        if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 255) {
            problems.add("rendering through the re-acquired DC reads (" + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) + ")");
        }
        if (::SwapBuffers(again) == FALSE) {
            problems.add("SwapBuffers on the re-acquired DC failed (error " + std::to_string(::GetLastError()) + ")");
        }
    }
    GlTools::expect_clean(problems, api, "the resize and DC rounds");
    ::ReleaseDC(rig.window.hwnd(), again);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::gl
