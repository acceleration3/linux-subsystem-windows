// mingw-w64 bug: `boolean` and `BYTE` are both `unsigned char`, so
// windows.foundation.h's IReference<boolean> and IReference<BYTE>
// instantiate the SAME C++ type → "redefinition of IReference<unsigned
// char>". Pre-defining the boolean block's include guard skips it;
// WGC capture doesn't use IReference<boolean>.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

#include "lsw/agent/capture/wgc.hpp"


#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/shmem.hpp"
#include "lsw/agent/win/window.hpp"

// clang-format off
#include <windows.h>
#include <roapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objidl.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
// Only the capture headers — they transitively pull the exact
// foundation/directx/graphics decls they need. Including
// windows.foundation.h (etc.) explicitly alongside them triggers a
// mingw-w64 header bug: a duplicate IReference<BYTE> definition.
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
// clang-format on

#include <algorithm>
#include <cstddef>
#include <cstdio>
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

namespace lsw::agent::capture {

namespace {

std::mutex g_mu;
bool g_init_done = false;
bool g_init_ok = false;
ComPtr<ID3D11Device> g_d3d;
ComPtr<ID3D11DeviceContext> g_ctx;
ComPtr<WGD3D::IDirect3DDevice> g_rt_device;
LARGE_INTEGER g_qpc_freq{};

inline LONGLONG qpc_now() {
    LARGE_INTEGER li{};
    ::QueryPerformanceCounter(&li);
    return li.QuadPart;
}
inline std::uint64_t qpc_to_us(LONGLONG d) {
    if (g_qpc_freq.QuadPart == 0) return 0;
    return static_cast<std::uint64_t>(
        (static_cast<double>(d) * 1'000'000.0)
        / static_cast<double>(g_qpc_freq.QuadPart));
}

// Is every pixel of this band fully transparent? BGRA, so alpha is
// byte 3.
bool band_is_empty(const std::uint8_t* pixels, UINT pitch, int x, int y, int w,
                   int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    for (int row = y; row < y + h; ++row) {
        const std::uint8_t* p = pixels + (static_cast<std::size_t>(row) * pitch)
                                + (static_cast<std::size_t>(x) * 4U);
        for (int col = 0; col < w; ++col) {
            if (p[(static_cast<std::size_t>(col) * 4U) + 3U] != 0U) {
                return false;
            }
        }
    }
    return true;
}

// A frame that was never finished painting.
//
// The window's ON-SCREEN part is where the application definitely draws
// — the overhang of a maximized window is not, and is transparent by
// rights. So an outermost row or column INSIDE that part being empty
// from end to end means the capture is short of pixels the window has:
// PrintWindow of the same window comes back fully opaque.
//
// This is what maximizing leaves behind. The frame that lands during the
// transition is missing its top and left edges, and since WGC only
// produces a frame when something changes, an application that then sits
// idle never sends a better one — the mirror keeps the hole for as long
// as the window is up.
//
// Rounded corners do not trip it: those leave the corners clear, not a
// whole edge. Layered windows are excluded outright — a window shaped by
// its own alpha is entitled to a transparent edge.
bool frame_is_incomplete(HWND hwnd, const std::uint8_t* pixels, UINT pitch,
                         UINT tex_w, UINT tex_h) {
    if (pixels == nullptr
        || (::GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0) {
        return false;
    }
    const auto vis =
        win::Window::visible_rect(reinterpret_cast<std::uintptr_t>(hwnd));
    if (!vis.valid) {
        return false;
    }
    // Only what is really in this texture — the two disagree for a frame
    // or two after a resize.
    const int x = vis.x;
    const int y = vis.y;
    const int w = std::min(vis.w, static_cast<int>(tex_w) - x);
    const int h = std::min(vis.h, static_cast<int>(tex_h) - y);
    if (x < 0 || y < 0 || w <= 1 || h <= 1) {
        return false;
    }
    return band_is_empty(pixels, pitch, x, y, w, 1)
           || band_is_empty(pixels, pitch, x, y + h - 1, w, 1)
           || band_is_empty(pixels, pitch, x, y, 1, h)
           || band_is_empty(pixels, pitch, x + w - 1, y, 1, h);
}

// How much of THIS texture is off the guest's screen, packed one byte
// per edge for the slot header (left | top<<8 | right<<16 | bottom<<24).
//
// Computed here, against the texture actually being committed, and only
// when the two agree on size: mid-transition the window's frame rect has
// already changed while the texture still holds the old one, and insets
// from the new rect would crop the old pixels. Zero means "all of it",
// which is both the common case and the safe answer.
std::uint32_t visible_insets_for(HWND hwnd, UINT tex_w, UINT tex_h) {
    const auto vis =
        win::Window::visible_rect(reinterpret_cast<std::uintptr_t>(hwnd));
    const auto frame =
        win::Window::frame_size(reinterpret_cast<std::uintptr_t>(hwnd));
    if (!vis.valid || frame.first != static_cast<int>(tex_w)
        || frame.second != static_cast<int>(tex_h)) {
        return 0;
    }
    const int left = vis.x;
    const int top = vis.y;
    const int right = static_cast<int>(tex_w) - (vis.x + vis.w);
    const int bottom = static_cast<int>(tex_h) - (vis.y + vis.h);
    if (left < 0 || top < 0 || right < 0 || bottom < 0 || left > 255 || top > 255
        || right > 255 || bottom > 255) {
        return 0;
    }
    return static_cast<std::uint32_t>(left)
           | (static_cast<std::uint32_t>(top) << 8U)
           | (static_cast<std::uint32_t>(right) << 16U)
           | (static_cast<std::uint32_t>(bottom) << 24U);
}

struct Session {
    ComPtr<WGC::IGraphicsCaptureItem> item;
    ComPtr<WGC::IDirect3D11CaptureFramePool> pool;
    ComPtr<WGC::IGraphicsCaptureSession> session;
    ComPtr<ID3D11Texture2D> staging;
    UINT staging_w = 0;
    UINT staging_h = 0;
    SizeInt32 pool_size{0, 0};  // size the frame pool was (re)created at
    int shmem_slot = -1;         // index in transport::shmem allocator
    int repairs = 0;             // see repair_locked
    LONGLONG last_repair_qpc = 0;

    // Rolling 1-second stats window. Reset after each emit.
    LONGLONG stats_window_start_qpc = 0;
    std::uint64_t calls = 0;          // total grab() calls
    std::uint64_t delivered = 0;      // grabs that committed a fresh frame
    std::uint64_t cached = 0;         // grabs where no new frame was queued
    std::uint64_t errors = 0;         // grabs that hit a failure path
    std::uint64_t drained = 0;        // frames discarded by drain-to-newest
    std::uint64_t recreates = 0;      // frame-pool recreates (size change)
    std::uint64_t over_60 = 0;        // delivered grabs with total > 16.667 ms
    std::uint64_t over_30 = 0;        // delivered grabs with total > 33.333 ms
    // Per-stage sums + maxes (us), denominator is `delivered`.
    std::uint64_t t_copy_sum_us  = 0, t_copy_max_us  = 0;
    std::uint64_t t_map_sum_us   = 0, t_map_max_us   = 0;
    std::uint64_t t_write_sum_us = 0, t_write_max_us = 0;
    std::uint64_t t_grab_sum_us  = 0, t_grab_max_us  = 0;
};

std::map<HWND, Session> g_sessions;

// When, and at what size, each window's capture session was last
// rebuilt. Kept outside Session because rebuilding destroys the Session,
// and the whole point of the record is to outlive it.
struct RebuildStamp {
    LONGLONG qpc = 0;
    UINT width = 0;
    UINT height = 0;
};
std::map<HWND, RebuildStamp> g_last_rebuild;

enum class Repair { None, Repaint, Rebuild };

constexpr int kMaxRepairs = 3;

// What to do about a window whose last frame came up short.
//
// A half-painted frame is not itself the problem — the problem is that
// it is the LAST one. WGC produces a frame only when something changes,
// so an application that maximizes and then sits idle (produced=0.0/s in
// the stats line) leaves the mirror holding the frame that landed
// mid-transition, with its top and left edges missing, for as long as
// the window is up.
//
// First a repaint request, which is cheap and enough for a window that
// draws through WM_PAINT. If the next look is still short, rebuild the
// capture session: a fresh GraphicsCaptureItem makes DWM hand over the
// whole window surface again, which is what an agent restart was
// accidentally doing every time it "fixed" this.
//
// Bounded twice over. Sessions are deliberately not torn down at the
// rate the watcher subscribes and unsubscribes — that churn destabilised
// WGC and crashed the agent — so a rebuild is rate-limited per window by
// a clock that outlives the session it replaces.
Repair plan_repair_locked(HWND hwnd, Session& s, UINT tex_w, UINT tex_h) {
    constexpr std::uint64_t kMinGapUs = 150'000;
    constexpr std::uint64_t kSameSizeRebuildGapUs = 5'000'000;
    if (s.repairs >= kMaxRepairs) {
        return Repair::None;
    }
    const LONGLONG now = qpc_now();
    if (s.last_repair_qpc != 0 && qpc_to_us(now - s.last_repair_qpc) < kMinGapUs) {
        return Repair::None;
    }
    s.last_repair_qpc = now;
    ++s.repairs;
    if (s.repairs == 1) {
        return Repair::Repaint;
    }
    // The floor is per GEOMETRY, not per window. A window that is a
    // different size than when it was last rebuilt has genuinely new
    // pixels to get wrong and is repaired straight away — a flat
    // per-window timer instead made every other maximize come out
    // broken, because toggling faster than the timer refused the repair
    // outright.
    //
    // The timer still guards the pathological case: the same window, at
    // the same size, coming up short over and over. Session churn is
    // what destabilised WGC, so it must stay bounded there.
    const auto last = g_last_rebuild.find(hwnd);
    const bool same_geometry = last != g_last_rebuild.end()
                               && last->second.width == tex_w
                               && last->second.height == tex_h;
    if (same_geometry && qpc_to_us(now - last->second.qpc) < kSameSizeRebuildGapUs) {
        return Repair::None;
    }
    g_last_rebuild[hwnd] = {now, tex_w, tex_h};
    return Repair::Rebuild;
}

bool init_locked() {
    if (g_init_done) {
        return g_init_ok;
    }
    g_init_done = true;
    ::QueryPerformanceFrequency(&g_qpc_freq);
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
            if (i->second.shmem_slot >= 0) {
                transport::shmem().free_slot(i->second.shmem_slot);
            }
            g_last_rebuild.erase(i->first);  // the hwnd is free to be reused
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
    s.stats_window_start_qpc = qpc_now();
    auto res = g_sessions.emplace(hwnd, std::move(s));
    return &res.first->second;
}

// Emit + reset the rolling stats window if at least 1 s has elapsed.
// Caller holds g_mu.
void maybe_emit_stats_locked(HWND hwnd, Session& s) {
    if (g_qpc_freq.QuadPart == 0 || s.stats_window_start_qpc == 0) {
        return;
    }
    const LONGLONG now = qpc_now();
    const double secs = static_cast<double>(now - s.stats_window_start_qpc)
                        / static_cast<double>(g_qpc_freq.QuadPart);
    if (secs < 1.0) {
        return;
    }
    auto avg_ms = [](std::uint64_t sum, std::uint64_t n) {
        return n == 0 ? 0.0
                      : (static_cast<double>(sum) / static_cast<double>(n))
                            / 1000.0;
    };
    const double produced_rate =
        static_cast<double>(s.delivered + s.drained) / secs;
    char buf[768];
    std::snprintf(buf, sizeof(buf),
        "wgc[hwnd=0x%llx]: poll=%.1f/s produced=%.1f/s deliver=%.1f/s "
        "cache=%.1f/s drained=%.2f/s recreates=%llu err=%llu | "
        "copy=%.2f/%.2fms map=%.2f/%.2fms write=%.2f/%.2fms "
        "grab=%.2f/%.2fms | over60=%llu over30=%llu",
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd)),
        static_cast<double>(s.calls) / secs,
        produced_rate,
        static_cast<double>(s.delivered) / secs,
        static_cast<double>(s.cached) / secs,
        static_cast<double>(s.drained) / secs,
        static_cast<unsigned long long>(s.recreates),
        static_cast<unsigned long long>(s.errors),
        avg_ms(s.t_copy_sum_us, s.delivered),
        static_cast<double>(s.t_copy_max_us) / 1000.0,
        avg_ms(s.t_map_sum_us, s.delivered),
        static_cast<double>(s.t_map_max_us) / 1000.0,
        avg_ms(s.t_write_sum_us, s.delivered),
        static_cast<double>(s.t_write_max_us) / 1000.0,
        avg_ms(s.t_grab_sum_us, s.delivered),
        static_cast<double>(s.t_grab_max_us) / 1000.0,
        static_cast<unsigned long long>(s.over_60),
        static_cast<unsigned long long>(s.over_30));
    LSW_AGENT_INFO(std::string(buf));

    s.stats_window_start_qpc = now;
    s.calls = s.delivered = s.cached = s.errors = 0;
    s.drained = s.recreates = 0;
    s.over_60 = s.over_30 = 0;
    s.t_copy_sum_us = s.t_copy_max_us = 0;
    s.t_map_sum_us = s.t_map_max_us = 0;
    s.t_write_sum_us = s.t_write_max_us = 0;
    s.t_grab_sum_us = s.t_grab_max_us = 0;
}

inline void bump_stage(std::uint64_t& sum, std::uint64_t& mx,
                       std::uint64_t us) {
    sum += us;
    if (us > mx) mx = us;
}

// Pull one frame, crop to client area, un-premultiply. ``stats`` (if
// Returns true if the window still exists (caller keeps the
// subscription); false if it's gone (host_endpoint should emit
// frame_gone and drop the sub). Side effect: commits BGRA frames
// into the shmem slot owned by ``s`` and rings the doorbell.
bool grab(HWND hwnd) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!init_locked()) {
        return false;
    }
    if (::IsWindow(hwnd) == FALSE) {
        // Window gone — release any slot we owned for it.
        if (auto it = g_sessions.find(hwnd); it != g_sessions.end()) {
            if (it->second.shmem_slot >= 0) {
                transport::shmem().free_slot(it->second.shmem_slot);
            }
            g_sessions.erase(it);
        }
        g_last_rebuild.erase(hwnd);
        return false;
    }
    Session* s = ensure_session_locked(hwnd);
    if (s == nullptr) {
        return true;  // transient failure (no D3D / shmem) — keep sub
    }

    const LONGLONG t_grab_start = qpc_now();
    ++s->calls;

    enum class Outcome { Delivered, Cached, Error };
    auto finalize = [&](Outcome oc) {
        const std::uint64_t total_us = qpc_to_us(qpc_now() - t_grab_start);
        bump_stage(s->t_grab_sum_us, s->t_grab_max_us, total_us);
        switch (oc) {
            case Outcome::Delivered:
                ++s->delivered;
                if (total_us > 16'667) ++s->over_60;
                if (total_us > 33'333) ++s->over_30;
                break;
            case Outcome::Cached: ++s->cached; break;
            case Outcome::Error:  ++s->errors; break;
        }
        maybe_emit_stats_locked(hwnd, *s);
    };

    // Drain to newest queued frame; discard older ones so latency
    // can't accumulate across slow ticks.
    ComPtr<WGC::IDirect3D11CaptureFrame> frame;
    for (;;) {
        ComPtr<WGC::IDirect3D11CaptureFrame> next;
        s->pool->TryGetNextFrame(&next);
        if (!next) break;
        if (frame) ++s->drained;
        frame = std::move(next);
    }
    if (!frame) {
        // Nothing new — which is exactly when a frame that came up short
        // matters, because there is nothing on the way to replace it.
        // Re-examine the one we last copied out; ``plan_repair_locked``
        // rate-limits this, so an idle window is scanned a few times
        // after a resize and then left alone.
        bool short_frame = false;
        if (s->staging != nullptr && s->repairs < kMaxRepairs) {
            D3D11_MAPPED_SUBRESOURCE last{};
            if (SUCCEEDED(g_ctx->Map(s->staging.Get(), 0, D3D11_MAP_READ, 0, &last))) {
                short_frame = frame_is_incomplete(
                    hwnd, static_cast<const std::uint8_t*>(last.pData),
                    last.RowPitch, s->staging_w, s->staging_h);
                g_ctx->Unmap(s->staging.Get(), 0);
            }
        }
        const Repair plan =
            short_frame ? plan_repair_locked(hwnd, *s, s->staging_w, s->staging_h)
                        : Repair::None;
        if (plan == Repair::Repaint) {
            ::RedrawWindow(hwnd, nullptr, nullptr,
                           RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            LSW_AGENT_INFO("wgc: last frame for hwnd="
                           + std::to_string(reinterpret_cast<std::uintptr_t>(hwnd))
                           + " is missing an edge and none is coming; asked the "
                             "window to repaint");
        }
        finalize(Outcome::Cached);
        if (plan == Repair::Rebuild) {
            // Everything that touches the session is done; drop it, and
            // the next tick builds a fresh capture for this window.
            LSW_AGENT_INFO("wgc: hwnd="
                           + std::to_string(reinterpret_cast<std::uintptr_t>(hwnd))
                           + " still missing an edge after a repaint; rebuilding "
                             "its capture session");
            if (s->shmem_slot >= 0) {
                transport::shmem().free_slot(s->shmem_slot);
            }
            g_sessions.erase(hwnd);
        }
        return true;
    }

    // Resize: recreate the pool AND free + reallocate the shmem slot
    // so the buffer half fits the new dimensions. The next tick will
    // commit at the new size.
    SizeInt32 cs{};
    if (SUCCEEDED(frame->get_ContentSize(&cs))
        && (cs.Width != s->pool_size.Width || cs.Height != s->pool_size.Height)
        && cs.Width > 0 && cs.Height > 0) {
        if (SUCCEEDED(s->pool->Recreate(
                g_rt_device.Get(),
                WGD::DirectXPixelFormat_B8G8R8A8UIntNormalized, 2, cs))) {
            s->pool_size = cs;
        }
        if (s->shmem_slot >= 0) {
            transport::shmem().free_slot(s->shmem_slot);
            s->shmem_slot = -1;
        }
        const auto info = transport::shmem().alloc_slot(
            reinterpret_cast<std::uint64_t>(hwnd),
            static_cast<std::uint32_t>(cs.Width),
            static_cast<std::uint32_t>(cs.Height));
        s->shmem_slot = info.index;
        // A new size is a new chance to paint a whole frame — and a
        // resize is exactly when the half-painted ones turn up.
        s->repairs = 0;
        ++s->recreates;
        frame.Reset();
        finalize(Outcome::Cached);
        return true;
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
        finalize(Outcome::Error);
        return true;
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
            finalize(Outcome::Error);
            return true;
        }
        s->staging_w = desc.Width;
        s->staging_h = desc.Height;
    }

    const LONGLONG t_copy_start = qpc_now();
    g_ctx->CopyResource(s->staging.Get(), tex.Get());
    const std::uint64_t t_copy_us = qpc_to_us(qpc_now() - t_copy_start);

    const LONGLONG t_map_start = qpc_now();
    D3D11_MAPPED_SUBRESOURCE map{};
    HRESULT hr_map = g_ctx->Map(s->staging.Get(), 0, D3D11_MAP_READ, 0, &map);
    const std::uint64_t t_map_us = qpc_to_us(qpc_now() - t_map_start);
    if (FAILED(hr_map)) {
        finalize(Outcome::Error);
        return true;
    }
    bump_stage(s->t_copy_sum_us, s->t_copy_max_us, t_copy_us);
    bump_stage(s->t_map_sum_us,  s->t_map_max_us,  t_map_us);

    // A whole frame clears the repair budget: this window paints fine,
    // so spend it again freely if it ever stops. Frames that come up
    // short are still committed as they are — a thin hole beats a stale
    // picture — and the idle path above deals with them if they turn out
    // to be the last word.
    const auto* mapped = static_cast<const std::uint8_t*>(map.pData);
    if (!frame_is_incomplete(hwnd, mapped, map.RowPitch, desc.Width, desc.Height)) {
        s->repairs = 0;
    }

    // Lazy slot allocation. ensure_session_locked may have created the
    // session before transport::shmem was open; alloc now if needed.
    if (s->shmem_slot < 0) {
        const auto info = transport::shmem().alloc_slot(
            reinterpret_cast<std::uint64_t>(hwnd), desc.Width, desc.Height);
        s->shmem_slot = info.index;
    }

    bool committed = false;
    if (s->shmem_slot >= 0) {
        const LONGLONG t_write_start = qpc_now();
        committed = transport::shmem().commit_frame(
            s->shmem_slot,
            static_cast<const std::uint8_t*>(map.pData),
            map.RowPitch, desc.Width, desc.Height,
            static_cast<std::uint64_t>(qpc_now()),
            visible_insets_for(hwnd, desc.Width, desc.Height));
        const std::uint64_t t_write_us = qpc_to_us(qpc_now() - t_write_start);
        bump_stage(s->t_write_sum_us, s->t_write_max_us, t_write_us);
    }

    g_ctx->Unmap(s->staging.Get(), 0);
    frame.Reset();
    finalize(committed ? Outcome::Delivered : Outcome::Error);
    return true;
}

}  // namespace

bool Wgc::available() {
    std::lock_guard<std::mutex> lock(g_mu);
    return init_locked() && transport::shmem().open();
}

bool Wgc::tick_capture(std::uintptr_t hwnd) {
    return grab(reinterpret_cast<HWND>(hwnd));
}

void Wgc::release(std::uintptr_t /*hwnd*/) {
    // Intentionally a no-op. unsubscribe_window fires constantly as
    // the watcher churns popups (50 ms poll); destroying the WGC
    // session here and recreating it moments later crashed the agent.
    // Sessions are instead reaped lazily in ensure_session_locked /
    // grab once the window is genuinely gone (IsWindow == false).
}

std::string Wgc::debug_json(std::uintptr_t /*hwnd*/) {
    // The alpha-histogram diagnostic path is gone with the JPEG
    // encode path. Kept as a stub so the RPC handler still links;
    // callers should fold any future diagnostics into the per-stage
    // stats line emitted by ``maybe_emit_stats_locked``.
    return "{\"note\":\"debug_json retired; see wgc[hwnd=...] log lines\"}";
}

}  // namespace lsw::agent::capture
