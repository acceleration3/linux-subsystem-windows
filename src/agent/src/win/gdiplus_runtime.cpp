#include "lsw/agent/win/gdiplus_runtime.hpp"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <mutex>

namespace lsw::agent::win {

namespace {
ULONG_PTR g_gdiplus_token = 0;

}

void GdiPlusRuntime::ensure_started() {
    static std::once_flag once;
    std::call_once(once, [] {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdiplus_token, &in, nullptr);
    });
}

}
