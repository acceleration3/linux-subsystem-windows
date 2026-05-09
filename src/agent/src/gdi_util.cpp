#include "lsw/agent/gdi_util.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
// gdiplus.h needs windows.h + objidl.h visible first.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
// clang-format on

#include <cstring>
#include <mutex>

namespace lsw::agent::gdi {

namespace {

ULONG_PTR g_gdiplus_token = 0;

int find_png_encoder_clsid(CLSID* out) {
    UINT n = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (size == 0)
        return -1;
    auto* info =
        static_cast<Gdiplus::ImageCodecInfo*>(::HeapAlloc(::GetProcessHeap(), 0, size));
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
    IStream* stream = nullptr;
    if (::CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
        return {};
    }
    if (bmp.Save(stream, &clsid, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return {};
    }
    HGLOBAL hg = nullptr;
    ::GetHGlobalFromStream(stream, &hg);
    const SIZE_T sz = ::GlobalSize(hg);
    std::vector<std::uint8_t> out(sz);
    if (void* p = ::GlobalLock(hg); p != nullptr) {
        std::memcpy(out.data(), p, sz);
        ::GlobalUnlock(hg);
    }
    stream->Release();
    return out;
}

}  // namespace

void ensure_gdiplus() {
    static std::once_flag once;
    std::call_once(once, [] {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdiplus_token, &in, nullptr);
    });
}

std::string base64_encode(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16)
                                  | (std::uint32_t(data[i + 1]) << 8) | std::uint32_t(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < data.size()) {
        std::uint32_t v = std::uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) {
            v |= std::uint32_t(data[i + 1]) << 8;
        }
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

std::string extract_icon_png_base64(const std::wstring& path) {
    ensure_gdiplus();

    SHFILEINFOW info{};
    if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                          SHGFI_ICON | SHGFI_LARGEICON) == 0) {
        LSW_AGENT_DEBUG("gdi_util: SHGetFileInfoW returned 0 (no icon)");
        return {};
    }
    HICON hicon = info.hIcon;
    if (hicon == nullptr) {
        LSW_AGENT_DEBUG("gdi_util: SHGetFileInfoW returned hIcon=null");
        return {};
    }

    ICONINFO ii{};
    if (!::GetIconInfo(hicon, &ii)) {
        LSW_AGENT_WARN(std::string{"gdi_util: GetIconInfo failed last_err="}
                       + std::to_string(::GetLastError()));
        ::DestroyIcon(hicon);
        return {};
    }
    BITMAP bmp{};
    HBITMAP src_bmp = (ii.hbmColor != nullptr) ? ii.hbmColor : ii.hbmMask;
    ::GetObject(src_bmp, sizeof(bmp), &bmp);
    const int w = bmp.bmWidth;
    // Mask-only icons are stored as a double-height monochrome bitmap;
    // colour icons are normal-height.
    const int h = (ii.hbmColor != nullptr) ? bmp.bmHeight : (bmp.bmHeight / 2);
    if (ii.hbmColor != nullptr)
        ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask != nullptr)
        ::DeleteObject(ii.hbmMask);
    if (w <= 0 || h <= 0) {
        LSW_AGENT_WARN(std::string{"gdi_util: icon has zero dimension w="}
                       + std::to_string(w) + " h=" + std::to_string(h));
        ::DestroyIcon(hicon);
        return {};
    }

    HDC hdc = ::GetDC(nullptr);
    HDC mem_dc = ::CreateCompatibleDC(hdc);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = ::CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HBITMAP old_sel = static_cast<HBITMAP>(::SelectObject(mem_dc, dib));
    ::DrawIconEx(mem_dc, 0, 0, hicon, w, h, 0, nullptr, DI_NORMAL);
    ::SelectObject(mem_dc, old_sel);
    ::DestroyIcon(hicon);

    Gdiplus::Bitmap bitmap(dib, nullptr);
    const auto png = bitmap_to_png_bytes(bitmap);

    ::DeleteObject(dib);
    ::DeleteDC(mem_dc);
    ::ReleaseDC(nullptr, hdc);

    if (png.empty()) {
        LSW_AGENT_WARN("gdi_util: PNG encode returned empty buffer");
        return {};
    }
    return base64_encode(png);
}

}  // namespace lsw::agent::gdi
