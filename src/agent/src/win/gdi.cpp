#include "lsw/agent/win/gdi.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
// gdiplus.h needs windows.h + objidl.h visible first.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <psapi.h>
// clang-format on

#include <cstring>
#include <memory>
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
        LSW_AGENT_WARN(std::string{"gdi: GetIconInfo failed last_err="}
                       + std::to_string(::GetLastError()));
        return {};
    }
    BITMAP bm{};
    HBITMAP src = ii.hbmColor != nullptr ? ii.hbmColor : ii.hbmMask;
    if (::GetObjectW(src, sizeof(bm), &bm) == 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  ::DeleteObject(ii.hbmMask);
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

    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4, 0);
    HDC hdc = ::GetDC(nullptr);
    if (ii.hbmColor != nullptr) {
        ::GetDIBits(hdc, ii.hbmColor, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
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
        std::vector<std::uint8_t> mask(static_cast<size_t>(w) * h * 4, 0);
        BITMAPINFO mbmi = bmi;
        ::GetDIBits(hdc, ii.hbmMask, 0, h, mask.data(), &mbmi, DIB_RGB_COLORS);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const bool transparent = mask[i] != 0;
            pixels[i + 3] = transparent ? 0 : 255;
        }
    }
    ::ReleaseDC(nullptr, hdc);
    if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  ::DeleteObject(ii.hbmMask);

    // Build a GDI+ bitmap that references ``pixels`` and PNG-encode
    // it. ``PixelFormat32bppARGB`` matches the GetDIBits layout
    // (BGRA bytes in memory, alpha in the high byte).
    Gdiplus::Bitmap gdi_bmp(w, h, w * 4, PixelFormat32bppARGB, pixels.data());
    const auto png = bitmap_to_png_bytes(gdi_bmp);
    if (png.empty()) {
        LSW_AGENT_WARN("gdi: PNG encode returned empty buffer");
        return {};
    }
    return base64_encode(png);
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
    ::PrivateExtractIconsW(path.c_str(), 0, 256, 256, &hicon, &extracted, 1,
                            LR_DEFAULTCOLOR);
    if (hicon == nullptr || extracted == 0) {
        // PrivateExtractIcons fails for shell virtual items (Control
        // Panel applets, Store apps, .lnk targets, ...). Fall back to
        // SHGetFileInfo which resolves through the system icon cache
        // — lower resolution (32x32) but covers far more cases.
        SHFILEINFOW info{};
        if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                              SHGFI_ICON | SHGFI_LARGEICON) == 0) {
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

std::string extract_window_icon_png_base64(void* hwnd_p) {
    ensure_gdiplus();
    auto hwnd = static_cast<HWND>(hwnd_p);
    if (hwnd == nullptr || !::IsWindow(hwnd)) {
        return {};
    }

    // Probe in size order so the manager gets the biggest icon
    // available — Win11 taskbars/Alt-Tab use 32x32+ and downscale if
    // needed. ``SendMessageTimeoutW`` returns nonzero ON SUCCESS; the
    // actual WM_GETICON reply (the HICON) comes back via the
    // ``lpdwResult`` out-pointer. Earlier code passed nullptr and
    // mistakenly cast the success code itself as the icon, which
    // either crashed in GDI+ or just produced an empty PNG depending
    // on the message dispatch path. Capture the out-pointer here.
    // ``SendMessage(WM_GETICON)`` does NOT transfer ownership; the
    // class-icon GCLP_HICON[SM] variants are owned by the window
    // class. Either way: do NOT ``DestroyIcon`` these.
    auto probe = [hwnd](WPARAM which) -> HICON {
        DWORD_PTR result = 0;
        const LRESULT ok = ::SendMessageTimeoutW(
            hwnd, WM_GETICON, which, 0,
            SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 200, &result);
        return ok != 0 ? reinterpret_cast<HICON>(result) : nullptr;
    };
    HICON hicon = probe(ICON_BIG);
    if (hicon == nullptr) hicon = probe(ICON_SMALL2);
    if (hicon == nullptr) hicon = probe(ICON_SMALL);
    if (hicon == nullptr) {
        hicon = reinterpret_cast<HICON>(::GetClassLongPtrW(hwnd, GCLP_HICON));
    }
    if (hicon == nullptr) {
        hicon = reinterpret_cast<HICON>(::GetClassLongPtrW(hwnd, GCLP_HICONSM));
    }
    if (hicon != nullptr) {
        return hicon_to_png_base64(hicon);
    }

    // Final fallback: the window neither has WM_GETICON nor a class
    // icon set (rare — happens with bare CreateWindow apps). Use the
    // process's main executable's icon, which is what the Win32 Alt-Tab
    // does in the same situation.
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return {};
    }
    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return {};
    }
    wchar_t exe_path[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    const BOOL ok = ::QueryFullProcessImageNameW(proc, 0, exe_path, &len);
    ::CloseHandle(proc);
    if (!ok || exe_path[0] == L'\0') {
        return {};
    }
    return extract_icon_png_base64(exe_path);
}

}  // namespace lsw::agent::gdi
