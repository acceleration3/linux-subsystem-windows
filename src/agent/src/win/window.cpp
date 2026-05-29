#include "lsw/agent/win/window.hpp"

#include "lsw/agent/win/gdi.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/capture/wgc.hpp"

// clang-format off
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <psapi.h>
#include <dwmapi.h>
// clang-format on

#include <cstring>
#include <vector>

namespace lsw::agent::win {

namespace {

constexpr UINT kPwRenderFullContent = 0x00000002U;  // PW_RENDERFULLCONTENT (Win8.1+)


std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string exe_path_for(DWORD pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return {};
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD n = MAX_PATH;
    std::string out;
    if (::QueryFullProcessImageNameW(h, 0, buf, &n) != 0) {
        out = utf16_to_utf8(std::wstring(buf, n));
    }
    ::CloseHandle(h);
    return out;
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<DiscoveredWindow>*>(lparam);
    if (!::IsWindowVisible(hwnd)) {
        return TRUE;
    }
    // DWM-cloaked windows are "visible" per IsWindowVisible but not
    // actually shown — UWP/packaged apps keep many such ghost
    // top-levels. Capturing them spawns dead black mirrors.
    int cloaked = 0;
    if (::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))
            == S_OK
        && cloaked != 0) {
        return TRUE;
    }
    // NOTE: we deliberately do NOT filter by title or WS_EX_TOOLWINDOW
    // anymore. Menus (class "#32768"), combo-box drop-downs
    // ("ComboLbox") and modern WinUI flyouts are title-less and often
    // tool windows — the old filter dropped every one of them, which
    // is exactly why menus weren't captured. The AppWindowWatcher
    // scopes mirrors to the launched app's PID, so permissive
    // top-level enumeration here only ever surfaces that app's own
    // popups, not unrelated system chrome.
    wchar_t cls[64] = {};
    ::GetClassNameW(hwnd, cls, 64);
    if (::lstrcmpiW(cls, L"tooltips_class32") == 0) {
        return TRUE;  // pure tooltips: pure flicker, no value mirroring
    }

    int title_len = ::GetWindowTextLengthW(hwnd);
    std::wstring title;
    if (title_len > 0) {
        title.resize(static_cast<size_t>(title_len));
        ::GetWindowTextW(hwnd, title.data(), title_len + 1);
    }

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    const std::string exe = exe_path_for(pid);
    // Skip our own agent process so we don't capture ourselves
    // (mostly relevant when running --console rather than as a
    // service, but cheap to filter unconditionally).
    {
        wchar_t self[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, self, MAX_PATH);
        if (utf16_to_utf8(self) == exe) {
            return TRUE;
        }
    }

    // FULL-WINDOW model: the mirror shows the whole window (title bar,
    // menu bar, borders) — not just the client area — so classic
    // menu-bar / system menus, which open in the NON-client region,
    // line up. The reference rect is the DWM extended frame bounds
    // (the visible window; excludes only the external drop shadow),
    // falling back to GetWindowRect. Origin + size both come from it
    // so enumeration, capture, input and placement all agree.
    RECT fr{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr,
                                       sizeof(fr)))
        || fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        ::GetWindowRect(hwnd, &fr);
    }
    if (fr.right - fr.left <= 0 || fr.bottom - fr.top <= 0) {
        return TRUE;  // zero-area placeholder/ghost top-level
    }
    DiscoveredWindow w;
    w.hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    // GA_ROOTOWNER walks BOTH the parent and owner chains to the
    // top-level "owned root" — which is what we need to associate a
    // popup with the right tracked window. GW_OWNER alone returns the
    // direct owner, which for things like ComboLBox dropdowns is the
    // combobox CHILD control (not a top-level we mirror) → the popup
    // mirror would then have no tracked owner and fall through to
    // the main-window path (decorated frame + compositor-chosen
    // position). GA_ROOTOWNER returns the actual top-level instead.
    // For a window with no owner it returns the window itself; treat
    // that as "no owner".
    HWND root = ::GetAncestor(hwnd, GA_ROOTOWNER);
    w.owner = (root != nullptr && root != hwnd)
                  ? reinterpret_cast<std::uintptr_t>(root)
                  : 0;
    w.pid = static_cast<std::uint32_t>(pid);
    w.title = utf16_to_utf8(title);
    w.exe_path = exe;
    w.client_w = fr.right - fr.left;   // now the WINDOW size
    w.client_h = fr.bottom - fr.top;
    w.screen_x = fr.left;              // now the WINDOW origin
    w.screen_y = fr.top;
    out->push_back(std::move(w));
    return TRUE;
}

// Encode a GDI+ bitmap to PNG bytes (shared by window + cursor
// capture). Empty on failure.
std::vector<std::uint8_t> encode_png(Gdiplus::Bitmap& bitmap) {
    CLSID clsid;
    UINT n = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (size == 0) {
        return {};
    }
    auto* info =
        static_cast<Gdiplus::ImageCodecInfo*>(::HeapAlloc(::GetProcessHeap(), 0, size));
    Gdiplus::GetImageEncoders(n, size, info);
    bool found = false;
    for (UINT i = 0; i < n; ++i) {
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
    bitmap.Save(stream, &clsid, nullptr);
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

}  // namespace

bool Window::focus_window(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return false;
    }
    if (::GetForegroundWindow() == hwnd) {
        return true;
    }
    if (::IsIconic(hwnd) != FALSE) {
        ::ShowWindow(hwnd, SW_RESTORE);
    }
    ::AllowSetForegroundWindow(ASFW_ANY);
    return ::SetForegroundWindow(hwnd) != FALSE;
}

bool Window::unfocus_window(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return false;
    }
    // Only act if THIS window is the current foreground; otherwise
    // someone else already has focus and we'd be stomping it.
    if (::GetForegroundWindow() != hwnd) {
        return true;
    }
    HWND shell = ::GetShellWindow();
    if (shell == nullptr) {
        return false;
    }
    ::AllowSetForegroundWindow(ASFW_ANY);
    return ::SetForegroundWindow(shell) != FALSE;
}

int Window::hit_test_nc(std::uintptr_t hwnd_raw, int x, int y) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return -1;  // HTERROR
    }
    // ``x, y`` are EFB-relative captured-frame px (same origin the
    // input-inject path uses). WM_NCHITTEST wants screen coords in
    // ``lParam`` packed as MAKELPARAM(screen_x, screen_y).
    RECT efb{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &efb,
                                       sizeof(efb)))) {
        ::GetWindowRect(hwnd, &efb);
    }
    const int sx = efb.left + x;
    const int sy = efb.top + y;
    const LRESULT r = ::SendMessageW(
        hwnd, WM_NCHITTEST, 0,
        MAKELPARAM(static_cast<WORD>(sx), static_cast<WORD>(sy)));
    return static_cast<int>(r);
}

bool Window::close_window(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        LSW_AGENT_WARN(std::string{"window_capture: close_window invalid hwnd="}
                       + std::to_string(hwnd_raw));
        return false;
    }
    // PostMessage (not SendMessage) so we don't block on the target
    // window's message loop — the app processes WM_CLOSE on its own
    // schedule, possibly putting up a save-changes dialog.
    const bool ok = ::PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE;
    LSW_AGENT_INFO(std::string{"window_capture: close_window hwnd="} + std::to_string(hwnd_raw)
                   + (ok ? " posted" : " post FAILED"));
    return ok;
}

bool Window::resize_client(std::uintptr_t hwnd_raw, int client_w,
                                   int client_h) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE || client_w <= 0
        || client_h <= 0) {
        return false;
    }
    // FULL-WINDOW model: the host widget IS the whole window, so the
    // requested size is the window size — set it directly (no
    // client→window inflation). The captured DWM extended-frame size
    // differs from GetWindowRect by only the invisible border, which
    // is immaterial here.
    //
    // Clamp to the work area of the monitor the window currently sits
    // on (so it never grows past the guest's desktop / under the
    // taskbar) and recenter on the same work area each time — the
    // user wants the guest window to stay centred on its screen as
    // the mirror resizes, never escaping the screen margins.
    HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    if (mon != nullptr && ::GetMonitorInfoW(mon, &mi) != 0) {
        work = mi.rcWork;
    } else {
        work = {0, 0, ::GetSystemMetrics(SM_CXSCREEN),
                ::GetSystemMetrics(SM_CYSCREEN)};
    }
    const int work_w = work.right - work.left;
    const int work_h = work.bottom - work.top;
    if (client_w > work_w) client_w = work_w;
    if (client_h > work_h) client_h = work_h;
    const int x = work.left + (work_w - client_w) / 2;
    const int y = work.top + (work_h - client_h) / 2;
    const BOOL ok = ::SetWindowPos(hwnd, nullptr, x, y, client_w, client_h,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
    LSW_AGENT_INFO(std::string{"window_capture: resize hwnd="}
                   + std::to_string(hwnd_raw) + " -> "
                   + std::to_string(client_w) + "x" + std::to_string(client_h)
                   + " @ " + std::to_string(x) + "," + std::to_string(y)
                   + (ok ? " ok" : " FAILED"));
    return ok != FALSE;
}

Window::WindowProps Window::query_props(std::uintptr_t hwnd_raw) {
    WindowProps p{};
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return p;
    }
    RECT fr{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr,
                                       sizeof(fr)))) {
        ::GetWindowRect(hwnd, &fr);
    }
    p.x = fr.left;
    p.y = fr.top;
    p.w = fr.right - fr.left;
    p.h = fr.bottom - fr.top;
    const auto style =
        static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
    p.resizable = (style & WS_THICKFRAME) != 0;
    p.minimizable = (style & WS_MINIMIZEBOX) != 0;
    p.maximizable = (style & WS_MAXIMIZEBOX) != 0;
    p.closable = (style & WS_SYSMENU) != 0;
    if (p.closable) {
        if (HMENU sm = ::GetSystemMenu(hwnd, FALSE); sm != nullptr) {
            const UINT st = ::GetMenuState(sm, SC_CLOSE, MF_BYCOMMAND);
            if (st != static_cast<UINT>(-1)
                && (st & (MF_DISABLED | MF_GRAYED)) != 0) {
                p.closable = false;
            }
        }
    }
    // WM_GETMINMAXINFO: ask the app what its min/max track sizes are.
    // Initialise with system defaults so unanswered fields fall back
    // sanely. Send with a small timeout so a wedged app can't hang us.
    MINMAXINFO mmi{};
    mmi.ptMaxSize.x = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    mmi.ptMaxSize.y = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    mmi.ptMaxTrackSize.x = mmi.ptMaxSize.x;
    mmi.ptMaxTrackSize.y = mmi.ptMaxSize.y;
    mmi.ptMinTrackSize.x = ::GetSystemMetrics(SM_CXMINTRACK);
    mmi.ptMinTrackSize.y = ::GetSystemMetrics(SM_CYMINTRACK);
    DWORD_PTR res = 0;
    ::SendMessageTimeoutW(hwnd, WM_GETMINMAXINFO, 0,
                          reinterpret_cast<LPARAM>(&mmi),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &res);
    // The min/max above are WINDOW (track) sizes, which is what we
    // need since the host mirror is the full window now.
    p.min_w = mmi.ptMinTrackSize.x;
    p.min_h = mmi.ptMinTrackSize.y;
    p.max_w = mmi.ptMaxTrackSize.x;
    p.max_h = mmi.ptMaxTrackSize.y;
    // 0 sentinel = "no app-imposed limit" — leave them be; the manager
    // applies setMinimum/MaximumSize only when meaningful.
    if (!p.resizable) {
        // Non-resizable windows: min == max == current size, no point
        // exposing the system defaults as a usable range.
        p.min_w = p.max_w = p.w;
        p.min_h = p.max_h = p.h;
    }
    // The HWND's monitor — host clamps resize_window against this so
    // the guest never grows past its own screen (letterboxing kicks in
    // on the host when it does). Falls back to the virtual desktop
    // metrics if no monitor handle is available.
    HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon != nullptr && ::GetMonitorInfoW(mon, &mi) != 0) {
        p.screen_w = mi.rcMonitor.right - mi.rcMonitor.left;
        p.screen_h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    } else {
        p.screen_w = ::GetSystemMetrics(SM_CXSCREEN);
        p.screen_h = ::GetSystemMetrics(SM_CYSCREEN);
    }

    // (Title-bar / button rect collection moved out of ``query_props``
    // in Phase 7: the ``window_props`` RPC handler now layers UIA's
    // ``Uia::get_title_bar`` on top of these size/style fields. UIA
    // works for both classic DefWindowProc-driven chrome and modern
    // DWM-drawn caption buttons that the wndproc-owned
    // ``WM_GETTITLEBARINFOEX`` couldn't see consistently.)

    p.valid = true;
    return p;
}

std::vector<DiscoveredWindow> Window::enumerate() {
    // This is the in-session implementation. When called from the
    // service process (session 0) it sees nothing because top-level
    // windows live on the user session's desktop. The host_endpoint
    // forwards list_windows to the user-session GuiHelper which
    // ends up calling this function; in the helper's process, we ARE
    // the user session, so EnumWindows works natively.
    std::vector<DiscoveredWindow> out;
    ::EnumWindows(&enum_proc, reinterpret_cast<LPARAM>(&out));
    return out;  // no per-call log: enumerate runs at the watcher poll
                 // rate (now ~50 Hz) — logging it floods the ring.
}

std::string Window::capture_debug_json(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return "{\"err\":\"bad hwnd\"}";
    }
    RECT fr{};
    if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr,
                                       sizeof(fr)))) {
        ::GetWindowRect(hwnd, &fr);
    }
    const int w = fr.right - fr.left;
    const int h = fr.bottom - fr.top;
    if (w <= 0 || h <= 0) {
        return "{\"err\":\"empty\"}";
    }
    HDC screen = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp =
        ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    std::string out = "{\"err\":\"dib\"}";
    if (bmp != nullptr && bits != nullptr) {
        const size_t n = static_cast<size_t>(w) * h * 4;
        std::memset(bits, 0, n);
        HBITMAP old = static_cast<HBITMAP>(::SelectObject(mem, bmp));
        const BOOL pw =
            ::PrintWindow(hwnd, mem, kPwRenderFullContent);  // full window
        ::SelectObject(mem, old);
        auto* px = static_cast<std::uint8_t*>(bits);
        std::uint64_t a0 = 0;
        std::uint64_t a255 = 0;
        std::uint64_t amid = 0;
        std::uint64_t rgb_nonzero_at_a0 = 0;
        for (size_t i = 0; i < n; i += 4) {
            const std::uint8_t a = px[i + 3];
            if (a == 0) {
                ++a0;
                if (px[i] || px[i + 1] || px[i + 2]) ++rgb_nonzero_at_a0;
            } else if (a == 255) {
                ++a255;
            } else {
                ++amid;
            }
        }
        auto corner = [&](int x, int y) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            return std::to_string(px[i + 2]) + "," + std::to_string(px[i + 1])
                   + "," + std::to_string(px[i]) + "," + std::to_string(px[i + 3]);
        };
        out = std::string{"{\"pw\":"} + (pw ? "true" : "false")
              + ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(h)
              + ",\"a0\":" + std::to_string(a0)
              + ",\"a255\":" + std::to_string(a255)
              + ",\"amid\":" + std::to_string(amid)
              + ",\"rgb_nonzero_at_a0\":" + std::to_string(rgb_nonzero_at_a0)
              + ",\"tl\":\"" + corner(0, 0) + "\""
              + ",\"tr\":\"" + corner(w - 1, 0) + "\""
              + ",\"ctr\":\"" + corner(w / 2, h / 2) + "\"}";
    }
    if (bmp != nullptr) ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return out;
}

bool Window::tick_capture(std::uintptr_t hwnd_raw) {
    auto* hwnd = reinterpret_cast<HWND>(hwnd_raw);
    if (hwnd == nullptr || ::IsWindow(hwnd) == FALSE) {
        return false;
    }
    if (!capture::Wgc::available()) {
        // IVSHMEM region not yet mapped (driver missing on a fresh
        // VM, or post-install setup hasn't completed). Keep the
        // subscription alive; available() will start returning true
        // once the manager region is in place and the agent's
        // shmem.open() succeeds.
        return true;
    }
    return capture::Wgc::tick_capture(hwnd_raw);
}

}  // namespace lsw::agent::win
