#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lsw::suites::sanity::lsw {
class InputWindow {
public:
    InputWindow() = default;
    InputWindow(const InputWindow&) = delete;
    InputWindow& operator=(const InputWindow&) = delete;
    ~InputWindow();

    [[nodiscard]] bool open(const std::wstring& title, int x, int y, int width, int height, std::string& error);

    [[nodiscard]] HWND hwnd() const { return hwnd_; }

    [[nodiscard]] int chars() const { return chars_.load(); }

    [[nodiscard]] int focus_gains() const { return focus_.load(); }

    [[nodiscard]] std::vector<POINT> clicks();

    void reset();

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void pump(std::wstring title, int x, int y, int width, int height);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
    HWND hwnd_ = nullptr;
    std::atomic<int> chars_{0};
    std::atomic<int> focus_{0};
    std::vector<POINT> clicks_;
};
}  // namespace lsw::suites::sanity::lsw
