#include "support/test_window.hpp"

namespace lsw::suites {

namespace {

constexpr const wchar_t* kClassName = L"lsw_suites_test_window";

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
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
    HWND hwnd = ::CreateWindowExW(0, kClassName, config.title.c_str(),
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                  config.x, config.y, config.width, config.height, nullptr, nullptr,
                                  ::GetModuleHandleW(nullptr), nullptr);
    {
        std::lock_guard lock(mutex_);
        if (hwnd == nullptr) {
            error_ = "CreateWindowEx failed (error " + std::to_string(::GetLastError()) + ")";
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
