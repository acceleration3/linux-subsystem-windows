#pragma once

#include "support/bitmap.hpp"

#include <windows.h>

#include <string>

namespace lsw::suites {
class GdiDib {
public:
    GdiDib() = default;
    GdiDib(const GdiDib&) = delete;
    GdiDib& operator=(const GdiDib&) = delete;
    ~GdiDib() { destroy(); }

    [[nodiscard]] bool create(int width, int height, std::string& error);

    void destroy();

    void clear(std::uint32_t xrgb);

    [[nodiscard]] Bitmap snapshot() const;

    [[nodiscard]] HDC dc() const { return dc_; }

    [[nodiscard]] std::uint32_t* bits() const { return bits_; }

    [[nodiscard]] int width() const { return width_; }

    [[nodiscard]] int height() const { return height_; }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
}  // namespace lsw::suites
