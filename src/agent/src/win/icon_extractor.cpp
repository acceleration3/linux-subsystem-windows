#include "lsw/agent/win/icon_extractor.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/win/com_ptr.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/gdiplus_runtime.hpp"

// clang-format off
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
// clang-format on

#include <cstring>
#include <vector>

namespace lsw::agent::win {

namespace {

int find_png_encoder_clsid(CLSID* out) {
    UINT n = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (size == 0)
        return -1;
    auto* info = static_cast<Gdiplus::ImageCodecInfo*>(::HeapAlloc(::GetProcessHeap(), 0, size));
    Gdiplus::GetImageEncoders(n, size, info);
    int rc = -1;
    for (UINT i = 0; i < n; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            *out = info[i].Clsid;
            rc = 0;
            break;
        }
    }
    ::HeapFree(::GetProcessHeap(), 0, info);
    return rc;
}

std::vector<std::uint8_t> bitmap_to_png_bytes(Gdiplus::Bitmap& bmp) {
    CLSID clsid;
    if (find_png_encoder_clsid(&clsid) != 0) {
        return {};
    }
    ComPtr<IStream> stream;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()) != S_OK) {
        return {};
    }
    if (bmp.Save(stream.Get(), &clsid, nullptr) != Gdiplus::Ok) {
        return {};
    }
    HGLOBAL hg = nullptr;
    ::GetHGlobalFromStream(stream.Get(), &hg);
    const SIZE_T sz = ::GlobalSize(hg);
    std::vector<std::uint8_t> out(sz);
    if (void* p = ::GlobalLock(hg); p != nullptr) {
        std::memcpy(out.data(), p, sz);
        ::GlobalUnlock(hg);
    }
    return out;
}

[[nodiscard]] std::string hicon_to_png_base64(HICON hicon) {
    if (hicon == nullptr) {
        return {};
    }
    ICONINFO ii{};
    if (!::GetIconInfo(hicon, &ii)) {
        LSW_AGENT_WARN("gdi: GetIconInfo failed last_err={}", ErrorText::last_error());
        return {};
    }
    BITMAP bm{};
    HBITMAP src = ii.hbmColor != nullptr ? ii.hbmColor : ii.hbmMask;
    if (::GetObjectW(src, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        if (ii.hbmColor)
            ::DeleteObject(ii.hbmColor);
        if (ii.hbmMask)
            ::DeleteObject(ii.hbmMask);
        return {};
    }
    const int w = bm.bmWidth;
    const int h = ii.hbmColor != nullptr ? bm.bmHeight : (bm.bmHeight / 2);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    HDC hdc = ::GetDC(nullptr);
    if (ii.hbmColor != nullptr) {
        ::GetDIBits(hdc, ii.hbmColor, 0, static_cast<UINT>(h), pixels.data(), &bmi, DIB_RGB_COLORS);
    }

    bool has_alpha = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha && ii.hbmMask != nullptr) {
        std::vector<std::uint8_t> mask(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
        BITMAPINFO mbmi = bmi;
        ::GetDIBits(hdc, ii.hbmMask, 0, static_cast<UINT>(h), mask.data(), &mbmi, DIB_RGB_COLORS);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const bool transparent = mask[i] != 0;
            pixels[i + 3] = transparent ? 0 : 255;
        }
    }
    ::ReleaseDC(nullptr, hdc);
    if (ii.hbmColor)
        ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        ::DeleteObject(ii.hbmMask);

    Gdiplus::Bitmap gdi_bmp(w, h, w * 4, PixelFormat32bppARGB, pixels.data());
    const auto png = bitmap_to_png_bytes(gdi_bmp);
    if (png.empty()) {
        LSW_AGENT_WARN("gdi: PNG encode returned empty buffer");
        return {};
    }
    return util::Base64::encode(png);
}

}

std::string IconExtractor::from_premultiplied_bitmap(HBITMAP bitmap) {
    GdiPlusRuntime::ensure_started();
    BITMAP bm{};
    if (bitmap == nullptr || ::GetObjectW(bitmap, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 ||
        bm.bmHeight <= 0) {
        return {};
    }
    const int w = bm.bmWidth;
    const int h = bm.bmHeight;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    HDC hdc = ::GetDC(nullptr);
    const int rows = ::GetDIBits(hdc, bitmap, 0, static_cast<UINT>(h), pixels.data(), &bmi, DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, hdc);
    if (rows != h) {
        LSW_AGENT_WARN("gdi: GetDIBits read {} of {} rows", rows, h);
        return {};
    }
    Gdiplus::Bitmap gdi_bmp(w, h, w * 4, PixelFormat32bppPARGB, pixels.data());
    const auto png = bitmap_to_png_bytes(gdi_bmp);
    return png.empty() ? std::string{} : util::Base64::encode(png);
}

std::string IconExtractor::from_executable(const std::wstring& path) {
    GdiPlusRuntime::ensure_started();

    HICON hicon = nullptr;
    UINT extracted = 0;
    ::PrivateExtractIconsW(path.c_str(), 0, 256, 256, &hicon, &extracted, 1, LR_DEFAULTCOLOR);
    if (hicon == nullptr || extracted == 0) {
        SHFILEINFOW info{};
        if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) ==
            0) {
            LSW_AGENT_DEBUG("gdi: no icon source for path");
            return {};
        }
        hicon = info.hIcon;
        if (hicon == nullptr) {
            LSW_AGENT_DEBUG("gdi: SHGetFileInfoW returned hIcon=null");
            return {};
        }
    }
    const std::string out = hicon_to_png_base64(hicon);
    ::DestroyIcon(hicon);
    return out;
}

}
