#include "sanity/present/window_checks.hpp"

#include "core/text.hpp"
#include "support/api_policy.hpp"
#include "sanity/interop/kit.hpp"
#include "sanity/present/kit.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/process_sampler.hpp"
#include "support/swap_rig.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace lsw::suites::sanity::present {

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 120;

bool paced_path(const std::string& id) {
    return id != "vk_immediate" && id != "vk_mailbox";
}

struct Size {
    int w;
    int h;
};

BOOL CALLBACK collect_monitor(HMONITOR, HDC, LPRECT rect, LPARAM data) {
    reinterpret_cast<std::vector<RECT>*>(data)->push_back(*rect);
    return TRUE;
}

bool open_or_note(Context& context, PresentPath& path, int width, int height, const std::string& id, Problems& problems) {
    std::string error;
    const Opened state = Kit::open(path, width, height, error);
    if (state == Opened::Yes) {
        return true;
    }
    if (state == Opened::Unsupported) {
        context.note(id + " is not exercised: " + error);
    } else {
        problems.add(id + ": " + error);
    }
    return false;
}

}  // namespace

Outcome WindowChecks::resize_during_presentation(Context& context) {
    Problems problems;
    int exercised = 0;
    const Size sizes[] = {{200, 140}, {170, 150}, {220, 124}, {160, 120}};
    for (const std::string& id : PresentPath::ids()) {
        if (!paced_path(id)) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        if (!open_or_note(context, *path, kWidth, kHeight, id, problems)) {
            continue;
        }
        ++exercised;
        FrameWatcher watcher;
        watcher.start(path->client());
        struct Phase {
            long long from;
            long long to;
            int frame;
        };
        std::vector<Phase> phases;
        int frame = 0;
        std::string failure;
        for (int round = 0; round < 3 && failure.empty(); ++round) {
            for (const Size size : sizes) {
                std::string error;
                const long long left_previous = QpcClock::now();
                if (!phases.empty()) {
                    phases.back().to = left_previous;
                }
                if (!path->resize(size.w, size.h, error)) {
                    failure = "resize to " + std::to_string(size.w) + "x" + std::to_string(size.h) + ": " + error;
                    break;
                }
                watcher.set_area(path->client());
                PresentResult result;
                for (int k = 0; k < 2; ++k) {
                    result = path->present(frame, 1);
                    if (!result.ok) {
                        failure = "frame " + std::to_string(frame) + " after resizing to " + std::to_string(size.w) + "x" + std::to_string(size.h) + ": " + result.error;
                        break;
                    }
                    ++frame;
                }
                if (!failure.empty()) {
                    break;
                }
                std::string detail;
                const std::string label = Kit::sanitize(id) + ".resize" + std::to_string(frame);
                if (!path->drain(error) || !Kit::settle(context, label, *path, FrameImage::render(frame - 1, size.w, size.h), 0, detail)) {
                    failure = "frame " + std::to_string(frame - 1) + " is not exact at " + std::to_string(size.w) + "x" + std::to_string(size.h) + " after the resize: " + (detail.empty() ? error : detail);
                    break;
                }
                phases.push_back({QpcClock::now(), 0, frame - 1});
            }
        }
        if (!phases.empty() && phases.back().to == 0) {
            phases.back().to = QpcClock::now();
        }
        const std::vector<Sighting> sightings = watcher.stop();
        path->close();
        if (!failure.empty()) {
            problems.add(id + ": " + failure);
            continue;
        }
        const SequenceReport report = SequenceJudge::judge(sightings, RefreshRate::period_ms() * 0.75);
        if (report.regressions != 0) {
            problems.add(id + ": " + report.first_regression + " while resizing");
        }
        for (const Phase& phase : phases) {
            for (const Sighting& sighting : sightings) {
                if (sighting.qpc > phase.from && sighting.qpc < phase.to && sighting.value != phase.frame) {
                    problems.add(id + ": after frame " + std::to_string(phase.frame) + " was exact the window showed " + FrameProbe::name(sighting.value) + " (" + std::to_string(sighting.value) +
                                 ") before the next resize");
                    break;
                }
            }
        }
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened with the APIs enabled");
    }
    context.measure("paths_exercised", exercised);
    return problems.outcome();
}

Outcome WindowChecks::occlusion(Context& context) {
    Problems problems;
    int exercised = 0;
    for (const char* id : {"dxgi_flip_discard", "dxgi_blt_sequential", "d3d9ex_flip", "d3d12_flip"}) {
        if (ApiPolicy::is_skipped(PresentPath::api_of(id))) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        if (!open_or_note(context, *path, kWidth, kHeight, id, problems)) {
            continue;
        }
        ++exercised;
        std::string error;
        PresentResult result = path->present(2, 1);
        if (!result.ok || !Kit::await_frame(*path, 2, 3000)) {
            problems.add(std::string(id) + ": the window never showed its first frame before it was covered");
            path->close();
            continue;
        }
        int occluded = 0;
        int total = 0;
        {
            TestWindow cover;
            TestWindowConfig config;
            config.popup = true;
            config.topmost = true;
            config.x = Kit::kX - 30;
            config.y = Kit::kY - 30;
            config.width = kWidth + 60;
            config.height = kHeight + 60;
            if (!cover.create(config, error)) {
                problems.add(std::string(id) + ": " + error);
                path->close();
                continue;
            }
            cover.paint_with(RGB(10, 200, 90));
            ::InvalidateRect(cover.hwnd(), nullptr, TRUE);
            ::Sleep(400);
            const long long start = QpcClock::now();
            int frame = 3;
            while (QpcClock::ms_since(start) < 1500.0) {
                const long long before = QpcClock::now();
                result = path->present(frame, 1);
                if (QpcClock::ms_since(before) > 3000.0) {
                    problems.add(std::string(id) + ": Present blocked for more than 3 s while the window was covered");
                    break;
                }
                if (!result.ok) {
                    problems.add(std::string(id) + ": Present on a covered window failed: " + result.error);
                    break;
                }
                ++total;
                occluded += result.occluded ? 1 : 0;
                frame = 3 + (frame - 2) % 20;
            }
            if (!path->drain(error)) {
                problems.add(std::string(id) + ": the device did not stay usable while covered: " + error);
            }
        }
        Kit::report_status(context, id, "covered", occluded, total);
        ::Sleep(300);
        result = path->present(40, 1);
        std::string detail;
        if (!result.ok) {
            problems.add(std::string(id) + ": Present after uncovering failed: " + result.error);
        } else if (!path->drain(error) || !Kit::settle(context, Kit::sanitize(id) + ".uncovered", *path, FrameImage::render(40, kWidth, kHeight), 0, detail)) {
            problems.add(std::string(id) + ": the content is wrong after the cover was removed: " + (detail.empty() ? error : detail));
        }
        path->close();
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened with the APIs enabled");
    }
    return problems.outcome();
}

Outcome WindowChecks::minimize_restore(Context& context) {
    Problems problems;
    int exercised = 0;
    for (const std::string& id : PresentPath::ids()) {
        if (!paced_path(id)) {
            continue;
        }
        std::unique_ptr<PresentPath> path = PresentPath::make(id);
        if (!open_or_note(context, *path, kWidth, kHeight, id, problems)) {
            continue;
        }
        ++exercised;
        std::string error;
        HWND window = path->hwnd();
        if (!path->present(1, 1).ok || !Kit::await_frame(*path, 1, 3000)) {
            problems.add(id + ": the window never showed its first frame");
            path->close();
            continue;
        }
        ::ShowWindow(window, SW_MINIMIZE);
        ::Sleep(400);
        int total = 0;
        int occluded = 0;
        int stale = 0;
        for (int frame = 2; frame < 18; ++frame) {
            const long long before = QpcClock::now();
            const PresentResult result = path->present(frame, 1);
            if (QpcClock::ms_since(before) > 3000.0) {
                problems.add(id + ": Present blocked for more than 3 s on a minimised window");
                break;
            }
            if (!result.ok && !result.stale) {
                problems.add(id + ": Present on a minimised window returned an undocumented failure: " + result.error);
                break;
            }
            ++total;
            occluded += result.occluded ? 1 : 0;
            stale += result.stale ? 1 : 0;
        }
        if (!path->drain(error)) {
            problems.add(id + ": the device did not stay usable while minimised: " + error);
        }
        ::ShowWindow(window, SW_RESTORE);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((::IsIconic(window) != FALSE || path->client().right - path->client().left != kWidth) && std::chrono::steady_clock::now() < deadline) {
            ::Sleep(20);
        }
        if (::IsIconic(window) != FALSE) {
            problems.add(id + ": the window did not restore");
            path->close();
            continue;
        }
        if (stale > 0 && !path->resize(kWidth, kHeight, error)) {
            problems.add(id + ": recreating the swapchain after the restore failed: " + error);
            path->close();
            continue;
        }
        const PresentResult result = path->present(50, 1);
        std::string detail;
        if (!result.ok) {
            problems.add(id + ": Present after the restore failed: " + result.error);
        } else if (!path->drain(error) || !Kit::settle(context, Kit::sanitize(id) + ".restored", *path, FrameImage::render(50, kWidth, kHeight), 0, detail)) {
            problems.add(id + ": the latest frame is not displayed after the restore: " + (detail.empty() ? error : detail));
        }
        context.measure(id + ".minimized.occluded", occluded);
        context.measure(id + ".minimized.stale", stale);
        context.measure(id + ".minimized.presents", total);
        path->close();
    }
    if (exercised == 0 && problems.empty()) {
        return Context::skip("no presentation path could be opened with the APIs enabled");
    }
    return problems.outcome();
}

Outcome WindowChecks::fullscreen_exclusive(Context& context) {
    SwapRig rig;
    SwapOptions options;
    options.width = kWidth;
    options.height = kHeight;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    options.effect = DXGI_SWAP_EFFECT_DISCARD;
    options.buffers = 2;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    D3d11FrameDraw draw;
    if (!draw.open(rig.device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    DEVMODEW before{};
    before.dmSize = sizeof(before);
    ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &before);
    RECT window_before{};
    ::GetWindowRect(rig.window.hwnd(), &window_before);
    ::SetForegroundWindow(rig.window.hwnd());
    ::Sleep(200);
    HRESULT result = rig.chain->SetFullscreenState(TRUE, nullptr);
    if (result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || result == E_ACCESSDENIED) {
        ::Sleep(500);
        result = rig.chain->SetFullscreenState(TRUE, nullptr);
    }
    if (FAILED(result)) {
        draw.release();
        return Outcome::fail("SetFullscreenState(TRUE) returned " + Kit::hr(result));
    }
    BOOL state = FALSE;
    Com<IDXGIOutput> output;
    rig.chain->GetFullscreenState(&state, output.put());
    if (state == FALSE) {
        problems.add("GetFullscreenState reports windowed after SetFullscreenState(TRUE)");
    }
    DXGI_OUTPUT_DESC output_desc{};
    int mode_width = static_cast<int>(before.dmPelsWidth);
    int mode_height = static_cast<int>(before.dmPelsHeight);
    if (output && SUCCEEDED(output->GetDesc(&output_desc))) {
        mode_width = static_cast<int>(output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left);
        mode_height = static_cast<int>(output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top);
    }
    rig.device.context->ClearState();
    result = rig.chain->ResizeBuffers(0, static_cast<UINT>(mode_width), static_cast<UINT>(mode_height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) {
        problems.add("ResizeBuffers to the display mode returned " + Kit::hr(result));
    } else {
        Com<ID3D11RenderTargetView> view = rig.back_view(0);
        if (!view) {
            problems.add("no render target view in fullscreen");
        } else {
            draw.frame(view.get(), mode_width, mode_height, 19);
            rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
            view.reset();
            result = rig.chain->Present(1, 0);
            static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
            if (FAILED(result)) {
                problems.add("Present in fullscreen returned " + Kit::hr(result));
            } else {
                std::string detail;
                const RECT desktop = output_desc.DesktopCoordinates.right != 0 ? output_desc.DesktopCoordinates : RECT{0, 0, mode_width, mode_height};
                if (!interop::Kit::settle_screen(context, "fullscreen", desktop, FrameImage::render(19, mode_width, mode_height), 0, 3000, detail)) {
                    problems.add("the fullscreen frame differs from what was rendered at " + std::to_string(mode_width) + "x" + std::to_string(mode_height) + ": " + detail);
                }
            }
        }
    }
    rig.device.context->ClearState();
    result = rig.chain->SetFullscreenState(FALSE, nullptr);
    if (FAILED(result)) {
        problems.add("SetFullscreenState(FALSE) returned " + Kit::hr(result));
    }
    rig.device.context->ClearState();
    static_cast<void>(rig.chain->ResizeBuffers(0, kWidth, kHeight, DXGI_FORMAT_UNKNOWN, 0));
    ::Sleep(800);
    DEVMODEW after{};
    after.dmSize = sizeof(after);
    ::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &after);
    if (after.dmPelsWidth != before.dmPelsWidth || after.dmPelsHeight != before.dmPelsHeight || after.dmDisplayFrequency != before.dmDisplayFrequency) {
        problems.add("the desktop mode is " + std::to_string(after.dmPelsWidth) + "x" + std::to_string(after.dmPelsHeight) + "@" + std::to_string(after.dmDisplayFrequency) + " after leaving fullscreen, it was " +
                     std::to_string(before.dmPelsWidth) + "x" + std::to_string(before.dmPelsHeight) + "@" + std::to_string(before.dmDisplayFrequency));
    }
    RECT window_after{};
    ::GetWindowRect(rig.window.hwnd(), &window_after);
    if (window_after.left != window_before.left || window_after.top != window_before.top || window_after.right != window_before.right || window_after.bottom != window_before.bottom) {
        problems.add("the window rectangle changed from (" + std::to_string(window_before.left) + "," + std::to_string(window_before.top) + "," + std::to_string(window_before.right) + "," +
                     std::to_string(window_before.bottom) + ") to (" + std::to_string(window_after.left) + "," + std::to_string(window_after.top) + "," + std::to_string(window_after.right) + "," +
                     std::to_string(window_after.bottom) + ") across a fullscreen round trip");
    }
    draw.release();
    return problems.outcome();
}

Outcome WindowChecks::borderless_fullscreen_flip(Context& context) {
    Problems problems;
    POINT origin{0, 0};
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info) == FALSE) {
        return Outcome::fail("the primary monitor rectangle is unavailable");
    }
    const int width = static_cast<int>(info.rcMonitor.right - info.rcMonitor.left);
    const int height = static_cast<int>(info.rcMonitor.bottom - info.rcMonitor.top);
    {
        SwapRig rig;
        SwapOptions options;
        options.width = static_cast<UINT>(width);
        options.height = static_cast<UINT>(height);
        options.x = info.rcMonitor.left;
        options.y = info.rcMonitor.top;
        options.popup = true;
        options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        std::string error;
        D3d11FrameDraw draw;
        if (!rig.open(options, error) || !draw.open(rig.device, error)) {
            return Outcome::fail(error);
        }
        for (const int frame : {17, 18}) {
            Com<ID3D11RenderTargetView> view = rig.back_view(0);
            if (!view) {
                problems.add("no render target view over buffer 0");
                break;
            }
            draw.frame(view.get(), width, height, frame);
            rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
            view.reset();
            const HRESULT presented = rig.chain->Present(1, 0);
            static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
            if (FAILED(presented)) {
                problems.add("Present returned " + Kit::hr(presented));
                break;
            }
            std::string detail;
            if (!interop::Kit::settle_screen(context, "borderless_" + std::to_string(frame), info.rcMonitor, FrameImage::render(frame, width, height), 0, 4000, detail)) {
                problems.add("the borderless flip frame " + std::to_string(frame) + " differs from what was rendered: " + detail);
            }
        }
        context.note("whether the compositor used independent flip is not queryable through a documented API, so only the pixels are judged");
        rig.chain.reset();
        draw.release();
        rig.device.destroy();
        rig.window.destroy();
    }
    ::Sleep(600);
    if (FrameProbe::sample(info.rcMonitor) == 18) {
        problems.add("the desktop still shows the fullscreen frame after the window was destroyed");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_discard");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        problems.add("a windowed flip chain cannot be opened after leaving borderless fullscreen: " + error);
    } else {
        std::string detail;
        if (!path->present(23, 1).ok || !path->drain(error) || !Kit::settle(context, "windowed_after", *path, FrameImage::render(23, kWidth, kHeight), 0, detail)) {
            problems.add("windowed flip presentation is not exact after leaving borderless fullscreen: " + (detail.empty() ? error : detail));
        }
        path->close();
    }
    return problems.outcome();
}

Outcome WindowChecks::many_windows(Context& context) {
    constexpr int kCount = 16;
    constexpr int kClientW = 120;
    constexpr int kClientH = 80;
    SwapRig rig;
    SwapOptions options;
    options.width = kClientW;
    options.height = kClientH;
    options.visible = false;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    D3d11FrameDraw draw;
    if (!draw.open(rig.device, error)) {
        return Outcome::fail(error);
    }
    struct Cell {
        TestWindow window;
        Com<IDXGISwapChain1> chain;
    };
    std::vector<std::unique_ptr<Cell>> cells;
    for (int i = 0; i < kCount; ++i) {
        auto cell = std::make_unique<Cell>();
        TestWindowConfig config;
        config.x = 10 + (i % 4) * 150;
        config.y = 10 + (i / 4) * 130;
        TestWindow::outer_size(config.x, config.y, kClientW, kClientH, config.width, config.height);
        if (!cell->window.create(config, error)) {
            return Outcome::fail("window " + std::to_string(i) + ": " + error);
        }
        ::SetWindowPos(cell->window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SwapOptions chain_options = rig.options;
        chain_options.visible = true;
        const HRESULT created = rig.try_create(chain_options, cell->window.hwnd(), cell->chain);
        if (FAILED(created)) {
            return Outcome::fail("swap chain " + std::to_string(i) + ": " + Kit::hr(created));
        }
        cells.push_back(std::move(cell));
    }
    Problems problems;
    constexpr int kSteps = 8;
    std::vector<int> last(kCount, -1);
    for (int step = 0; step < kSteps && problems.empty(); ++step) {
        for (int i = 0; i < kCount; ++i) {
            Com<ID3D11Texture2D> buffer;
            Com<ID3D11RenderTargetView> view;
            cells[static_cast<std::size_t>(i)]->chain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void());
            if (!buffer || FAILED(rig.device.device->CreateRenderTargetView(buffer.get(), nullptr, view.put()))) {
                problems.add("no render target view for window " + std::to_string(i));
                break;
            }
            draw.frame(view.get(), kClientW, kClientH, (i * 4 + step) % FrameImage::kFrames);
            rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
            const HRESULT presented = cells[static_cast<std::size_t>(i)]->chain->Present(0, 0);
            if (FAILED(presented)) {
                problems.add("window " + std::to_string(i) + " Present returned " + Kit::hr(presented));
            }
        }
        static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        for (int i = 0; i < kCount; ++i) {
            const int want = (i * 4 + step) % FrameImage::kFrames;
            const RECT area = ScreenCapture::client_on_screen(cells[static_cast<std::size_t>(i)]->window.hwnd());
            if (!Kit::await_frame_in(area, want, 3000)) {
                problems.add("window " + std::to_string(i) + " at step " + std::to_string(step) + " shows " + FrameProbe::name(FrameProbe::sample(area)) + " " + std::to_string(FrameProbe::sample(area)) +
                             " instead of its own frame " + std::to_string(want));
                break;
            }
            if (want < last[static_cast<std::size_t>(i)] && last[static_cast<std::size_t>(i)] - want < 32) {
                problems.add("window " + std::to_string(i) + " went back from frame " + std::to_string(last[static_cast<std::size_t>(i)]) + " to " + std::to_string(want));
            }
            last[static_cast<std::size_t>(i)] = want;
        }
    }
    for (int i = 0; i < kCount && problems.empty(); ++i) {
        std::string detail;
        const int want = (i * 4 + kSteps - 1) % FrameImage::kFrames;
        if (!interop::Kit::settle_screen(context, "many_windows_" + std::to_string(i), ScreenCapture::client_on_screen(cells[static_cast<std::size_t>(i)]->window.hwnd()),
                                         FrameImage::render(want, kClientW, kClientH), 0, 3000, detail)) {
            problems.add("window " + std::to_string(i) + " is not exact at its last frame " + std::to_string(want) + ": " + detail);
        }
    }
    static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
    for (std::unique_ptr<Cell>& cell : cells) {
        cell->chain.reset();
        cell->window.destroy();
    }
    draw.release();
    return problems.outcome();
}

Outcome WindowChecks::multi_monitor(Context& context) {
    std::vector<RECT> monitors;
    ::EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors));
    if (monitors.size() < 2) {
        return Context::skip("the session has " + std::to_string(monitors.size()) + " monitor; the row needs more than one output");
    }
    Problems problems;
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_sequential");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        return Outcome::fail(error);
    }
    int frame = 1;
    for (int pass = 0; pass < 2; ++pass) {
        for (const RECT& monitor : monitors) {
            ::SetWindowPos(path->hwnd(), nullptr, static_cast<int>(monitor.left) + 40, static_cast<int>(monitor.top) + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ::Sleep(400);
            RECT client = path->client();
            if (client.right - client.left != path->width() || client.bottom - client.top != path->height()) {
                if (!path->resize(path->width(), path->height(), error)) {
                    problems.add("keeping the client size on the monitor at " + std::to_string(monitor.left) + "," + std::to_string(monitor.top) + " failed: " + error);
                    continue;
                }
            }
            for (int k = 0; k < 3; ++k, ++frame) {
                const PresentResult result = path->present(frame, 1);
                if (!result.ok) {
                    problems.add("Present on the monitor at " + std::to_string(monitor.left) + "," + std::to_string(monitor.top) + " failed: " + result.error);
                    break;
                }
            }
            std::string detail;
            if (!path->drain(error) || !Kit::settle(context, "monitor_" + std::to_string(monitor.left) + "_" + std::to_string(frame), *path, FrameImage::render(frame - 1, path->width(), path->height()), 0, detail)) {
                problems.add("frame " + std::to_string(frame - 1) + " is not exact after moving to the monitor at " + std::to_string(monitor.left) + "," + std::to_string(monitor.top) + ": " + (detail.empty() ? error : detail));
            }
        }
    }
    path->close();
    return problems.outcome();
}

Outcome WindowChecks::destroy_while_presenting(Context& context) {
    for (int warm = 0; warm < 1; ++warm) {
        std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_discard");
        std::string error;
        if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
            return Outcome::fail(error);
        }
        path->close();
    }
    const ProcessSample before = ProcessSampler::take();
    Problems problems;
    {
        SwapRig rig;
        SwapOptions options;
        options.width = kWidth;
        options.height = kHeight;
        options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        std::string error;
        D3d11FrameDraw draw;
        if (!rig.open(options, error) || !draw.open(rig.device, error)) {
            return Outcome::fail(error);
        }
        std::atomic<bool> stop{false};
        std::atomic<int> presents{0};
        std::atomic<int> failures{0};
        std::atomic<long> last_failure{0};
        std::thread presenter([&] {
            int frame = 0;
            while (!stop) {
                Com<ID3D11RenderTargetView> view = rig.back_view(0);
                if (view) {
                    draw.frame(view.get(), kWidth, kHeight, frame % FrameImage::kFrames);
                    rig.device.context->OMSetRenderTargets(0, nullptr, nullptr);
                    view.reset();
                }
                const HRESULT presented = rig.chain->Present(0, 0);
                if (FAILED(presented)) {
                    ++failures;
                    last_failure = static_cast<long>(presented);
                    ::Sleep(5);
                } else {
                    ++presents;
                }
                ++frame;
            }
        });
        ::Sleep(400);
        const long long destroy_start = QpcClock::now();
        rig.window.destroy();
        const double destroy_ms = QpcClock::ms_since(destroy_start);
        ::Sleep(300);
        stop = true;
        const long long join_start = QpcClock::now();
        presenter.join();
        const double join_ms = QpcClock::ms_since(join_start);
        context.measure("destroy_window_ms", destroy_ms);
        context.measure("join_ms", join_ms);
        context.measure("presents_ok", presents.load());
        context.measure("presents_failed_after_destroy", failures.load());
        if (failures.load() > 0) {
            context.measure("last_failure_hr", Kit::hr(static_cast<HRESULT>(last_failure.load())));
        }
        if (destroy_ms > 5000.0) {
            problems.add("destroying the window took " + std::to_string(destroy_ms) + " ms while another thread was presenting");
        }
        if (presents.load() < 5) {
            problems.add("the presenting thread managed only " + std::to_string(presents.load()) + " presents before the window was destroyed");
        }
        static_cast<void>(D3d11Tools::wait_event(rig.device, rig.device.context, error));
        if (rig.device.device->GetDeviceRemovedReason() != S_OK) {
            problems.add("destroying the window removed the device: " + Kit::hr(rig.device.device->GetDeviceRemovedReason()));
        }
        rig.chain.reset();
        draw.release();
        rig.device.destroy();
    }
    ::Sleep(200);
    const ProcessSample after = ProcessSampler::take();
    context.measure("handles_before", before.handles);
    context.measure("handles_after", after.handles);
    if (after.handles > before.handles + 4) {
        problems.add("the process holds " + std::to_string(after.handles) + " handles after the window, chain and device were released, " + std::to_string(before.handles) + " before");
    }
    std::unique_ptr<PresentPath> path = PresentPath::make("dxgi_flip_discard");
    std::string error;
    if (Kit::open(*path, kWidth, kHeight, error) != Opened::Yes) {
        problems.add("a new window and chain cannot be created afterwards: " + error);
    } else {
        std::string detail;
        if (!path->present(9, 1).ok || !path->drain(error) || !Kit::settle(context, "new_window", *path, FrameImage::render(9, kWidth, kHeight), 0, detail)) {
            problems.add("the new window does not present correctly: " + (detail.empty() ? error : detail));
        }
        path->close();
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::present
