#include "sanity/lsw/lsw_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/capture/wgc_checks.hpp"
#include "sanity/env/install_checks.hpp"
#include "sanity/iso/fleet.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/iso/scene.hpp"
#include "sanity/lsw/input_window.hpp"
#include "support/child_runs.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/test_window.hpp"

#include <dwmapi.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace lsw::suites::sanity::lsw {

namespace {

using Clock = std::chrono::steady_clock;

Outcome app_role(Context&, const std::string& role) {
    if (role != "window-app") {
        return Outcome::fail("unknown role " + role);
    }
    TestWindow window;
    TestWindowConfig config;
    config.title = Text::wide(ChildMode::variable(L"LSW_SUITES_APP_TITLE"));
    config.x = 120;
    config.y = 120;
    config.width = 400;
    config.height = 300;
    std::string error;
    if (!window.create(config, error)) {
        return Outcome::fail(error);
    }
    const auto end = Clock::now() + std::chrono::milliseconds(static_cast<long long>(iso::Loads::seconds() * 1000));
    while (::IsWindow(window.hwnd()) != FALSE && Clock::now() < end) {
        ::Sleep(50);
    }
    return Outcome::ok();
}

struct Found {
    HWND window = nullptr;
    DWORD pid = 0;
};

struct Search {
    const std::wstring* title;
    Found found;
};

BOOL CALLBACK match_window(HWND window, LPARAM param) {
    auto* state = reinterpret_cast<Search*>(param);
    wchar_t text[256] = {};
    ::GetWindowTextW(window, text, 255);
    if (::IsWindowVisible(window) != FALSE && *state->title == text) {
        state->found.window = window;
        ::GetWindowThreadProcessId(window, &state->found.pid);
        return FALSE;
    }
    return TRUE;
}

Found find_window(const std::wstring& title, int timeout_ms) {
    Search search{&title, {}};
    const auto end = Clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        ::EnumWindows(match_window, reinterpret_cast<LPARAM>(&search));
        if (search.found.window != nullptr) {
            return search.found;
        }
        ::Sleep(100);
    } while (Clock::now() < end);
    return {};
}

RECT bounds(HWND window) {
    RECT rect{};
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        ::GetWindowRect(window, &rect);
    }
    return rect;
}

RECT work_area(HWND window) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    ::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcWork;
}

bool close_to(const RECT& a, const RECT& b, int tolerance) {
    return std::abs(a.left - b.left) <= tolerance && std::abs(a.top - b.top) <= tolerance && std::abs(a.right - b.right) <= tolerance && std::abs(a.bottom - b.bottom) <= tolerance;
}

std::string text(const RECT& r) {
    return "(" + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom) + ")";
}

void key(WORD vk, bool up) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    ::SendInput(1, &input, sizeof(input));
}

void combo(WORD modifier, WORD vk) {
    key(modifier, false);
    key(vk, false);
    key(vk, true);
    key(modifier, true);
}

void mouse(int x, int y, DWORD flags) {
    const int width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>((static_cast<long long>(x - ::GetSystemMetrics(SM_XVIRTUALSCREEN)) * 65535 + width / 2) / (width - 1));
    input.mi.dy = static_cast<LONG>((static_cast<long long>(y - ::GetSystemMetrics(SM_YVIRTUALSCREEN)) * 65535 + height / 2) / (height - 1));
    input.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    ::SendInput(1, &input, sizeof(input));
}

void focus(HWND window) {
    ::keybd_event(VK_MENU, 0, 0, 0);
    ::SetForegroundWindow(window);
    ::keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    for (int i = 0; i < 40 && ::GetForegroundWindow() != window; ++i) {
        ::Sleep(25);
    }
}

struct App {
    iso::Fleet fleet;
    Found found;
    std::wstring title;

    bool start(Context& context, double seconds, std::string& error) {
        title = L"lsw-suites-app-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(::GetTickCount64());
        std::vector<std::pair<std::string, std::string>> env = iso::Scene::seconds(seconds);
        env.emplace_back("LSW_SUITES_APP_TITLE", Text::utf8(title));
        fleet.start(context.id(), "app", "window-app", env, static_cast<int>(seconds) + 60);
        found = find_window(title, 15000);
        if (found.window == nullptr) {
            error = "no visible window titled '" + Text::utf8(title) + "' appeared within 15 s";
            return false;
        }
        return true;
    }
};

}  // namespace

Outcome LswChecks::launched_app_appears(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    const auto begin = Clock::now();
    if (!app.start(context, 12, error)) {
        return Outcome::fail(error);
    }
    context.measure("appeared_ms", std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
    RECT client{};
    ::GetClientRect(app.found.window, &client);
    Problems problems;
    int frame_width = 0;
    int frame_height = 0;
    TestWindow::outer_size(120, 120, 0, 0, frame_width, frame_height);
    if (client.right != 400 - frame_width || client.bottom != 300 - frame_height) {
        problems.add("the window's client size is " + std::to_string(client.right) + "x" + std::to_string(client.bottom) + ", expected " + std::to_string(400 - frame_width) + "x" +
                     std::to_string(300 - frame_height) + " for an outer size of 400x300");
    }
    wchar_t title[256] = {};
    ::GetWindowTextW(app.found.window, title, 255);
    if (app.title != title) {
        problems.add("the window title differs");
    }
    return problems.outcome();
}

Outcome LswChecks::relaunching_app(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "lsw-suites-relaunch.cmd";
    const std::wstring title = L"lsw-suites-relaunch-" + std::to_wstring(::GetCurrentProcessId());
    {
        std::ofstream file(script, std::ios::trunc);
        file << "@echo off\r\nset LSW_SUITES_CHILD=1\r\nset LSW_SUITES_ROLE=window-app\r\nset LSW_SUITES_APP_TITLE=" << Text::utf8(title) << "\r\nset LSW_SUITES_ISO_SECONDS=10\r\n\""
             << ChildRuns::own_exe().string() << "\" --run-one " << context.id() << " --artifacts \"" << (std::filesystem::temp_directory_path() / "lsw-suites-relaunch-art").string() << "\"\r\n";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring line = L"explorer.exe \"" + script.wstring() + L"\"";
    if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) {
        return Context::skip("explorer.exe cannot be started");
    }
    ::CloseHandle(info.hProcess);
    ::CloseHandle(info.hThread);
    const Found found = find_window(title, 25000);
    std::error_code ignore;
    Problems problems;
    if (found.window == nullptr) {
        problems.add("a process started through explorer.exe never showed its window");
    } else {
        HANDLE owner = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, found.pid);
        if (owner == nullptr || ::WaitForSingleObject(owner, 0) == WAIT_OBJECT_0) {
            problems.add("the window's owner process is not running");
        }
        if (found.pid == info.dwProcessId) {
            problems.add("the window belongs to the process that was started, not to the relaunched one");
        }
        if (owner != nullptr) {
            ::CloseHandle(owner);
        }
        ::PostMessageW(found.window, WM_CLOSE, 0, 0);
    }
    std::filesystem::remove(script, ignore);
    return problems.outcome();
}

Outcome LswChecks::frames_equal_guest(Context& context) {
    return capture::WgcChecks::frames_in_order(context);
}

Outcome LswChecks::maximize_work_area(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    if (!app.start(context, 20, error)) {
        return Outcome::fail(error);
    }
    ::ShowWindow(app.found.window, SW_MAXIMIZE);
    ::Sleep(800);
    const RECT got = bounds(app.found.window);
    const RECT want = work_area(app.found.window);
    context.measure("bounds", text(got));
    context.measure("work_area", text(want));
    return close_to(got, want, 1) ? Outcome::ok() : Outcome::fail("the maximized window is at " + text(got) + ", the work area is " + text(want));
}

Outcome LswChecks::snap_restore(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    if (!app.start(context, 30, error)) {
        return Outcome::fail(error);
    }
    focus(app.found.window);
    const RECT original = bounds(app.found.window);
    const RECT work = work_area(app.found.window);
    combo(VK_LWIN, VK_LEFT);
    ::Sleep(1200);
    const RECT snapped = bounds(app.found.window);
    const RECT half{work.left, work.top, work.left + (work.right - work.left) / 2, work.bottom};
    Problems problems;
    if (!close_to(snapped, half, 2)) {
        problems.add("after Win+Left the window is at " + text(snapped) + ", the left half is " + text(half));
    }
    combo(VK_LWIN, VK_RIGHT);
    ::Sleep(1200);
    const RECT restored = bounds(app.found.window);
    if (!close_to(restored, original, 2)) {
        problems.add("after Win+Right the window is at " + text(restored) + ", it was at " + text(original));
    }
    return problems.outcome();
}

Outcome LswChecks::move_resize_guest_follows(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    if (!app.start(context, 30, error)) {
        return Outcome::fail(error);
    }
    focus(app.found.window);
    Problems problems;
    RECT before{};
    ::GetWindowRect(app.found.window, &before);
    const int grab_x = (before.left + before.right) / 2;
    const int grab_y = before.top + 14;
    mouse(grab_x, grab_y, 0);
    ::Sleep(150);
    mouse(grab_x, grab_y, MOUSEEVENTF_LEFTDOWN);
    ::Sleep(100);
    for (int step = 1; step <= 10; ++step) {
        mouse(grab_x + 12 * step, grab_y + 8 * step, 0);
        ::Sleep(30);
    }
    mouse(grab_x + 120, grab_y + 80, MOUSEEVENTF_LEFTUP);
    ::Sleep(400);
    RECT moved{};
    ::GetWindowRect(app.found.window, &moved);
    if (std::abs((moved.left - before.left) - 120) > 1 || std::abs((moved.top - before.top) - 80) > 1) {
        problems.add("dragging the title bar by 120,80 moved the window by " + std::to_string(moved.left - before.left) + "," + std::to_string(moved.top - before.top));
    }
    ::SetWindowPos(app.found.window, nullptr, 0, 0, 520, 380, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ::Sleep(200);
    RECT resized{};
    ::GetWindowRect(app.found.window, &resized);
    if (std::abs((resized.right - resized.left) - 520) > 1 || std::abs((resized.bottom - resized.top) - 380) > 1) {
        problems.add("resizing to 520x380 left the window at " + text(resized));
    }
    return problems.outcome();
}

Outcome LswChecks::keyboard_to_right_window(Context& context) {
    InputWindow first;
    InputWindow second;
    std::string error;
    if (!first.open(L"lsw-suites-input-a", 60, 60, 300, 200, error) || !second.open(L"lsw-suites-input-b", 420, 60, 300, 200, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int round = 0;
    for (InputWindow* target : {&first, &second, &first}) {
        focus(target->hwnd());
        const auto waited_from = Clock::now();
        while (Clock::now() - waited_from < std::chrono::seconds(2) && (::GetForegroundWindow() != target->hwnd() || target->focus_gains() == 0)) {
            ::Sleep(10);
        }
        const std::string label = "lsw.keyboard.round" + std::to_string(round++);
        context.measure(label + ".foreground_is_target", ::GetForegroundWindow() == target->hwnd());
        context.measure(label + ".focus_gains_seen", target->focus_gains());
        context.measure(label + ".focus_wait_ms", std::chrono::duration<double, std::milli>(Clock::now() - waited_from).count());
        first.reset();
        second.reset();
        for (WORD vk : {static_cast<WORD>('A'), static_cast<WORD>('B'), static_cast<WORD>('C')}) {
            key(vk, false);
            key(vk, true);
        }
        ::Sleep(300);
        InputWindow* other = target == &first ? &second : &first;
        if (target->chars() != 3 || other->chars() != 0) {
            problems.add("typing three keys with " + std::string(target == &first ? "the first" : "the second") + " window focused delivered " + std::to_string(target->chars()) + " to it and " + std::to_string(other->chars()) + " to the other");
        }
    }
    return problems.outcome();
}

Outcome LswChecks::mouse_position_mapping(Context& context) {
    SetProcessDPIAware();
    InputWindow window;
    std::string error;
    if (!window.open(L"lsw-suites-input-mouse", 100, 100, 400, 300, error)) {
        return Outcome::fail(error);
    }
    focus(window.hwnd());
    context.measure("dpi", static_cast<long long>(::GetDpiForWindow(window.hwnd())));
    Problems problems;
    POINT targets[] = {{5, 5}, {200, 150}, {395, 295}, {37, 211}, {333, 19}};
    for (const POINT& target : targets) {
        window.reset();
        POINT screen = target;
        ::ClientToScreen(window.hwnd(), &screen);
        mouse(screen.x, screen.y, 0);
        ::Sleep(50);
        mouse(screen.x, screen.y, MOUSEEVENTF_LEFTDOWN);
        mouse(screen.x, screen.y, MOUSEEVENTF_LEFTUP);
        ::Sleep(200);
        const std::vector<POINT> clicks = window.clicks();
        if (clicks.empty()) {
            problems.add("the click at client " + std::to_string(target.x) + "," + std::to_string(target.y) + " never arrived");
        } else if (std::abs(clicks.front().x - target.x) > 1 || std::abs(clicks.front().y - target.y) > 1) {
            problems.add("the click at client " + std::to_string(target.x) + "," + std::to_string(target.y) + " arrived at " + std::to_string(clicks.front().x) + "," + std::to_string(clicks.front().y));
        }
    }
    return problems.outcome();
}

Outcome LswChecks::focus_follows(Context&) {
    InputWindow first;
    InputWindow second;
    std::string error;
    if (!first.open(L"lsw-suites-focus-a", 60, 60, 300, 200, error) || !second.open(L"lsw-suites-focus-b", 420, 60, 300, 200, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    for (int round = 0; round < 6; ++round) {
        InputWindow& target = round % 2 == 0 ? first : second;
        focus(target.hwnd());
        if (::GetForegroundWindow() != target.hwnd()) {
            problems.add("round " + std::to_string(round) + ": the foreground window is not the one focused");
        }
    }
    first.reset();
    RECT area{};
    ::GetWindowRect(first.hwnd(), &area);
    mouse((area.left + area.right) / 2, (area.top + area.bottom) / 2, 0);
    mouse((area.left + area.right) / 2, (area.top + area.bottom) / 2, MOUSEEVENTF_LEFTDOWN);
    mouse((area.left + area.right) / 2, (area.top + area.bottom) / 2, MOUSEEVENTF_LEFTUP);
    ::Sleep(400);
    if (::GetForegroundWindow() != first.hwnd()) {
        problems.add("a click on the first window did not make it the foreground window");
    }
    return problems.outcome();
}

Outcome LswChecks::status_numbers_match_activity(Context&) {
    return Context::skip("the stack's GPU status numbers exist on the host; the host phase judges them against the workloads of the run");
}

Outcome LswChecks::update_preserves_requirements(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    const Outcome requirements = env::InstallChecks::requirements_hold(context);
    if (requirements.status() == Status::Fail) {
        return requirements;
    }
    App app;
    std::string error;
    if (!app.start(context, 8, error)) {
        return Outcome::fail("apps do not launch: " + error);
    }
    return Outcome::ok();
}

Outcome LswChecks::window_close_ends_app(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    if (!app.start(context, 40, error)) {
        return Outcome::fail(error);
    }
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, app.found.pid);
    if (process == nullptr) {
        return Outcome::fail("the app process cannot be opened");
    }
    ::PostMessageW(app.found.window, WM_CLOSE, 0, 0);
    const DWORD waited = ::WaitForSingleObject(process, 10000);
    ::CloseHandle(process);
    Problems problems;
    if (::IsWindow(app.found.window) != FALSE) {
        problems.add("the window still exists after WM_CLOSE");
    }
    if (waited != WAIT_OBJECT_0) {
        problems.add("the app did not exit within 10 s of its window closing");
    }
    return problems.outcome();
}

Outcome LswChecks::maximized_not_at_drop_point(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    App app;
    std::string error;
    if (!app.start(context, 30, error)) {
        return Outcome::fail(error);
    }
    focus(app.found.window);
    ::ShowWindow(app.found.window, SW_MAXIMIZE);
    ::Sleep(800);
    const RECT work = work_area(app.found.window);
    RECT before{};
    ::GetWindowRect(app.found.window, &before);
    const int grab_x = (before.left + before.right) / 2;
    const int grab_y = before.top + 12;
    mouse(grab_x, grab_y, 0);
    mouse(grab_x, grab_y, MOUSEEVENTF_LEFTDOWN);
    for (int step = 1; step <= 10; ++step) {
        mouse(grab_x + 10 * step, grab_y + 25 * step, 0);
        ::Sleep(30);
    }
    mouse(grab_x + 100, grab_y + 250, MOUSEEVENTF_LEFTUP);
    ::Sleep(800);
    const RECT after = bounds(app.found.window);
    context.measure("after_drop", text(after));
    if (::IsZoomed(app.found.window) != FALSE && !close_to(after, work, 1)) {
        return Outcome::fail("a window still maximized after the drop is at " + text(after) + " instead of the work area " + text(work));
    }
    return Outcome::ok();
}

Outcome LswChecks::first_launch_mirrored(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, app_role);
    }
    return Context::skip("whether the host mirrors the first launch after boot is observed by the host phase; the guest side of a first launch is covered by sanity.lsw.mirror.relaunching_app");
}

}  // namespace lsw::suites::sanity::lsw
