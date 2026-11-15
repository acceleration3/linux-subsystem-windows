#include "sanity/present/display_checks.hpp"

#include "core/text.hpp"
#include "sanity/present/kit.hpp"
#include "support/api_policy.hpp"

#include <dwmapi.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;

constexpr GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
constexpr GUID kMonitorPowerOn = {0x02731015, 0x4510, 0x4526, {0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea}};

std::atomic<int> g_display_off{0};
std::atomic<int> g_display_events{0};

LRESULT CALLBACK power_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_POWERBROADCAST && wparam == PBT_POWERSETTINGCHANGE) {
        const POWERBROADCAST_SETTING* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
        if (setting != nullptr && setting->DataLength >= sizeof(DWORD)) {
            ++g_display_events;
            const DWORD value = *reinterpret_cast<const DWORD*>(setting->Data);
            if (value == 0) {
                ++g_display_off;
            }
        }
        return TRUE;
    }
    if (message == WM_CLOSE) {
        ::DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY) {
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

class PowerWatcher {
public:
    ~PowerWatcher() { stop(); }

    [[nodiscard]] bool start() {
        g_display_off = 0;
        g_display_events = 0;
        std::atomic<bool> ready{false};
        thread_ = std::thread([this, &ready] {
            WNDCLASSEXW cls{};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = power_proc;
            cls.hInstance = ::GetModuleHandleW(nullptr);
            cls.lpszClassName = L"lsw_suites_power_watch";
            ::RegisterClassExW(&cls);
            HWND window = ::CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
            HPOWERNOTIFY a = nullptr;
            HPOWERNOTIFY b = nullptr;
            if (window != nullptr) {
                a = ::RegisterPowerSettingNotification(window, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
                b = ::RegisterPowerSettingNotification(window, &kMonitorPowerOn, DEVICE_NOTIFY_WINDOW_HANDLE);
            }
            window_ = window;
            ready = true;
            MSG message;
            while (window != nullptr && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
            if (a != nullptr) {
                ::UnregisterPowerSettingNotification(a);
            }
            if (b != nullptr) {
                ::UnregisterPowerSettingNotification(b);
            }
        });
        while (!ready) {
            ::Sleep(2);
        }
        return window_ != nullptr;
    }

    void stop() {
        if (thread_.joinable()) {
            if (window_ != nullptr) {
                ::PostMessageW(window_, WM_CLOSE, 0, 0);
            }
            thread_.join();
        }
    }

private:
    std::thread thread_;
    std::atomic<HWND> window_{nullptr};
};

std::vector<std::wstring> attached_displays() {
    std::vector<std::wstring> out;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD i = 0; ::EnumDisplayDevicesW(nullptr, i, &device, 0) != FALSE; ++i) {
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0) {
            out.emplace_back(device.DeviceName);
        }
        device = {};
        device.cb = sizeof(device);
    }
    return out;
}

bool still_attached(const std::wstring& name) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD i = 0; ::EnumDisplayDevicesW(nullptr, i, &device, 0) != FALSE; ++i) {
        if (name == device.DeviceName) {
            return (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
        }
        device = {};
        device.cb = sizeof(device);
    }
    return false;
}

}  // namespace

Outcome DisplayChecks::host_receives_sequence(Context& context) {
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        return Outcome::fail(error);
    }
    Problems problems;
    const double period = RefreshRate::period_ms();
    FrameWatcher watcher;
    watcher.start(path->client());
    ::Sleep(80);
    std::string sequence;
    constexpr int kFrames = 40;
    for (int frame = 0; frame < kFrames; ++frame) {
        const PresentResult result = path->present(frame, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
        sequence += (sequence.empty() ? "" : ",") + std::to_string(frame);
    }
    static_cast<void>(path->drain(error));
    if (!Kit::await_frame(*path, kFrames - 1, 3000)) {
        problems.add("the last presented frame never became visible in the guest");
    }
    const SequenceReport report = SequenceJudge::judge(watcher.stop(), period * 0.75);
    if (report.regressions != 0 || report.torn != 0) {
        problems.add("the guest displayed the sequence out of order (" + SequenceJudge::summary(report) + ")");
    }
    context.measure("presented_sequence", sequence);
    context.measure("guest_seen_sequence", SequenceJudge::list(report.seen));
    context.measure("final_frame", kFrames - 1);
    const RECT area = path->client();
    context.measure("client_rect", std::to_string(area.left) + "," + std::to_string(area.top) + "," + std::to_string(area.right) + "," + std::to_string(area.bottom));
    std::string detail;
    if (!Kit::settle(context, "mirror_final", *path, FrameImage::render(kFrames - 1, kWidth, kHeight), 0, detail)) {
        problems.add("the final frame is not exact in the guest: " + detail);
    }
    ::Sleep(1500);
    context.note("the host half (same frame number sequence, nothing from the future, final frame delivered, capture equal to this content) is judged by the matrix host phase from presented_sequence and final_frame");
    path->close();
    return problems.outcome();
}

Outcome DisplayChecks::output_stays_active(Context& context) {
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        return Outcome::fail(error);
    }
    Problems problems;
    const std::vector<std::wstring> displays = attached_displays();
    if (displays.empty()) {
        path->close();
        return Outcome::fail("no display device is attached to the desktop at the start");
    }
    PowerWatcher power;
    const bool watching = power.start();
    context.measure("power_watch", watching);
    const long long start = QpcClock::now();
    int frame = 0;
    int checks = 0;
    double next_check = 0.0;
    while (QpcClock::ms_since(start) < 3000.0) {
        const PresentResult result = path->present(frame % FrameImage::kFrames, 1);
        if (!result.ok) {
            problems.add("frame " + std::to_string(frame) + ": " + result.error);
            break;
        }
        ++frame;
        if (QpcClock::ms_since(start) >= next_check) {
            next_check += 100.0;
            ++checks;
            BOOL composition = FALSE;
            if (FAILED(::DwmIsCompositionEnabled(&composition)) || composition == FALSE) {
                problems.add("DWM composition is not enabled " + std::to_string(static_cast<int>(QpcClock::ms_since(start))) + " ms into the run");
                break;
            }
            for (const std::wstring& name : displays) {
                if (!still_attached(name)) {
                    problems.add("the display " + Text::utf8(name) + " is no longer attached to the desktop " + std::to_string(static_cast<int>(QpcClock::ms_since(start))) + " ms into the run");
                    break;
                }
            }
            if (!problems.empty()) {
                break;
            }
        }
    }
    static_cast<void>(path->drain(error));
    power.stop();
    context.measure("frames", frame);
    context.measure("state_checks", checks);
    context.measure("power_events", g_display_events.load());
    if (g_display_off.load() > 0) {
        problems.add("the system reported the display turning off " + std::to_string(g_display_off.load()) + " times while frames were composed");
    }
    path->close();
    return problems.outcome();
}

Outcome DisplayChecks::idle_then_present(Context& context) {
    Problems problems;
    int exercised = 0;
    for (const char* id : {"dxgi_flip_sequential", "dxgi_blt_sequential", "d3d12_flip", "gl_swapbuffers"}) {
        if (ApiPolicy::is_skipped(PresentPath::api_of(id))) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        std::string error;
        const Opened state = Kit::open(*path, kWidth, kHeight, error);
        if (state == Opened::Unsupported) {
            context.note(std::string(id) + " is not exercised: " + error);
            continue;
        }
        if (state == Opened::Failed) {
            problems.add(std::string(id) + ": " + error);
            continue;
        }
        ++exercised;
        if (!path->present(3, 1).ok || !path->drain(error) || !Kit::await_frame(*path, 3, 3000)) {
            problems.add(std::string(id) + ": the first frame never became visible");
            path->close();
            continue;
        }
        ::Sleep(2500);
        if (FrameProbe::sample(path->client()) != 3) {
            problems.add(std::string(id) + ": the window no longer shows frame 3 after 2.5 s without presents");
        }
        const long long before = QpcClock::now();
        const PresentResult result = path->present(30, 1);
        if (!result.ok) {
            problems.add(std::string(id) + ": " + result.error);
            path->close();
            continue;
        }
        const bool shown = Kit::await_frame(*path, 30, 3000);
        const double latency = QpcClock::ms_since(before);
        context.measure(std::string(id) + ".idle_present_latency_ms", latency);
        if (!shown || latency > 1000.0) {
            problems.add(std::string(id) + ": the first present after an idle period took " + std::to_string(latency) + " ms to reach the screen, above 1000 ms");
        }
        static_cast<void>(path->drain(error));
        path->close();
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened with the APIs enabled");
    }
    return problems.outcome();
}

Outcome DisplayChecks::input_to_photon_monotonic(Context& context) {
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_discard");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        return Outcome::fail(error);
    }
    POINT original{};
    ::GetCursorPos(&original);
    const RECT client = path->client();
    ::SetCursorPos(static_cast<int>(client.left) + 20, static_cast<int>(client.top) + 20);
    ::Sleep(150);
    POINT position{};
    ::GetCursorPos(&position);
    Problems problems;
    constexpr int kInputs = 40;
    std::vector<long long> sent(kInputs + 1, 0);
    FrameWatcher watcher;
    watcher.start(path->client());
    ::Sleep(80);
    int delivered = 0;
    bool blocked = false;
    for (int k = 1; k <= kInputs; ++k) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = (k % 2) != 0 ? 1 : -1;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        sent[static_cast<std::size_t>(k)] = QpcClock::now();
        if (::SendInput(1, &input, sizeof(input)) != 1) {
            blocked = true;
            break;
        }
        POINT now{};
        const long long wait_start = QpcClock::now();
        do {
            ::GetCursorPos(&now);
            if (now.x != position.x) {
                break;
            }
            ::Sleep(1);
        } while (QpcClock::ms_since(wait_start) < 500.0);
        if (now.x == position.x) {
            problems.add("input " + std::to_string(k) + " never moved the cursor");
            break;
        }
        position = now;
        const PresentResult result = path->present(k, 0);
        if (!result.ok) {
            problems.add("frame " + std::to_string(k) + ": " + result.error);
            break;
        }
        ++delivered;
        ::Sleep(40);
    }
    static_cast<void>(path->drain(error));
    const bool final_shown = delivered > 0 && Kit::await_frame(*path, delivered, 3000);
    const std::vector<Sighting> sightings = watcher.stop();
    ::SetCursorPos(original.x, original.y);
    path->close();
    if (blocked) {
        return Context::skip("SendInput is refused in this session");
    }
    double worst_latency = 0.0;
    double worst_gap = 0.0;
    long long previous = 0;
    for (int k = 1; k <= delivered; ++k) {
        long long seen = 0;
        for (const Sighting& sighting : sightings) {
            if (sighting.value == k) {
                seen = sighting.qpc;
                break;
            }
        }
        if (seen == 0) {
            continue;
        }
        if (seen < sent[static_cast<std::size_t>(k)]) {
            problems.add("frame " + std::to_string(k) + " was captured " + std::to_string(QpcClock::ms(sent[static_cast<std::size_t>(k)] - seen)) + " ms before the input that produced it");
        }
        worst_latency = std::max(worst_latency, QpcClock::ms(seen - sent[static_cast<std::size_t>(k)]));
        if (previous != 0) {
            worst_gap = std::max(worst_gap, QpcClock::ms(seen - previous));
        }
        previous = seen;
    }
    context.measure("worst_input_to_capture_ms", worst_latency);
    context.measure("worst_gap_between_frames_ms", worst_gap);
    context.measure("inputs", delivered);
    const SequenceReport report = SequenceJudge::judge(sightings, RefreshRate::period_ms() * 0.75);
    if (report.regressions != 0) {
        problems.add(report.first_regression);
    }
    if (!final_shown && problems.empty()) {
        problems.add("the frame for the last input never reached the capture");
    }
    if (worst_gap > 500.0) {
        problems.add("the captured sequence has a gap of " + std::to_string(worst_gap) + " ms between consecutive frames while inputs arrived every 40 ms, above 500 ms");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
