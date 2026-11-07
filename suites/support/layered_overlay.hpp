#pragma once

#include "support/gdi_dib.hpp"
#include "support/test_window.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
class LayeredOverlay {
public:
    [[nodiscard]] bool open(int x, int y, int width, int height, std::string& error);

    [[nodiscard]] bool update(const std::vector<std::uint32_t>& premultiplied_argb, std::string& error);

    void move(int x, int y);

    void close();

    [[nodiscard]] HWND hwnd() const { return window_.hwnd(); }

    [[nodiscard]] RECT rect() const { return {x_, y_, x_ + width_, y_ + height_}; }

    [[nodiscard]] GdiDib& dib() { return dib_; }

private:
    TestWindow window_;
    GdiDib dib_;
    int x_ = 0;
    int y_ = 0;
    int width_ = 0;
    int height_ = 0;
};
}  // namespace lsw::suites
