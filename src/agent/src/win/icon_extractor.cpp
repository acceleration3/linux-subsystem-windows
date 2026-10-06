#include "lsw/agent/win/icon_extractor.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/base64.hpp"
#include "lsw/agent/win/com_ptr.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/gdiplus_runtime.hpp"

// clang-format off
// gdiplus.h needs windows.h + objidl.h visible first.
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

// Shared HICON → PNG (base64) path. Does NOT destroy the icon —
// caller decides based on how it was obtained (SHGetFileInfo /
// LoadIcon owners destroy; SendMessage(WM_GETICON) and
// GetClassLongPtr(GCLP_HICON*) leave ownership with the window /
// window class).
//
// We deliberately avoid ``Gdiplus::Bitmap::FromHICON``: for 32-bit
// ARGB icons (i.e. every modern Win11 app icon) it has a well-known
// bug where the alpha channel is filled with 255 instead of being
// copied from the source, producing solid square icons in any
// consumer that expects transparency. Instead we read the icon's
// 32-bit colour DIB directly via ``GetDIBits`` — that preserves the
// real alpha channel. For legacy 24-bit colour + 1-bit AND mask
// icons (rare these days), we detect "no alpha was present" and
// synthesise alpha from the mask.
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
    // Mask-only icons store color+mask in a double-height monochrome
    // bitmap; colour icons are normal height.
    const int h = ii.hbmColor != nullptr ? bm.bmHeight : (bm.bmHeight / 2);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    HDC hdc = ::GetDC(nullptr);
    if (ii.hbmColor != nullptr) {
        ::GetDIBits(hdc, ii.hbmColor, 0, static_cast<UINT>(h), pixels.data(), &bmi, DIB_RGB_COLORS);
    }

    // Detect whether the color bitmap actually carried an alpha
    // channel. 32-bit modern icons populate alpha; 24-bit legacy
    // icons leave the high byte at 0 after GetDIBits.
    bool has_alpha = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha && ii.hbmMask != nullptr) {
        // Synthesise alpha from the AND mask. The mask is 1-bit:
        // ``1`` = transparent, ``0`` = opaque. Reading it as a
        // 32-bit DIB gives us 0xFFFFFFFF for set bits and 0x00000000
        // for clear bits — invert and clamp to alpha.
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

    // Build a GDI+ bitmap that references ``pixels`` and PNG-encode
    // it. ``PixelFormat32bppARGB`` matches the GetDIBits layout
    // (BGRA bytes in memory, alpha in the high byte).
    Gdiplus::Bitmap gdi_bmp(w, h, w * 4, PixelFormat32bppARGB, pixels.data());
    const auto png = bitmap_to_png_bytes(gdi_bmp);
    if (png.empty()) {
        LSW_AGENT_WARN("gdi: PNG encode returned empty buffer");
        return {};
    }
    return util::Base64::encode(png);
}

}  // namespace

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
    bmi.bmiHeader.biHeight = -h;  // top-down
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

    // Prefer the highest-resolution embedded icon (typically 256x256
    // on Win11 apps). PrivateExtractIcons reads the .ico resource
    // table directly and returns the largest entry not exceeding the
    // requested size, so we get the source's best representation
    // without going through the system icon cache (which strips down
    // to 32x32 / 48x48 for the standard shell icons). KDE / GNOME
    // render launcher icons at 48-64px (HiDPI doubles that); supplying
    // a 256px source preserves crispness at every render size.
    HICON hicon = nullptr;
    UINT extracted = 0;
    ::PrivateExtractIconsW(path.c_str(), 0, 256, 256, &hicon, &extracted, 1, LR_DEFAULTCOLOR);
    if (hicon == nullptr || extracted == 0) {
        // PrivateExtractIcons fails for shell virtual items (Control
        // Panel applets, Store apps, .lnk targets, ...). Fall back to
        // SHGetFileInfo which resolves through the system icon cache
        // — lower resolution (32x32) but covers far more cases.
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

}  // namespace lsw::agent::win
