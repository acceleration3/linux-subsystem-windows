#include "support/layered_overlay.hpp"

#include <algorithm>

namespace lsw::suites {

bool LayeredOverlay::open(int x, int y, int width, int height, std::string& error) {
    x_ = x;
    y_ = y;
    width_ = width;
    height_ = height;
    TestWindowConfig config;
    config.x = x;
    config.y = y;
    config.width = width;
    config.height = height;
    config.popup = true;
    config.layered = true;
    config.topmost = true;
    config.visible = false;
    return dib_.create(width, height, error) && window_.create(config, error);
}

bool LayeredOverlay::update(const std::vector<std::uint32_t>& premultiplied_argb, std::string& error) {
    std::copy(premultiplied_argb.begin(), premultiplied_argb.end(), dib_.bits());
    ::GdiFlush();
    POINT position{x_, y_};
    SIZE size{width_, height_};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = ::GetDC(nullptr);
    const BOOL done = ::UpdateLayeredWindow(window_.hwnd(), screen, &position, &size, dib_.dc(), &source, 0, &blend, ULW_ALPHA);
    ::ReleaseDC(nullptr, screen);
    if (done == FALSE) {
        error = "UpdateLayeredWindow failed (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    ::ShowWindow(window_.hwnd(), SW_SHOWNOACTIVATE);
    return true;
}

void LayeredOverlay::move(int x, int y) {
    x_ = x;
    y_ = y;
    ::SetWindowPos(window_.hwnd(), nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayeredOverlay::close() {
    window_.destroy();
    dib_.destroy();
}

}  // namespace lsw::suites
