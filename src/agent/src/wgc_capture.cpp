// mingw-w64 bug: `boolean` and `BYTE` are both `unsigned char`, so
// windows.foundation.h's IReference<boolean> and IReference<BYTE>
// instantiate the SAME C++ type → "redefinition of IReference<unsigned
// char>". Pre-defining the boolean block's include guard skips it;
// WGC capture doesn't use IReference<boolean>.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

#include "lsw/agent/wgc_capture.hpp"

#include "lsw/agent/gdi_util.hpp"
#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <roapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
// Only the capture headers — they transitively pull the exact
// foundation/directx/graphics decls they need. Including
// windows.foundation.h (etc.) explicitly alongside them triggers a
// mingw-w64 header bug: a duplicate IReference<BYTE> definition.
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
// clang-format on

#include <cstring>
#include <map>
#include <mutex>

namespace WGC = ABI::Windows::Graphics::Capture;
namespace WGD = ABI::Windows::Graphics::DirectX;
namespace WGD3D = ABI::Windows::Graphics::DirectX::Direct3D11;
namespace WF = ABI::Windows::Foundation;
using ABI::Windows::Graphics::SizeInt32;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

// Not in mingw's headers — declared from the SDK contract. Exported by
// d3d11.dll; the interface lets us pull an ID3D11Texture2D back out of
// a WinRT IDirect3DSurface.
extern "C" HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(
    IDXGIDevice*, IInspectable**);

// mingw-g++ ignores __declspec(uuid), so carry the IID explicitly and
// QueryInterface with it.
static const GUID kIID_Direct3DDxgiInterfaceAccess = {
    0xA9B3D012, 0x3DF2, 0x4EE3,
    {0xB8, 0xD1, 0x86, 0x95, 0xF4, 0x57, 0xD3, 0xC1}};
struct IDirect3DDxgiInterfaceAccess : ::IUnknown {
    virtual HRESULT __stdcall GetInterface(REFIID, void**) = 0;
};

namespace lsw::agent {

namespace {

std::mutex g_mu;
bool g_init_done = false;
bool g_init_ok = false;
ComPtr<ID3D11Device> g_d3d;
ComPtr<ID3D11DeviceContext> g_ctx;
ComPtr<WGD3D::IDirect3DDevice> g_rt_device;

struct Session {
    ComPtr<WGC::IGraphicsCaptureItem> item;
    ComPtr<WGC::IDirect3D11CaptureFramePool> pool;
    ComPtr<WGC::IGraphicsCaptureSession> session;
    ComPtr<ID3D11Texture2D> staging;
    UINT staging_w = 0;
    UINT staging_h = 0;
    SizeInt32 pool_size{0, 0};  // size the frame pool was (re)created at
    std::vector<std::uint8_t> last_png;  // serve while waiting for a frame
};

std::map<HWND, Session> g_sessions;

bool init_locked() {
    if (g_init_done) {
        return g_init_ok;
    }
    g_init_done = true;
    // The agent never called RoInitialize; multi-threaded apartment.
    // S_FALSE / RPC_E_CHANGED_MODE are both fine.
    ::RoInitialize(RO_INIT_MULTITHREADED);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     flags, nullptr, 0, D3D11_SDK_VERSION,
                                     &g_d3d, &fl, &g_ctx);
    if (FAILED(hr)) {
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                 nullptr, 0, D3D11_SDK_VERSION, &g_d3d, &fl,
                                 &g_ctx);
    }
    if (FAILED(hr)) {
        LSW_AGENT_WARN("wgc: D3D11CreateDevice failed hr="
                       + std::to_string(hr));
        return false;
    }
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(g_d3d.As(&dxgi))) {
        return false;
    }
    ComPtr<IInspectable> insp;
    hr = ::CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &insp);
    if (FAILED(hr) || !insp) {
        LSW_AGENT_WARN("wgc: CreateDirect3D11DeviceFromDXGIDevice hr="
                       + std::to_string(hr));
        return false;
    }
    if (FAILED(insp.As(&g_rt_device))) {
        return false;
    }
    g_init_ok = true;
    LSW_AGENT_INFO("wgc: initialized (Windows.Graphics.Capture ready)");
    return true;
}

// Build/refresh a session for hwnd. Caller holds g_mu.
Session* ensure_session_locked(HWND hwnd) {
    auto it = g_sessions.find(hwnd);
    if (it != g_sessions.end()) {
        return &it->second;
    }
    // Only reap sessions whose window is truly gone before creating a
    // new one. We deliberately do NOT destroy sessions on unsubscribe:
    // the watcher subscribes/unsubscribes the same hwnds many times a
    // second (popup churn at the 50 ms poll), and tearing a WGC
    // capture session up/down that fast destabilises WGC/D3D (the
    // agent crash). Reuse keeps it stable and is far cheaper.
    for (auto i = g_sessions.begin(); i != g_sessions.end();) {
        if (::IsWindow(i->first) == FALSE) {
            i = g_sessions.erase(i);
        } else {
            ++i;
        }
    }
    ComPtr<IGraphicsCaptureItemInterop> interop;
    HRESULT hr = ::RoGetActivationFactory(
        HStringReference(L"Windows.Graphics.Capture.GraphicsCaptureItem").Get(),
        __uuidof(IGraphicsCaptureItemInterop), &interop);
    if (FAILED(hr)) {
        LSW_AGENT_WARN("wgc: no GraphicsCaptureItem factory hr="
                       + std::to_string(hr));
        return nullptr;
    }
    Session s;
    hr = interop->CreateForWindow(
        hwnd, __uuidof(WGC::IGraphicsCaptureItem),
        reinterpret_cast<void**>(s.item.GetAddressOf()));
    if (FAILED(hr) || !s.item) {
        return nullptr;
    }
    SizeInt32 size{};
    s.item->get_Size(&size);
    if (size.Width <= 0 || size.Height <= 0) {
        return nullptr;
    }
    ComPtr<WGC::IDirect3D11CaptureFramePoolStatics2> pool_statics;
    hr = ::RoGetActivationFactory(
        HStringReference(
            L"Windows.Graphics.Capture.Direct3D11CaptureFramePool")
            .Get(),
        __uuidof(WGC::IDirect3D11CaptureFramePoolStatics2), &pool_statics);
    if (FAILED(hr)) {
        return nullptr;
    }
    hr = pool_statics->CreateFreeThreaded(
        g_rt_device.Get(), WGD::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2,
        size, &s.pool);
    if (FAILED(hr) || !s.pool) {
        return nullptr;
    }
    s.pool_size = size;
    hr = s.pool->CreateCaptureSession(s.item.Get(), &s.session);
    if (FAILED(hr) || !s.session) {
        return nullptr;
    }
    // Best-effort: no capture-cursor, no yellow capture border. These
    // QIs fail on older builds — harmless, just leave defaults.
    ComPtr<WGC::IGraphicsCaptureSession2> s2;
    if (SUCCEEDED(s.session.As(&s2))) {
        s2->put_IsCursorCaptureEnabled(false);
    }
    ComPtr<WGC::IGraphicsCaptureSession3> s3;
    if (SUCCEEDED(s.session.As(&s3))) {
        s3->put_IsBorderRequired(false);
    }
    if (FAILED(s.session->StartCapture())) {
        return nullptr;
    }
    auto res = g_sessions.emplace(hwnd, std::move(s));
    return &res.first->second;
}

// PNG-encode a tight BGRA (straight-alpha) buffer.
std::vector<std::uint8_t> encode_bgra_png(const std::uint8_t* bgra, int w,
                                          int h) {
    gdi::ensure_gdiplus();
    Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppARGB,
                        const_cast<BYTE*>(bgra));
    CLSID clsid;
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) {
        return {};
    }
    auto* info = static_cast<Gdiplus::ImageCodecInfo*>(
        ::HeapAlloc(::GetProcessHeap(), 0, size));
    Gdiplus::GetImageEncoders(num, size, info);
    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            clsid = info[i].Clsid;
            found = true;
            break;
        }
    }
    ::HeapFree(::GetProcessHeap(), 0, info);
    if (!found) {
        return {};
    }
    IStream* stream = nullptr;
    ::CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    bmp.Save(stream, &clsid, nullptr);
    HGLOBAL hg = nullptr;
    ::GetHGlobalFromStream(stream, &hg);
    const SIZE_T sz = ::GlobalSize(hg);
    std::vector<std::uint8_t> png(sz);
    if (void* p = ::GlobalLock(hg); p != nullptr) {
        std::memcpy(png.data(), p, sz);
        ::GlobalUnlock(hg);
    }
    stream->Release();
    return png;
}

// Pull one frame, crop to client area, un-premultiply. ``stats`` (if
// non-null) gets an alpha histogram instead of producing a PNG.
std::vector<std::uint8_t> grab(HWND hwnd, bool& ok, std::string* stats) {
    ok = false;
    std::lock_guard<std::mutex> lock(g_mu);
    if (!init_locked()) {
        return {};
    }
    if (::IsWindow(hwnd) == FALSE) {
        g_sessions.erase(hwnd);  // window gone — reap its session now
        return {};
    }
    Session* s = ensure_session_locked(hwnd);
    if (s == nullptr) {
        return {};
    }
    // NEVER sleep here — this runs on the agent's single-threaded
    // host loop. Drain the free-threaded pool to the NEWEST queued
    // frame (non-blocking) and discard older ones so latency can't
    // accumulate; if nothing is ready yet, serve the cached frame and
    // return immediately. (The old Sleep-retry blocked the loop up to
    // 50 ms per window per tick — that was the lag.)
    ComPtr<WGC::IDirect3D11CaptureFrame> frame;
    for (;;) {
        ComPtr<WGC::IDirect3D11CaptureFrame> next;
        s->pool->TryGetNextFrame(&next);
        if (!next) {
            break;
        }
        frame = std::move(next);
    }
    if (!frame) {
        ok = !s->last_png.empty();
        return s->last_png;  // no new frame this tick — reuse last
    }

    // The window may have resized (we drive resize_window to match
    // the host widget). The frame pool is fixed-size, so a stale pool
    // clips/chops the content. When the frame's ContentSize differs,
    // Recreate the pool at the new size and serve the cached frame
    // this tick; the next tick captures at the correct size.
    SizeInt32 cs{};
    if (SUCCEEDED(frame->get_ContentSize(&cs))
        && (cs.Width != s->pool_size.Width || cs.Height != s->pool_size.Height)
        && cs.Width > 0 && cs.Height > 0) {
        if (SUCCEEDED(s->pool->Recreate(
                g_rt_device.Get(),
                WGD::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, cs))) {
            s->pool_size = cs;
        }
        frame.Reset();
        ok = !s->last_png.empty();
        return s->last_png;
    }
    ComPtr<WGD3D::IDirect3DSurface> surface;
    frame->get_Surface(&surface);
    ComPtr<IDirect3DDxgiInterfaceAccess> access;
    ComPtr<ID3D11Texture2D> tex;
    if (!surface
        || FAILED(surface->QueryInterface(
               kIID_Direct3DDxgiInterfaceAccess,
               reinterpret_cast<void**>(access.GetAddressOf())))
        || !access
        || FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), &tex))
        || !tex) {
        return {};
    }
    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    if (s->staging == nullptr || s->staging_w != desc.Width
        || s->staging_h != desc.Height) {
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        s->staging.Reset();
        if (FAILED(g_d3d->CreateTexture2D(&sd, nullptr, &s->staging))) {
            return {};
        }
        s->staging_w = desc.Width;
        s->staging_h = desc.Height;
    }
    g_ctx->CopyResource(s->staging.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(g_ctx->Map(s->staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
        return {};
    }

    // FULL-WINDOW model: the WGC texture already spans the window's
    // DWM extended frame bounds (the whole visible window incl. title
    // bar / menu bar, excl. the external drop shadow) — exactly what
    // we want to mirror. No client crop.
    const int ox = 0;
    const int oy = 0;
    const int cw = static_cast<int>(desc.Width);
    const int ch = static_cast<int>(desc.Height);

    std::vector<std::uint8_t> buf(static_cast<size_t>(cw) * ch * 4);
    const auto* base = static_cast<const std::uint8_t*>(map.pData);
    std::uint64_t a0 = 0;
    std::uint64_t a255 = 0;
    std::uint64_t amid = 0;
    for (int y = 0; y < ch; ++y) {
        const std::uint8_t* srow = base + static_cast<size_t>(y + oy)
                                              * map.RowPitch
                                       + static_cast<size_t>(ox) * 4;
        std::uint8_t* drow = buf.data() + static_cast<size_t>(y) * cw * 4;
        for (int x = 0; x < cw; ++x) {
            const std::uint8_t* sp = srow + x * 4;  // B,G,R,A premultiplied
            std::uint8_t* dp = drow + x * 4;
            const int a = sp[3];
            if (a == 0) {
                dp[0] = dp[1] = dp[2] = dp[3] = 0;
                ++a0;
            } else {
                // Un-premultiply (WGC alpha is premultiplied).
                for (int c = 0; c < 3; ++c) {
                    int v = sp[c] * 255 / a;
                    dp[c] = static_cast<std::uint8_t>(v > 255 ? 255 : v);
                }
                dp[3] = static_cast<std::uint8_t>(a);
                if (a == 255) {
                    ++a255;
                } else {
                    ++amid;
                }
            }
        }
    }
    g_ctx->Unmap(s->staging.Get(), 0);
    frame.Reset();

    if (stats != nullptr) {
        RECT gwr{};
        ::GetWindowRect(hwnd, &gwr);
        RECT efb{};
        ::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &efb,
                                sizeof(efb));
        auto r = [](const RECT& q) {
            return std::to_string(q.left) + "," + std::to_string(q.top) + ","
                   + std::to_string(q.right) + "," + std::to_string(q.bottom);
        };
        *stats = std::string{"{\"w\":"} + std::to_string(cw) + ",\"h\":"
                 + std::to_string(ch) + ",\"a0\":" + std::to_string(a0)
                 + ",\"a255\":" + std::to_string(a255) + ",\"amid\":"
                 + std::to_string(amid)
                 + ",\"tex\":\"" + std::to_string(desc.Width) + "x"
                 + std::to_string(desc.Height) + "\""
                 + ",\"pool\":\"" + std::to_string(s->pool_size.Width) + "x"
                 + std::to_string(s->pool_size.Height) + "\""
                 + ",\"winrect\":\"" + r(gwr) + "\""
                 + ",\"efb\":\"" + r(efb) + "\""
                 + ",\"ox\":" + std::to_string(ox) + ",\"oy\":"
                 + std::to_string(oy) + "}";
        ok = true;
        return {};
    }
    auto png = encode_bgra_png(buf.data(), cw, ch);
    if (!png.empty()) {
        s->last_png = png;
        ok = true;
    }
    return png;
}

}  // namespace

bool WgcCapture::available() {
    std::lock_guard<std::mutex> lock(g_mu);
    return init_locked();
}

std::vector<std::uint8_t> WgcCapture::capture_png(std::uintptr_t hwnd,
                                                  bool& ok) {
    return grab(reinterpret_cast<HWND>(hwnd), ok, nullptr);
}

void WgcCapture::release(std::uintptr_t /*hwnd*/) {
    // Intentionally a no-op. unsubscribe_window fires constantly as
    // the watcher churns popups (50 ms poll); destroying the WGC
    // session here and recreating it moments later crashed the agent.
    // Sessions are instead reaped lazily in ensure_session_locked /
    // grab once the window is genuinely gone (IsWindow == false).
}

std::string WgcCapture::debug_json(std::uintptr_t hwnd) {
    // Diagnostic only — unlike the real path it MAY block, retrying
    // until a frame flows past session-warmup / pool-recreate so the
    // geometry is actually reported.
    std::string stats = "{\"err\":\"fail\"}";
    for (int i = 0; i < 40; ++i) {
        bool ok = false;
        grab(reinterpret_cast<HWND>(hwnd), ok, &stats);
        if (stats.rfind("{\"err\"", 0) != 0) {
            break;
        }
        ::Sleep(15);
    }
    return stats;
}

}  // namespace lsw::agent
