#include "support/test_window.hpp"

// clang-format off
#include <shellscalingapi.h>
// clang-format on

namespace lsw::suites {

namespace {

constexpr const wchar_t* kClassName = L"lsw_suites_test_window";

bool quiet_desktop() {
    static const bool quiet = ::GetEnvironmentVariableW(L"LSW_SUITES_QUIET_DESKTOP", nullptr, 0) != 0;
    return quiet;
}

POINT parking_origin() {
    return {::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64,
            ::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64};
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_WINDOWPOSCHANGING && quiet_desktop()) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
        position->flags |= SWP_NOZORDER;
        if ((position->flags & SWP_NOMOVE) == 0) {
            const POINT origin = parking_origin();
            position->x = origin.x;
            position->y = origin.y;
        }
    }
    if (message == WM_GETMINMAXINFO) {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {1, 1};
        return 0;
    }
    if (message == WM_PAINT) {
        const LONG_PTR paint = ::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (paint != 0) {
            PAINTSTRUCT ps;
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT client{};
            ::GetClientRect(hwnd, &client);
            HBRUSH brush = ::CreateSolidBrush(static_cast<COLORREF>(paint - 1));
            ::FillRect(dc, &client, brush);
            ::DeleteObject(brush);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
    }
    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

void square_corners(HWND hwnd) {
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) {
        return;
    }
    auto set = reinterpret_cast<SetAttribute>(reinterpret_cast<void*>(::GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (set != nullptr) {
        const DWORD preference = 1;
        set(hwnd, 33, &preference, sizeof(preference));
    }
}

void register_class() {
    static const ATOM atom = [] {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.style = CS_OWNDC;
        cls.lpfnWndProc = window_proc;
        cls.hInstance = ::GetModuleHandleW(nullptr);
        cls.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        cls.lpszClassName = kClassName;
        return ::RegisterClassExW(&cls);
    }();
    (void)atom;
}

}  // namespace

void TestWindow::outer_size(int x, int y, int client_width, int client_height, int& width, int& height) {
    RECT rect{0, 0, client_width, client_height};
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    auto adjust = user32 != nullptr ? reinterpret_cast<AdjustForDpi>(reinterpret_cast<void*>(::GetProcAddress(user32, "AdjustWindowRectExForDpi"))) : nullptr;
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    const POINT point{x, y};
    const HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
        dpi_x = 96;
    }
    if (adjust == nullptr || adjust(&rect, style, FALSE, 0, dpi_x) == FALSE) {
        rect = {0, 0, client_width, client_height};
        ::AdjustWindowRectEx(&rect, style, FALSE, 0);
    }
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

bool TestWindow::create(const TestWindowConfig& config, std::string& error) {
    destroy();
    ready_ = false;
    error_.clear();
    thread_ = std::thread([this, config] { pump(config); });
    std::unique_lock lock(mutex_);
    ready_cv_.wait(lock, [this] { return ready_; });
    if (hwnd_ == nullptr) {
        error = error_;
        lock.unlock();
        thread_.join();
        return false;
    }
    return true;
}

void TestWindow::pump(TestWindowConfig config) {
    register_class();
    if (quiet_desktop()) {
        const POINT origin = parking_origin();
        config.x = origin.x;
        config.y = origin.y;
        config.topmost = false;
    }
    DWORD ex_style = 0;
    if (config.layered) {
        ex_style |= WS_EX_LAYERED;
    }
    if (config.topmost) {
        ex_style |= WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    }
    HWND hwnd = ::CreateWindowExW(ex_style, kClassName, config.title.c_str(),
                                  (config.popup ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  config.x, config.y, config.width, config.height, nullptr, nullptr,
                                  ::GetModuleHandleW(nullptr), nullptr);
    std::string size_error;
    if (hwnd != nullptr) {
        square_corners(hwnd);
        int frame_width = 0;
        int frame_height = 0;
        if (!config.popup) {
            outer_size(config.x, config.y, 0, 0, frame_width, frame_height);
        }
        RECT client{};
        ::GetClientRect(hwnd, &client);
        const int want_width = config.width - frame_width;
        const int want_height = config.height - frame_height;
        if (client.right != want_width || client.bottom != want_height) {
            size_error = "window client area is " + std::to_string(client.right) + "x" + std::to_string(client.bottom) +
                         ", the requested client area is " + std::to_string(want_width) + "x" + std::to_string(want_height);
            ::DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }
    {
        std::lock_guard lock(mutex_);
        if (hwnd == nullptr) {
            error_ = size_error.empty() ? "CreateWindowEx failed (error " + std::to_string(::GetLastError()) + ")" : size_error;
        } else {
            hwnd_ = hwnd;
            dc_ = ::GetDC(hwnd);
            if (config.visible) {
                ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                ::UpdateWindow(hwnd);
            }
        }
        ready_ = true;
    }
    ready_cv_.notify_all();
    if (hwnd == nullptr) {
        return;
    }
    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

void TestWindow::paint_with(COLORREF color) {
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, static_cast<LONG_PTR>(color) + 1);
}

void TestWindow::show() {
    ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void TestWindow::hide() {
    ::ShowWindow(hwnd_, SW_HIDE);
}

void TestWindow::resize(int width, int height) {
    ::SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void TestWindow::move(int x, int y) {
    ::SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int TestWindow::width() const {
    RECT rect{};
    ::GetClientRect(hwnd_, &rect);
    return rect.right - rect.left;
}

int TestWindow::height() const {
    RECT rect{};
    ::GetClientRect(hwnd_, &rect);
    return rect.bottom - rect.top;
}

void TestWindow::destroy() {
    if (!thread_.joinable()) {
        return;
    }
    if (hwnd_ != nullptr) {
        ::SendMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    thread_.join();
    hwnd_ = nullptr;
    dc_ = nullptr;
}

}  // namespace lsw::suites
