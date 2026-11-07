#include "support/gdi_dib.hpp"

#include <algorithm>

namespace lsw::suites {

bool GdiDib::create(int width, int height, std::string& error) {
    destroy();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = ::GetDC(nullptr);
    dc_ = ::CreateCompatibleDC(screen);
    void* memory = nullptr;
    bitmap_ = dc_ != nullptr ? ::CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &memory, nullptr, 0) : nullptr;
    ::ReleaseDC(nullptr, screen);
    if (dc_ == nullptr || bitmap_ == nullptr || memory == nullptr) {
        error = "CreateDIBSection " + std::to_string(width) + "x" + std::to_string(height) + " failed (error " + std::to_string(::GetLastError()) + ")";
        destroy();
        return false;
    }
    previous_ = ::SelectObject(dc_, bitmap_);
    bits_ = static_cast<std::uint32_t*>(memory);
    width_ = width;
    height_ = height;
    return true;
}

void GdiDib::destroy() {
    ::GdiFlush();
    if (dc_ != nullptr && previous_ != nullptr) {
        ::SelectObject(dc_, previous_);
    }
    if (bitmap_ != nullptr) {
        ::DeleteObject(bitmap_);
    }
    if (dc_ != nullptr) {
        ::DeleteDC(dc_);
    }
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    width_ = 0;
    height_ = 0;
}

void GdiDib::clear(std::uint32_t xrgb) {
    ::GdiFlush();
    std::fill(bits_, bits_ + static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), xrgb);
}

Bitmap GdiDib::snapshot() const {
    ::GdiFlush();
    Bitmap out;
    out.width = width_;
    out.height = height_;
    out.pixels.assign(bits_, bits_ + static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
    return out;
}

}  // namespace lsw::suites
