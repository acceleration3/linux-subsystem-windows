// mingw-w64 bug: `boolean` and `BYTE` are both `unsigned char`, so
// windows.foundation.h's IReference<boolean> and IReference<BYTE>
// instantiate the SAME C++ type → "redefinition of IReference<unsigned
// char>". Pre-defining the boolean block's include guard skips it;
// WGC capture doesn't use IReference<boolean>.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__

#include "lsw/agent/capture/wgc.hpp"

#include "lsw/agent/capture/vulkan_session.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/shmem.hpp"

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

struct Session {
    ComPtr<WGC::IGraphicsCaptureItem> item;
    ComPtr<WGC::IDirect3D11CaptureFramePool> pool;
    ComPtr<WGC::IGraphicsCaptureSession> session;
    ComPtr<ID3D11Texture2D> staging;
    UINT staging_w = 0;
    UINT staging_h = 0;
    SizeInt32 pool_size{0, 0};  // size the frame pool was (re)created at
    int shmem_slot = -1;         // index in transport::shmem allocator

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
        finalize(Outcome::Cached);
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
            static_cast<std::uint64_t>(qpc_now()));
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
    // Never while an application owns the region.
    //
    // ``open`` is not a query — it maps the BAR — so asking whether
    // capture is available would TAKE the mapping back from a running
    // Vulkan application, or fail against the driver every tick trying.
    // Neither is capture's decision to make.
    if (VulkanSession::active()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    return init_locked() && transport::shmem().open();
}

void Wgc::drop_all_sessions() {
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto& [hwnd, session] : g_sessions) {
        if (session.shmem_slot >= 0) {
            transport::shmem().free_slot(session.shmem_slot);
        }
    }
    const auto n = g_sessions.size();
    g_sessions.clear();
    if (n != 0) {
        LSW_AGENT_INFO(std::string{"wgc: dropped "} + std::to_string(n)
                       + " capture session(s); the region is going to an application");
    }
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
