#pragma once

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace lsw::suites {
struct TestWindowConfig {
    int x = 40;
    int y = 40;
    int width = 320;
    int height = 240;
    bool visible = true;
    std::wstring title = L"lsw-suites";
};

class TestWindow {
public:
    TestWindow() = default;
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;
    ~TestWindow() { destroy(); }

    [[nodiscard]] bool create(const TestWindowConfig& config, std::string& error);

    void show();

    void hide();

    void resize(int width, int height);

    void move(int x, int y);

    void destroy();

    [[nodiscard]] HWND hwnd() const { return hwnd_; }

    [[nodiscard]] HDC dc() const { return dc_; }

    [[nodiscard]] int width() const;

    [[nodiscard]] int height() const;

    static void outer_size(int x, int y, int client_width, int client_height, int& width, int& height);

private:
    void pump(TestWindowConfig config);

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
    std::string error_;
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
};
}  // namespace lsw::suites
