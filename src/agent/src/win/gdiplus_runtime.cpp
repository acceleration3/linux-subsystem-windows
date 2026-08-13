#include "lsw/agent/win/gdiplus_runtime.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <mutex>

namespace lsw::agent::win {

namespace {

// GDI+ hands back a token at startup that GdiplusShutdown would need.
// We never shut down — GDI+ lives as long as the agent process — so the
// token is kept only because the API insists on an out-parameter.
ULONG_PTR g_gdiplus_token = 0;

}  // namespace

void GdiPlusRuntime::ensure_started() {
    static std::once_flag once;
    std::call_once(once, [] {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdiplus_token, &in, nullptr);
    });
}

}  // namespace lsw::agent::win
