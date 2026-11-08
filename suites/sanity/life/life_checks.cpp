#include "sanity/life/life_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/iso/fleet.hpp"
#include "sanity/iso/loads.hpp"
#include "sanity/iso/scene.hpp"
#include "sanity/life/kit.hpp"
#include "support/av_adapters.hpp"
#include "support/d3d11_tools.hpp"
#include "support/display_devices.hpp"
#include "support/fresh_process.hpp"
#include "support/pattern.hpp"
#include "support/refresh_rate.hpp"
#include "support/robust_child.hpp"
#include "support/swap_rig.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"

#include <powrprof.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::life {

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kTimeout = 240;

double since_ms(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

Outcome child_dispatch(Context& context) {
    return RobustChild::child(context, Kit::app_role);
}

std::string adapter_keys() {
    std::vector<AvAdapter> adapters;
    std::string error;
    std::string keys;
    if (AvAdapters::accel(adapters, error)) {
        std::vector<std::string> parts;
        for (const AvAdapter& adapter : adapters) {
            parts.push_back(adapter.luid_text() + "/" + std::to_string(adapter.outputs.size()));
        }
        std::sort(parts.begin(), parts.end());
        for (const std::string& part : parts) {
            keys += part + ";";
        }
    }
    return keys;
}

void start_apps(iso::Fleet& fleet, Context& context, double seconds) {
    for (const std::string& api : Kit::app_apis()) {
        fleet.start(context.id(), "app-" + api, "app-" + api, iso::Scene::seconds(seconds), kTimeout);
    }
}

bool advancing_vblank(double window_ms) {
    DWM_TIMING_INFO first;
    DWM_TIMING_INFO second;
    if (!RefreshRate::dwm_timing(first)) {
        return false;
    }
    ::Sleep(static_cast<DWORD>(window_ms));
    return RefreshRate::dwm_timing(second) && second.cRefresh > first.cRefresh;
}

struct Marker {
    TestWindow window;
    RECT area{};

    bool open(COLORREF color, std::string& error) {
        TestWindowConfig config;
        config.x = 200;
        config.y = 200;
        config.width = 160;
        config.height = 120;
        config.popup = true;
        config.topmost = true;
        if (!window.create(config, error)) {
            return false;
        }
        paint(color);
        ::GetWindowRect(window.hwnd(), &area);
        return true;
    }

    void paint(COLORREF color) {
        window.paint_with(color);
        ::InvalidateRect(window.hwnd(), nullptr, TRUE);
        ::UpdateWindow(window.hwnd());
    }
};

Outcome with_marker_after(Context& context, const std::function<bool(std::string&)>& disturb, const std::string& what, double limit_ms) {
    Marker marker;
    std::string error;
    const COLORREF color = RGB(12, 200, 96);
    if (!marker.open(color, error)) {
        return Outcome::fail(error);
    }
    double shown = 0;
    if (!Kit::desktop_shows(marker.area, color, 5000, shown)) {
        return Outcome::fail("the marker window is not visible in a desktop capture before " + what);
    }
    if (!disturb(error)) {
        return Outcome::fail(error);
    }
    marker.paint(color);
    double recovered = 0;
    const bool back = Kit::desktop_shows(marker.area, color, static_cast<int>(limit_ms), recovered);
    context.measure("desktop_recovered_ms", recovered);
    if (!back) {
        return Outcome::fail("the desktop did not show the marker window within " + std::to_string(static_cast<int>(limit_ms)) + " ms after " + what);
    }
    if (!advancing_vblank(500)) {
        return Outcome::fail("DwmGetCompositionTimingInfo shows no advancing refresh counter after " + what);
    }
    return Outcome::ok();
}

}  // namespace

Outcome LifeChecks::dwm_restart_apps_survive(Context& context) {
    if (ChildMode::active()) {
        return child_dispatch(context);
    }
    if (Kit::app_apis().empty()) {
        return Context::skip("no D3D11 or Vulkan present path is available");
    }
    iso::Fleet apps;
    start_apps(apps, context, 18);
    iso::Scene::sleep(5000);
    double ms = 0;
    std::string error;
    Problems problems;
    if (!Kit::restart_dwm(20000, ms, error)) {
        problems.add(error);
    }
    context.measure("dwm_restart_ms", ms);
    apps.join();
    Kit::judge_apps(problems, apps, "after the DWM restart, application");
    iso::Scene::forward(context, apps, "");
    return problems.outcome();
}

Outcome LifeChecks::dwm_restart_desktop_recovers(Context& context) {
    return with_marker_after(context, [](std::string& error) {
        double ms = 0;
        return Kit::restart_dwm(20000, ms, error);
    }, "restarting DWM", 15000);
}

Outcome LifeChecks::mode_change_apps_survive(Context& context) {
    DEVMODEW original{};
    if (!Kit::current_mode(original)) {
        return Outcome::fail("the current display mode cannot be read");
    }
    DEVMODEW target{};
    for (const DEVMODEW& mode : Kit::modes()) {
        if ((mode.dmPelsWidth != original.dmPelsWidth || mode.dmPelsHeight != original.dmPelsHeight) && mode.dmPelsWidth >= 800 && (target.dmPelsWidth == 0 || mode.dmPelsWidth < target.dmPelsWidth)) {
            target = mode;
        }
    }
    if (target.dmPelsWidth == 0) {
        return Context::skip("the output enumerates no second resolution");
    }
    SwapRig rig;
    SwapOptions options;
    options.width = original.dmPelsWidth;
    options.height = original.dmPelsHeight;
    options.x = 0;
    options.y = 0;
    options.popup = true;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    int color[3] = {40, 160, 220};
    if (!rig.fill(40 / 255.0f, 160 / 255.0f, 220 / 255.0f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
        return Outcome::fail("presenting the first frame failed: " + error);
    }
    const LONG changed = Kit::set_mode(target);
    context.measure("mode_change_result", static_cast<long long>(changed));
    if (changed != DISP_CHANGE_SUCCESSFUL) {
        Kit::restore_mode();
        return Outcome::fail("ChangeDisplaySettingsEx to " + std::to_string(target.dmPelsWidth) + "x" + std::to_string(target.dmPelsHeight) + " returned " + std::to_string(changed));
    }
    int bad_presents = 0;
    HRESULT first_bad = S_OK;
    for (int i = 0; i < 20; ++i) {
        const HRESULT result = rig.chain->Present(1, 0);
        if (FAILED(result) && result != DXGI_ERROR_WAS_STILL_DRAWING) {
            ++bad_presents;
            first_bad = first_bad == S_OK ? result : first_bad;
        }
        ::Sleep(50);
    }
    context.measure("presents_failed_after_change", bad_presents);
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);
    if (width != static_cast<int>(target.dmPelsWidth) || height != static_cast<int>(target.dmPelsHeight)) {
        problems.add("GetSystemMetrics reports " + std::to_string(width) + "x" + std::to_string(height) + " after the change to " + std::to_string(target.dmPelsWidth) + "x" + std::to_string(target.dmPelsHeight));
    }
    rig.back_view().reset();
    rig.window.move(0, 0);
    rig.window.resize(width, height);
    const HRESULT resized = rig.chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resized)) {
        problems.add("ResizeBuffers to the new desktop size returned " + D3d11Tools::hr(resized) + (bad_presents != 0 ? ", presents had failed with " + D3d11Tools::hr(first_bad) : std::string()));
    } else {
        rig.options.width = static_cast<UINT>(width);
        rig.options.height = static_cast<UINT>(height);
        const int want[3] = {color[0], color[1], color[2]};
        int got[3] = {};
        if (!rig.fill(40 / 255.0f, 160 / 255.0f, 220 / 255.0f, 1.0f, error) || FAILED(rig.chain->Present(1, 0)) || !rig.wait_for_color(width - 8, height - 8, want, 4, 5000, got)) {
            problems.add("after the mode change the new bottom-right corner shows " + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + " instead of the rendered colour: " + error);
        }
    }
    Kit::restore_mode();
    ::Sleep(500);
    DEVMODEW now{};
    if (Kit::current_mode(now) && (now.dmPelsWidth != original.dmPelsWidth || now.dmPelsHeight != original.dmPelsHeight)) {
        problems.add("the original mode was not restored");
    }
    return problems.outcome();
}

Outcome LifeChecks::mode_refresh_rate_change(Context& context) {
    DEVMODEW original{};
    if (!Kit::current_mode(original)) {
        return Outcome::fail("the current display mode cannot be read");
    }
    DEVMODEW target{};
    for (const DEVMODEW& mode : Kit::modes()) {
        if (mode.dmPelsWidth == original.dmPelsWidth && mode.dmPelsHeight == original.dmPelsHeight && mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency != original.dmDisplayFrequency) {
            target = mode;
            break;
        }
    }
    if (target.dmDisplayFrequency == 0) {
        return Context::skip("the output enumerates no second refresh rate at the current resolution");
    }
    SwapRig rig;
    SwapOptions options;
    std::string error;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const LONG changed = Kit::set_mode(target);
    if (changed != DISP_CHANGE_SUCCESSFUL) {
        Kit::restore_mode();
        return Outcome::fail("ChangeDisplaySettingsEx to " + std::to_string(target.dmDisplayFrequency) + " Hz returned " + std::to_string(changed));
    }
    ::Sleep(1500);
    const double hz = RefreshRate::hz();
    context.measure("dwm_hz_after", hz);
    if (std::fabs(hz - target.dmDisplayFrequency) > target.dmDisplayFrequency * 0.03 + 0.6) {
        problems.add("DWM reports " + std::to_string(hz) + " Hz after the change to " + std::to_string(target.dmDisplayFrequency) + " Hz");
    }
    std::vector<double> stamps;
    const auto begin = Clock::now();
    while (since_ms(begin) < 1500) {
        if (FAILED(rig.chain->Present(1, 0))) {
            problems.add("Present failed after the refresh rate change");
            break;
        }
        stamps.push_back(since_ms(begin));
    }
    if (stamps.size() > 10) {
        const double period = (stamps.back() - stamps[2]) / static_cast<double>(stamps.size() - 3);
        context.measure("present_period_ms", period);
        if (std::fabs(period - 1000.0 / target.dmDisplayFrequency) > 1000.0 / target.dmDisplayFrequency * 0.15) {
            problems.add("vsync-paced presents took " + std::to_string(period) + " ms each after the change to " + std::to_string(target.dmDisplayFrequency) + " Hz");
        }
    }
    Kit::restore_mode();
    return problems.outcome();
}

Outcome LifeChecks::sleep_resume_contents(Context& context) {
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kSize = 256;
    std::vector<std::uint32_t> pixels(kSize * kSize);
    Pattern::fill_pixels(321, kSize, kSize, kSize * 4, pixels.data());
    D3D11_SUBRESOURCE_DATA initial{pixels.data(), kSize * 4, 0};
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), &initial);
    if (!texture) {
        return Outcome::fail("creating the texture failed");
    }
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    LARGE_INTEGER due;
    due.QuadPart = -200000000LL;
    if (timer == nullptr || !::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, TRUE)) {
        return Context::skip("a wake timer cannot be armed");
    }
    const auto begin = Clock::now();
    if (!::SetSuspendState(FALSE, TRUE, FALSE) || since_ms(begin) < 3000) {
        ::CloseHandle(timer);
        return Context::skip("the guest does not enter sleep (SetSuspendState returned without a suspend)");
    }
    ::CloseHandle(timer);
    context.measure("sleep_ms", since_ms(begin));
    ::Sleep(2000);
    Problems problems;
    const HRESULT reason = device.device->GetDeviceRemovedReason();
    if (reason == S_OK) {
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        TexelImage image;
        if (!staging) {
            problems.add("creating the staging texture after resume failed");
        } else {
            device.context->CopyResource(staging.get(), texture.get());
            if (!D3d11Tools::read_texture(device, staging.get(), 0, image, error)) {
                problems.add("reading the texture after resume failed: " + error);
            } else if (std::memcmp(image.data.data(), pixels.data(), pixels.size() * 4) != 0) {
                problems.add("the texture created before sleep changed across sleep and resume while the device reports no loss");
            }
        }
    } else if (reason != DXGI_ERROR_DEVICE_REMOVED && reason != DXGI_ERROR_DEVICE_RESET && reason != DXGI_ERROR_DEVICE_HUNG && reason != DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
        problems.add("the device reports the undocumented removal reason " + D3d11Tools::hr(reason) + " after resume");
    }
    context.measure("removed_reason", D3d11Tools::hr(reason));
    FreshProcess::check(context, problems, context.id(), "sleep_resume");
    return problems.outcome();
}

Outcome LifeChecks::agent_restart_apps_unaffected(Context& context) {
    if (ChildMode::active()) {
        return child_dispatch(context);
    }
    DWORD code = 0;
    if (!Kit::run(L"sc.exe query lsw-agent", code) || code != 0) {
        return Context::skip("the lsw-agent service is not installed in this guest");
    }
    iso::Fleet apps;
    start_apps(apps, context, 24);
    iso::Scene::sleep(5000);
    Problems problems;
    if (!Kit::run(L"sc.exe stop lsw-agent", code)) {
        problems.add("sc stop did not return");
    }
    iso::Scene::sleep(4000);
    if (!Kit::run(L"sc.exe start lsw-agent", code) || (code != 0 && code != 1056)) {
        problems.add("sc start lsw-agent failed with exit code " + std::to_string(code));
    }
    apps.join();
    Kit::judge_apps(problems, apps, "across the agent restart, application");
    iso::Scene::forward(context, apps, "");
    return problems.outcome();
}

Outcome LifeChecks::agent_restart_new_apps_work(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, [](Context&, const std::string& role) { return Outcome::fail("unknown role " + role); });
    }
    DWORD code = 0;
    if (!Kit::run(L"sc.exe query lsw-agent", code) || code != 0) {
        return Context::skip("the lsw-agent service is not installed in this guest");
    }
    const std::string before = adapter_keys();
    Problems problems;
    static_cast<void>(Kit::run(L"sc.exe stop lsw-agent", code));
    iso::Scene::sleep(4000);
    if (!Kit::run(L"sc.exe start lsw-agent", code) || (code != 0 && code != 1056)) {
        problems.add("sc start lsw-agent failed with exit code " + std::to_string(code));
    }
    iso::Scene::sleep(5000);
    const std::string after = adapter_keys();
    if (before != after) {
        problems.add("the adapters changed across the agent restart: [" + before + "] became [" + after + "]");
    }
    FreshProcess::check(context, problems, context.id(), "agent_restart");
    return problems.outcome();
}

Outcome LifeChecks::live_driver_update(Context& context) {
    if (ChildMode::active()) {
        return child_dispatch(context);
    }
    if (Kit::app_apis().empty()) {
        return Context::skip("no D3D11 or Vulkan present path is available");
    }
    std::wstring instance;
    std::string version_before;
    for (const DisplayDevice& device : DisplayDevices::enumerate(true)) {
        if (device.accel) {
            instance = device.instance_id;
            version_before = device.driver_version;
            break;
        }
    }
    if (instance.empty()) {
        return Outcome::fail("the accel-virt display device is not enumerated");
    }
    iso::Fleet apps;
    start_apps(apps, context, 30);
    iso::Scene::sleep(5000);
    DWORD code = 0;
    Problems problems;
    if (!Kit::run(L"pnputil.exe /restart-device \"" + instance + L"\"", code, 120000) || code != 0) {
        problems.add("pnputil /restart-device failed with exit code " + std::to_string(code));
    }
    apps.join();
    Kit::judge_apps(problems, apps, "across the driver restart, application");
    iso::Scene::forward(context, apps, "");
    std::string version_after;
    bool started = false;
    for (const DisplayDevice& device : DisplayDevices::enumerate(true)) {
        if (device.accel) {
            version_after = device.driver_version;
            started = device.started;
        }
    }
    context.measure("driver_version_before", version_before);
    context.measure("driver_version_after", version_after);
    if (!started) {
        problems.add("the display device is not started after the driver restart");
    }
    FreshProcess::check(context, problems, context.id(), "driver_restart");
    return problems.outcome();
}

Outcome LifeChecks::session_logoff_logon(Context& context) {
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    const std::string before = adapter_keys();
    const std::wstring reconnect = L"schtasks.exe /Create /TN lsw-suites-reconnect /TR \"tscon.exe " + std::to_wstring(session) + L" /dest:console\" /SC ONCE /ST 23:59 /RU SYSTEM /RL HIGHEST /F";
    DWORD code = 0;
    if (!Kit::run(reconnect, code) || code != 0) {
        return Context::skip("a SYSTEM task to reconnect the session cannot be created (schtasks exit " + std::to_string(code) + ")");
    }
    Problems problems;
    if (!Kit::run(L"tsdiscon.exe", code) || code != 0) {
        static_cast<void>(Kit::run(L"schtasks.exe /Delete /TN lsw-suites-reconnect /F", code));
        return Context::skip("the session cannot be disconnected (tsdiscon exit " + std::to_string(code) + ")");
    }
    ::Sleep(3000);
    static_cast<void>(Kit::run(L"schtasks.exe /Run /TN lsw-suites-reconnect", code));
    ::Sleep(8000);
    static_cast<void>(Kit::run(L"schtasks.exe /Delete /TN lsw-suites-reconnect /F", code));
    const std::string after = adapter_keys();
    if (before != after) {
        problems.add("the adapters and outputs changed across the disconnect: [" + before + "] became [" + after + "]");
    }
    if (::GetSystemMetrics(SM_CXSCREEN) <= 0 || ::GetSystemMetrics(SM_CYSCREEN) <= 0) {
        problems.add("the desktop has no size after the session was reconnected");
    }
    FreshProcess::check(context, problems, context.id(), "session_reconnect");
    return problems.outcome();
}

Outcome LifeChecks::display_output_off_on(Context& context) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS || path_count == 0) {
        return Outcome::fail("GetDisplayConfigBufferSizes returned no active path");
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
        return Outcome::fail("QueryDisplayConfig failed");
    }
    paths.resize(path_count);
    modes.resize(mode_count);
    const std::vector<DISPLAYCONFIG_PATH_INFO> saved_paths = paths;
    const std::vector<DISPLAYCONFIG_MODE_INFO> saved_modes = modes;
    Marker marker;
    std::string error;
    const COLORREF color = RGB(200, 60, 140);
    if (!marker.open(color, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    paths[0].flags &= ~static_cast<UINT32>(DISPLAYCONFIG_PATH_ACTIVE);
    const LONG off = ::SetDisplayConfig(path_count, paths.data(), mode_count, modes.data(), SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_NO_OPTIMIZATION);
    context.measure("off_result", static_cast<long long>(off));
    ::Sleep(1500);
    const LONG on = ::SetDisplayConfig(static_cast<UINT32>(saved_paths.size()), const_cast<DISPLAYCONFIG_PATH_INFO*>(saved_paths.data()), static_cast<UINT32>(saved_modes.size()),
                                       const_cast<DISPLAYCONFIG_MODE_INFO*>(saved_modes.data()), SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_NO_OPTIMIZATION);
    context.measure("on_result", static_cast<long long>(on));
    if (on != ERROR_SUCCESS) {
        problems.add("restoring the saved display configuration returned " + std::to_string(on));
    }
    marker.paint(color);
    double ms = 0;
    if (!Kit::desktop_shows(marker.area, color, 15000, ms)) {
        problems.add("the desktop does not show the marker window within 15 s after the output was switched off and on");
    }
    context.measure("recomposed_ms", ms);
    DWM_TIMING_INFO first;
    if (RefreshRate::dwm_timing(first)) {
        ::Sleep(8000);
        DWM_TIMING_INFO later;
        const DEVMODEW stable = [] {
            DEVMODEW mode{};
            static_cast<void>(Kit::current_mode(mode));
            return mode;
        }();
        if (!RefreshRate::dwm_timing(later) || later.cRefresh < first.cRefresh) {
            problems.add("the refresh counter went backwards or stopped in the 8 s after recovery, which indicates the output cycling again");
        }
        DEVMODEW again{};
        if (!Kit::current_mode(again) || again.dmPelsWidth != stable.dmPelsWidth || again.dmPelsHeight != stable.dmPelsHeight) {
            problems.add("the display mode changed by itself after recovery");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::life
