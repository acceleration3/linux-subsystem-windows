#include "support/screen_capture.hpp"

#include "core/text.hpp"
#include "support/gdi_dib.hpp"

#include <cstdlib>

namespace lsw::suites {

namespace {

bool within(std::uint32_t a, std::uint32_t b, int tolerance) {
    for (int shift = 0; shift < 24; shift += 8) {
        const int delta = static_cast<int>((a >> shift) & 0xFFu) - static_cast<int>((b >> shift) & 0xFFu);
        if (std::abs(delta) > tolerance) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string ScreenCapture::rgb_text(std::uint32_t value) {
    return "(" + std::to_string((value >> 16) & 0xFFu) + "," + std::to_string((value >> 8) & 0xFFu) + "," + std::to_string(value & 0xFFu) + ")";
}

RECT ScreenCapture::client_on_screen(HWND window) {
    RECT client{};
    ::GetClientRect(window, &client);
    POINT origin{0, 0};
    ::ClientToScreen(window, &origin);
    return {origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
}

bool ScreenCapture::cursor_state(POINT& position, POINT& hotspot, HCURSOR& handle) {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE || (info.flags & CURSOR_SHOWING) == 0 || info.hCursor == nullptr) {
        return false;
    }
    ICONINFO icon{};
    if (::GetIconInfo(info.hCursor, &icon) == FALSE) {
        return false;
    }
    hotspot = {static_cast<LONG>(icon.xHotspot), static_cast<LONG>(icon.yHotspot)};
    if (icon.hbmMask != nullptr) {
        ::DeleteObject(icon.hbmMask);
    }
    if (icon.hbmColor != nullptr) {
        ::DeleteObject(icon.hbmColor);
    }
    position = info.ptScreenPos;
    handle = info.hCursor;
    return true;
}

void ScreenCapture::draw_cursor(HDC dc, int origin_x, int origin_y) {
    POINT position{};
    POINT hotspot{};
    HCURSOR handle = nullptr;
    if (cursor_state(position, hotspot, handle)) {
        ::DrawIconEx(dc, position.x - hotspot.x - origin_x, position.y - hotspot.y - origin_y, handle, 0, 0, 0, nullptr, DI_NORMAL);
    }
}

bool ScreenCapture::screen(const RECT& area, bool cursor, Bitmap& out, std::string& error) {
    const int width = static_cast<int>(area.right - area.left);
    const int height = static_cast<int>(area.bottom - area.top);
    GdiDib dib;
    if (!dib.create(width, height, error)) {
        return false;
    }
    HDC desktop = ::GetDC(nullptr);
    if (desktop == nullptr) {
        error = "the desktop device context is unavailable";
        return false;
    }
    const BOOL copied = ::BitBlt(dib.dc(), 0, 0, width, height, desktop, area.left, area.top, SRCCOPY | CAPTUREBLT);
    ::ReleaseDC(nullptr, desktop);
    if (copied == FALSE) {
        error = "BitBlt from the desktop failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    if (cursor) {
        draw_cursor(dib.dc(), area.left, area.top);
    }
    out = dib.snapshot();
    return true;
}

bool ScreenCapture::window_client(HWND window, Bitmap& out, std::string& error) {
    RECT client{};
    ::GetClientRect(window, &client);
    GdiDib dib;
    if (!dib.create(client.right, client.bottom, error)) {
        return false;
    }
    HDC source = ::GetDC(window);
    if (source == nullptr) {
        error = "GetDC of the window failed";
        return false;
    }
    const BOOL copied = ::BitBlt(dib.dc(), 0, 0, client.right, client.bottom, source, 0, 0, SRCCOPY | CAPTUREBLT);
    ::ReleaseDC(window, source);
    if (copied == FALSE) {
        error = "BitBlt from the window device context failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    out = dib.snapshot();
    return true;
}

bool ScreenCapture::print_window(HWND window, UINT flags, Bitmap& out, std::string& error) {
    RECT rect{};
    ::GetWindowRect(window, &rect);
    GdiDib dib;
    if (!dib.create(static_cast<int>(rect.right - rect.left), static_cast<int>(rect.bottom - rect.top), error)) {
        return false;
    }
    dib.clear(0x00FF00FFu);
    if (::PrintWindow(window, dib.dc(), flags) == FALSE) {
        error = "PrintWindow failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    out = dib.snapshot();
    return true;
}

bool ScreenCapture::matches(const Bitmap& got, const std::vector<std::uint32_t>& expected, int tolerance, std::string& where) {
    if (static_cast<std::size_t>(got.width) * static_cast<std::size_t>(got.height) != expected.size()) {
        where = "the capture is " + std::to_string(got.width) + "x" + std::to_string(got.height) + " but the expected image has " + std::to_string(expected.size()) + " pixels";
        return false;
    }
    return matches_region(got, 0, 0, got.width, got.height, expected, tolerance, where);
}

bool ScreenCapture::matches_region(const Bitmap& got, int origin_x, int origin_y, int width, int height,
                                   const std::vector<std::uint32_t>& expected, int tolerance, std::string& where) {
    if (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > expected.size()) {
        where = "the expected image has " + std::to_string(expected.size()) + " pixels, fewer than the " + std::to_string(width) + "x" + std::to_string(height) + " compared";
        return false;
    }
    std::size_t bad = 0;
    std::string first;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t want = expected[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] & 0x00FFFFFFu;
            if (!got.valid(origin_x + x, origin_y + y)) {
                ++bad;
                continue;
            }
            const std::uint32_t have = got.at(origin_x + x, origin_y + y);
            if (!within(have, want, tolerance)) {
                if (first.empty()) {
                    first = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is " + rgb_text(have) + ", expected " + rgb_text(want);
                }
                ++bad;
            }
        }
    }
    if (bad == 0) {
        return true;
    }
    where = std::to_string(bad) + " of " + std::to_string(width * height) + " pixels differ, first " + first;
    return false;
}

}  // namespace lsw::suites
