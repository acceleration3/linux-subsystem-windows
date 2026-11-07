#include "sanity/lsw/input_window.hpp"

#include <chrono>

namespace lsw::suites::sanity::lsw {

LRESULT CALLBACK InputWindow::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<InputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (self != nullptr) {
        if (message == WM_CHAR) {
            self->chars_.fetch_add(1);
        } else if (message == WM_SETFOCUS) {
            self->focus_.fetch_add(1);
        } else if (message == WM_LBUTTONDOWN) {
            std::lock_guard lock(self->mutex_);
            self->clicks_.push_back({static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))});
        }
    }
    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

void InputWindow::pump(std::wstring title, int x, int y, int width, int height) {
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.lpszClassName = L"lsw-suites-input-window";
    cls.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    ::RegisterClassW(&cls);
    RECT outer{0, 0, width, height};
    ::AdjustWindowRect(&outer, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = ::CreateWindowExW(0, cls.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, cls.hInstance, this);
    {
        std::lock_guard lock(mutex_);
        hwnd_ = window;
        ready_ = true;
    }
    ready_cv_.notify_all();
    MSG message;
    while (window != nullptr && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool InputWindow::open(const std::wstring& title, int x, int y, int width, int height, std::string& error) {
    thread_ = std::thread([=, this] { pump(title, x, y, width, height); });
    std::unique_lock lock(mutex_);
    ready_cv_.wait_for(lock, std::chrono::seconds(10), [this] { return ready_; });
    if (hwnd_ == nullptr) {
        error = "creating the input window failed";
        return false;
    }
    return true;
}

std::vector<POINT> InputWindow::clicks() {
    std::lock_guard lock(mutex_);
    return clicks_;
}

void InputWindow::reset() {
    chars_ = 0;
    focus_ = 0;
    std::lock_guard lock(mutex_);
    clicks_.clear();
}

InputWindow::~InputWindow() {
    if (hwnd_ != nullptr) {
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace lsw::suites::sanity::lsw
