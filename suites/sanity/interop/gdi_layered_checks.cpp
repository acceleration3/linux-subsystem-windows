#include "sanity/interop/gdi_layered_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/gdi_dib.hpp"
#include "support/layered_overlay.hpp"
#include "support/window_presenter.hpp"

#include <algorithm>
#include <chrono>
#include <functional>

namespace lsw::suites::sanity::interop {

namespace {

std::uint32_t premultiply(std::uint32_t xrgb, int alpha) {
    std::uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int channel = static_cast<int>((xrgb >> shift) & 255u);
        out |= static_cast<std::uint32_t>((channel * alpha + 127) / 255) << shift;
    }
    return out;
}

bool near_pixels(std::uint32_t a, std::uint32_t b, int tolerance) {
    for (int shift = 0; shift < 24; shift += 8) {
        const int delta = static_cast<int>((a >> shift) & 255u) - static_cast<int>((b >> shift) & 255u);
        if (delta > tolerance || delta < -tolerance) {
            return false;
        }
    }
    return true;
}

RECT inflate(RECT rect, int by) {
    return {rect.left - by, rect.top - by, rect.right + by, rect.bottom + by};
}

bool inside(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

}  // namespace

Outcome GdiLayeredChecks::per_pixel_alpha(Context& context) {
    constexpr int kEdge = 160;
    std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make("d3d11");
    std::string error;
    if (!presenter->open(60, 60, kEdge, kEdge, error) || !presenter->present(2, error)) {
        presenter->close();
        return Outcome::fail("the Direct3D 11 window below: " + error);
    }
    const RECT client = ScreenCapture::client_on_screen(presenter->hwnd());
    LayeredOverlay overlay;
    Problems problems;
    if (!overlay.open(static_cast<int>(client.left), static_cast<int>(client.top), kEdge, kEdge, error)) {
        presenter->close();
        return Outcome::fail("the layered window: " + error);
    }
    const std::vector<std::uint32_t> below = FrameImage::render(2, kEdge, kEdge);
    struct Ramp {
        const char* name;
        std::uint32_t color;
        bool horizontal;
        bool rising;
    };
    for (const Ramp& ramp : {Ramp{"horizontal rising", 0xFA3C14u, true, true}, Ramp{"vertical falling", 0x1450FAu, false, false}}) {
        std::vector<std::uint32_t> argb(static_cast<std::size_t>(kEdge) * kEdge);
        std::vector<std::uint32_t> premultiplied(argb.size());
        std::vector<std::uint8_t> alpha(argb.size());
        for (int y = 0; y < kEdge; ++y) {
            for (int x = 0; x < kEdge; ++x) {
                const int along = ramp.horizontal ? x : y;
                int a = along * 255 / (kEdge - 1);
                if (!ramp.rising) {
                    a = 255 - a;
                }
                const std::size_t at = static_cast<std::size_t>(y) * kEdge + static_cast<std::size_t>(x);
                premultiplied[at] = premultiply(ramp.color, a);
                alpha[at] = static_cast<std::uint8_t>(a);
                argb[at] = (static_cast<std::uint32_t>(a) << 24) | premultiplied[at];
            }
        }
        if (!overlay.update(argb, error)) {
            problems.add(std::string(ramp.name) + ": " + error);
            break;
        }
        std::string detail;
        if (!Kit::settle_screen(context, std::string("layered_") + (ramp.horizontal ? "h" : "v"), client, Kit::blend(below, premultiplied, alpha), 2, 3000, detail)) {
            problems.add(std::string(ramp.name) + " per-pixel alpha overlay does not compose to the source-over result: " + detail);
        }
    }
    overlay.close();
    presenter->close();
    return problems.outcome();
}

Outcome GdiLayeredChecks::over_3d_update(Context& context) {
    constexpr int kEdge = 128;
    constexpr int kIterations = 40;
    constexpr int kAlpha = 96;
    std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make("d3d11");
    std::string error;
    if (!presenter->open(60, 60, kEdge, kEdge, error) || !presenter->present(0, error)) {
        presenter->close();
        return Outcome::fail("the Direct3D 11 window below: " + error);
    }
    const RECT client = ScreenCapture::client_on_screen(presenter->hwnd());
    LayeredOverlay overlay;
    if (!overlay.open(static_cast<int>(client.left), static_cast<int>(client.top), kEdge, kEdge, error)) {
        presenter->close();
        return Outcome::fail("the layered window: " + error);
    }
    auto overlay_color = [](int j) { return FrameImage::color((j * 5 + 21) % FrameImage::kFrames, 3) & 0xFFFFFFu; };
    auto overlay_pixels = [&](int j) {
        std::vector<std::uint32_t> argb(static_cast<std::size_t>(kEdge) * kEdge, (static_cast<std::uint32_t>(kAlpha) << 24) | premultiply(overlay_color(j), kAlpha));
        return argb;
    };
    auto composed = [&](int frame, int j) {
        std::vector<std::uint32_t> premultiplied(static_cast<std::size_t>(kEdge) * kEdge, premultiply(overlay_color(j), kAlpha));
        std::vector<std::uint8_t> alpha(premultiplied.size(), static_cast<std::uint8_t>(kAlpha));
        return Kit::blend(FrameImage::render(frame, kEdge, kEdge), premultiplied, alpha);
    };
    Problems problems;
    if (!overlay.update(overlay_pixels(0), error)) {
        presenter->close();
        return Outcome::fail(error);
    }
    int seen_frame = 0;
    int seen_overlay = 0;
    int captures = 0;
    int pairs_latest = 0;
    for (int i = 1; i <= kIterations && problems.empty(); ++i) {
        if (!presenter->present(i, error) || !overlay.update(overlay_pixels(i), error)) {
            problems.add("iteration " + std::to_string(i) + ": " + error);
            break;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (true) {
            Bitmap shot;
            if (!ScreenCapture::screen(client, false, shot, error)) {
                problems.add(error);
                break;
            }
            ++captures;
            bool found = false;
            int found_frame = 0;
            int found_overlay = 0;
            for (int f = i; f >= seen_frame && !found; --f) {
                for (int j = i; j >= seen_overlay && !found; --j) {
                    const std::vector<std::uint32_t> want = composed(f, j);
                    std::string where;
                    if (ScreenCapture::matches(shot, want, 3, where)) {
                        found = true;
                        found_frame = f;
                        found_overlay = j;
                    }
                }
            }
            if (!found) {
                problems.add("iteration " + std::to_string(i) + ": a capture is not any consistent (frame, overlay) pair with frame >= " + std::to_string(seen_frame) +
                             " and overlay >= " + std::to_string(seen_overlay) + ", so it is torn, mis-blended or stale");
                Kit::keep_image(context, "torn_capture_" + std::to_string(i), shot);
                break;
            }
            seen_frame = found_frame;
            seen_overlay = found_overlay;
            if (found_frame == i && found_overlay == i) {
                ++pairs_latest;
                break;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                problems.add("iteration " + std::to_string(i) + ": the composed result stays at frame " + std::to_string(found_frame) + " with overlay " + std::to_string(found_overlay));
                break;
            }
            ::Sleep(8);
        }
    }
    context.measure("captures", captures);
    context.measure("iterations_reaching_latest_pair", pairs_latest);
    overlay.close();
    presenter->close();
    return problems.outcome();
}

Outcome GdiLayeredChecks::text_over_flip_window(Context& context) {
    constexpr int kEdge = 192;
    std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make("d3d11", true);
    std::string error;
    if (!presenter->open(60, 60, kEdge, kEdge, error) || !presenter->present(7, error)) {
        presenter->close();
        return Outcome::fail("the flip-model Direct3D 11 window: " + error);
    }
    HWND window = presenter->hwnd();
    const RECT client = ScreenCapture::client_on_screen(window);
    const std::vector<std::uint32_t> frame = FrameImage::render(7, kEdge, kEdge);
    Problems problems;
    std::string detail;
    if (!Kit::settle_screen(context, "flip_frame", client, frame, 0, 3000, detail)) {
        problems.add("the flip-model frame never appeared: " + detail);
    }
    HDC window_dc = ::GetDC(window);
    ::SetBkMode(window_dc, TRANSPARENT);
    ::SetTextColor(window_dc, RGB(255, 255, 255));
    const char label[] = "GDI over flip";
    SIZE extent{};
    ::GetTextExtentPoint32A(window_dc, label, 13, &extent);
    const RECT text{8, 8, 8 + extent.cx, 8 + extent.cy};
    ::TextOutA(window_dc, 8, 8, label, 13);
    ::GdiFlush();
    ::ReleaseDC(window, window_dc);
    ::Sleep(100);
    Bitmap shot;
    if (!ScreenCapture::screen(client, false, shot, error)) {
        problems.add(error);
    } else {
        bool shown = false;
        for (int y = 0; y < kEdge; ++y) {
            for (int x = 0; x < kEdge; ++x) {
                const std::uint32_t want = frame[static_cast<std::size_t>(y) * kEdge + static_cast<std::size_t>(x)];
                if ((shot.at(x, y) & 0xFFFFFFu) == (want & 0xFFFFFFu)) {
                    continue;
                }
                if (inside(text, x, y)) {
                    shown = true;
                } else {
                    problems.add("GDI text on the window device context of a flip-model window changed pixel (" + std::to_string(x) + "," + std::to_string(y) + ") outside the text: " +
                                 Kit::rgb(shot.at(x, y)) + " instead of " + Kit::rgb(want));
                    y = kEdge;
                    break;
                }
            }
        }
        context.measure("window_dc_text_visible_on_flip_window", shown);
    }
    if (!presenter->present(8, error)) {
        problems.add(error);
    } else if (!Kit::settle_screen(context, "flip_next_frame", client, FrameImage::render(8, kEdge, kEdge), 0, 3000, detail)) {
        problems.add("the next presented frame is not exact after GDI drew on the window: " + detail);
    }
    GdiDib text_dib;
    const int overlay_width = static_cast<int>(text.right - text.left) + 16;
    const int overlay_height = static_cast<int>(text.bottom - text.top) + 8;
    LayeredOverlay overlay;
    if (!text_dib.create(overlay_width, overlay_height, error) || !overlay.open(static_cast<int>(client.left) + 4, static_cast<int>(client.top) + 40, overlay_width, overlay_height, error)) {
        problems.add(error);
    } else {
        text_dib.clear(0u);
        ::SetBkMode(text_dib.dc(), TRANSPARENT);
        ::SetTextColor(text_dib.dc(), RGB(255, 255, 255));
        ::TextOutA(text_dib.dc(), 8, 4, label, 13);
        ::GdiFlush();
        std::vector<std::uint32_t> argb(Kit::at(0, overlay_height, overlay_width));
        std::vector<std::uint32_t> premultiplied(argb.size());
        std::vector<std::uint8_t> alpha(argb.size());
        bool any_text = false;
        for (std::size_t i = 0; i < argb.size(); ++i) {
            const std::uint32_t px = text_dib.bits()[i] & 0xFFFFFFu;
            const std::uint32_t a = std::max({(px >> 16) & 255u, (px >> 8) & 255u, px & 255u});
            alpha[i] = static_cast<std::uint8_t>(a);
            premultiplied[i] = px;
            argb[i] = (a << 24) | px;
            any_text = any_text || a != 0;
        }
        if (!any_text) {
            problems.add("GDI drew no text into the overlay bitmap");
        }
        if (!overlay.update(argb, error)) {
            problems.add(error);
        } else {
            std::vector<std::uint32_t> want = FrameImage::render(8, kEdge, kEdge);
            std::vector<std::uint32_t> below(argb.size());
            for (int y = 0; y < overlay_height; ++y) {
                for (int x = 0; x < overlay_width; ++x) {
                    below[Kit::at(x, y, overlay_width)] = want[Kit::at(x + 4, y + 40, kEdge)];
                }
            }
            const std::vector<std::uint32_t> mixed = Kit::blend(below, premultiplied, alpha);
            for (int y = 0; y < overlay_height; ++y) {
                for (int x = 0; x < overlay_width; ++x) {
                    want[Kit::at(x + 4, y + 40, kEdge)] = mixed[Kit::at(x, y, overlay_width)];
                }
            }
            if (!Kit::settle_screen(context, "flip_with_layered_text", client, want, 2, 3000, detail)) {
                problems.add("a layered window with GDI text over the flip-model window does not compose to the frame plus the text: " + detail);
            }
        }
        overlay.close();
    }
    presenter->close();
    return problems.outcome();
}

Outcome GdiLayeredChecks::mixed_windows(Context& context) {
    constexpr int kClient = 200;
    constexpr std::uint32_t kGdiColor = 0x4682B4u;
    constexpr std::uint32_t kLayerColor = 0xFFD23Fu;
    constexpr int kLayerAlpha = 110;
    std::string error;
    TestWindow gdi;
    TestWindowConfig config;
    config.x = 40;
    config.y = 40;
    TestWindow::outer_size(config.x, config.y, kClient, kClient, config.width, config.height);
    if (!gdi.create(config, error)) {
        return Outcome::fail(error);
    }
    gdi.paint_with(Kit::colorref(kGdiColor));
    ::SetWindowPos(gdi.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::RedrawWindow(gdi.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    std::unique_ptr<WindowPresenter> three_d = WindowPresenter::make("d3d11");
    if (!three_d->open(200, 120, kClient, kClient, error) || !three_d->present(9, error)) {
        three_d->close();
        return Outcome::fail("the Direct3D 11 window: " + error);
    }
    TestWindow layer;
    TestWindowConfig layer_config;
    layer_config.x = 300;
    layer_config.y = 200;
    layer_config.width = 160;
    layer_config.height = 140;
    layer_config.popup = true;
    layer_config.layered = true;
    layer_config.topmost = true;
    if (!layer.create(layer_config, error)) {
        three_d->close();
        return Outcome::fail(error);
    }
    layer.paint_with(Kit::colorref(kLayerColor));
    ::SetLayeredWindowAttributes(layer.hwnd(), 0, static_cast<BYTE>(kLayerAlpha), LWA_ALPHA);
    ::RedrawWindow(layer.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    Problems problems;
    struct Surface {
        HWND window;
        int kind;
    };
    auto verify = [&](const std::string& stage) {
        const Surface gdi_surface{gdi.hwnd(), 0};
        const Surface d3d_surface{three_d->hwnd(), 1};
        RECT outer_gdi{};
        RECT outer_3d{};
        RECT outer_layer{};
        ::GetWindowRect(gdi_surface.window, &outer_gdi);
        ::GetWindowRect(d3d_surface.window, &outer_3d);
        ::GetWindowRect(layer.hwnd(), &outer_layer);
        const RECT client_gdi = ScreenCapture::client_on_screen(gdi_surface.window);
        const RECT client_3d = ScreenCapture::client_on_screen(d3d_surface.window);
        RECT bounds{std::min({outer_gdi.left, outer_3d.left, outer_layer.left}) - 4, std::min({outer_gdi.top, outer_3d.top, outer_layer.top}) - 4,
                    std::max({outer_gdi.right, outer_3d.right, outer_layer.right}) + 4, std::max({outer_gdi.bottom, outer_3d.bottom, outer_layer.bottom}) + 4};
        const RECT zone_3d = inflate(outer_3d, 24);
        const RECT inner_3d = inflate(client_3d, -8);
        const auto expectation = [&](int sx, int sy, std::uint32_t& out) {
            const bool in_layer = inside(outer_layer, sx, sy);
            const bool near_layer = inside(inflate(outer_layer, 2), sx, sy) && !in_layer;
            if (near_layer || (inside(zone_3d, sx, sy) && !inside(inner_3d, sx, sy))) {
                return false;
            }
            std::uint32_t base = 0;
            if (inside(client_3d, sx, sy)) {
                base = FrameImage::at(9, kClient, kClient, sx - static_cast<int>(client_3d.left), sy - static_cast<int>(client_3d.top));
            } else if (inside(client_gdi, sx, sy)) {
                base = kGdiColor;
            } else {
                return false;
            }
            base &= 0xFFFFFFu;
            if (in_layer) {
                std::uint32_t mixed = 0;
                for (int shift = 16; shift >= 0; shift -= 8) {
                    const int value = (static_cast<int>((kLayerColor >> shift) & 255u) * kLayerAlpha + static_cast<int>((base >> shift) & 255u) * (255 - kLayerAlpha) + 127) / 255;
                    mixed |= static_cast<std::uint32_t>(std::min(value, 255)) << shift;
                }
                base = mixed;
            }
            out = base;
            return true;
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        std::string first;
        Bitmap shot;
        std::size_t checked = 0;
        do {
            if (!ScreenCapture::screen(bounds, false, shot, error)) {
                problems.add(stage + ": " + error);
                return;
            }
            first.clear();
            checked = 0;
            for (int y = static_cast<int>(bounds.top); y < bounds.bottom && first.empty(); y += 3) {
                for (int x = static_cast<int>(bounds.left); x < bounds.right; x += 3) {
                    std::uint32_t want = 0;
                    if (!expectation(x, y, want)) {
                        continue;
                    }
                    ++checked;
                    const std::uint32_t have = shot.at(x - static_cast<int>(bounds.left), y - static_cast<int>(bounds.top));
                    if (!near_pixels(have, want, 3)) {
                        first = "screen (" + std::to_string(x) + "," + std::to_string(y) + ") is " + Kit::rgb(have) + ", the z-ordered composite is " + Kit::rgb(want);
                        break;
                    }
                }
            }
            if (first.empty()) {
                break;
            }
            ::Sleep(20);
        } while (std::chrono::steady_clock::now() < deadline);
        if (!first.empty()) {
            problems.add(stage + ": " + first);
            Kit::keep_image(context, Kit::sanitize(stage) + ".screen", shot);
        } else if (checked < 200) {
            problems.add(stage + ": only " + std::to_string(checked) + " pixels could be judged, the windows do not overlap as arranged");
        }
        context.measure(Kit::sanitize(stage) + ".pixels_judged", static_cast<long long>(checked));
    };
    verify("initial arrangement");
    ::SetWindowPos(three_d->hwnd(), nullptr, 240, 150, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::Sleep(100);
    verify("after moving the Direct3D 11 window");
    ::SetWindowPos(layer.hwnd(), nullptr, 120, 170, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::Sleep(100);
    verify("after moving the layered window");
    layer.destroy();
    three_d->close();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
