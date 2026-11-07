#include "sanity/env/display_checks.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/problems.hpp"
#include "support/test_window.hpp"

// clang-format off
#include <windows.h>
#include <shellscalingapi.h>
// clang-format on

#include <algorithm>
#include <map>
#include <set>

namespace lsw::suites::sanity::env {

namespace {

void enable_dpi_awareness() {
    using SetContext = BOOL(WINAPI*)(HANDLE);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    void* address = user32 != nullptr ? reinterpret_cast<void*>(::GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
                                      : nullptr;
    auto set_context = reinterpret_cast<SetContext>(address);
    if (set_context == nullptr || set_context(reinterpret_cast<HANDLE>(-4)) == FALSE) {
        ::SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }
}

std::set<std::string> gdi_outputs() {
    std::set<std::string> out;
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (::EnumDisplayDevicesW(nullptr, index, &device, 0) == FALSE) {
            break;
        }
        if ((device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0 &&
            (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) == 0) {
            out.insert(Text::utf8(Text::upper(std::wstring(device.DeviceName))));
        }
    }
    return out;
}

std::set<std::string> dxgi_outputs(std::string& error) {
    std::set<std::string> out;
    std::vector<AvAdapter> adapters;
    if (!AvAdapters::all(adapters, error)) {
        return out;
    }
    for (const AvAdapter& adapter : adapters) {
        for (const AvOutput& output : adapter.outputs) {
            if (output.attached) {
                out.insert(Text::upper(output.device_name));
            }
        }
    }
    return out;
}

std::string join(const std::set<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += name + " ";
    }
    return out;
}

struct MonitorInfoEx {
    HMONITOR monitor;
    MONITORINFOEXW info;
};

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MonitorInfoEx entry{};
    entry.monitor = monitor;
    entry.info.cbSize = sizeof(entry.info);
    if (::GetMonitorInfoW(monitor, &entry.info) != FALSE) {
        reinterpret_cast<std::vector<MonitorInfoEx>*>(param)->push_back(entry);
    }
    return TRUE;
}

std::vector<MonitorInfoEx> monitors() {
    std::vector<MonitorInfoEx> out;
    ::EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&out));
    return out;
}

std::map<std::wstring, int> configured_scales() {
    constexpr int kPercents[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};
    struct GetScale {
        DISPLAYCONFIG_DEVICE_INFO_HEADER header;
        std::int32_t min_relative;
        std::int32_t current_relative;
        std::int32_t max_relative;
    };
    std::map<std::wstring, int> out;
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
        return out;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(),
                             nullptr) != ERROR_SUCCESS) {
        return out;
    }
    paths.resize(path_count);
    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        GetScale scale{};
        scale.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFDu);
        scale.header.size = sizeof(scale);
        scale.header.adapterId = path.sourceInfo.adapterId;
        scale.header.id = path.sourceInfo.id;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (::DisplayConfigGetDeviceInfo(&scale.header) != ERROR_SUCCESS ||
            ::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }
        const int index = std::clamp(-scale.min_relative + scale.current_relative, 0, 11);
        out[Text::upper(std::wstring(source.viewGdiDeviceName))] = kPercents[index];
    }
    return out;
}

}  // namespace

Outcome DisplayChecks::output_present(Context& context) {
    const std::set<std::string> gdi = gdi_outputs();
    std::string error;
    const std::set<std::string> dxgi = dxgi_outputs(error);
    if (!error.empty()) {
        return Outcome::fail(error);
    }
    context.measure("gdi.outputs", join(gdi));
    context.measure("dxgi.outputs", join(dxgi));
    Problems problems;
    if (gdi.empty()) {
        problems.add("EnumDisplayDevices reports no attached output");
    }
    if (dxgi.empty()) {
        problems.add("DXGI EnumOutputs reports no attached output");
    }
    if (gdi != dxgi) {
        problems.add("EnumDisplayDevices lists [" + join(gdi) + "] but DXGI lists [" + join(dxgi) + "]");
    }
    return problems.outcome();
}

Outcome DisplayChecks::output_active_mode(Context& context) {
    const std::set<std::string> names = gdi_outputs();
    if (names.empty()) {
        return Outcome::fail("EnumDisplayDevices reports no attached output");
    }
    Problems problems;
    for (const std::string& name : names) {
        const std::wstring wide = Text::wide(name);
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (::EnumDisplaySettingsW(wide.c_str(), ENUM_CURRENT_SETTINGS, &current) == FALSE) {
            problems.add(name + ": EnumDisplaySettings(ENUM_CURRENT_SETTINGS) fails");
            continue;
        }
        context.measure(name + ".mode", std::to_string(current.dmPelsWidth) + "x" +
                                            std::to_string(current.dmPelsHeight) + "x" +
                                            std::to_string(current.dmBitsPerPel) + "@" +
                                            std::to_string(current.dmDisplayFrequency));
        if (current.dmPelsWidth == 0 || current.dmPelsHeight == 0 || current.dmBitsPerPel == 0 ||
            current.dmDisplayFrequency == 0) {
            problems.add(name + ": current mode has a zero width, height, depth or frequency");
            continue;
        }
        bool listed = false;
        std::size_t modes = 0;
        for (DWORD index = 0;; ++index) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (::EnumDisplaySettingsW(wide.c_str(), index, &mode) == FALSE) {
                break;
            }
            ++modes;
            listed = listed || (mode.dmPelsWidth == current.dmPelsWidth &&
                                mode.dmPelsHeight == current.dmPelsHeight &&
                                mode.dmBitsPerPel == current.dmBitsPerPel &&
                                mode.dmDisplayFrequency == current.dmDisplayFrequency);
        }
        context.measure(name + ".modes", modes);
        if (!listed) {
            problems.add(name + ": the current mode is not in the enumerated mode list");
        }
    }
    return problems.outcome();
}

Outcome DisplayChecks::desktop_rect_consistent(Context& context) {
    enable_dpi_awareness();
    const std::vector<MonitorInfoEx> list = monitors();
    if (list.empty()) {
        return Outcome::fail("EnumDisplayMonitors reports no monitor");
    }
    RECT bounds = list.front().info.rcMonitor;
    Problems problems;
    for (const MonitorInfoEx& entry : list) {
        const RECT& m = entry.info.rcMonitor;
        const RECT& w = entry.info.rcWork;
        bounds.left = std::min(bounds.left, m.left);
        bounds.top = std::min(bounds.top, m.top);
        bounds.right = std::max(bounds.right, m.right);
        bounds.bottom = std::max(bounds.bottom, m.bottom);
        if (w.left < m.left || w.top < m.top || w.right > m.right || w.bottom > m.bottom) {
            problems.add(Text::utf8(entry.info.szDevice) + ": work area lies outside the monitor rectangle");
        }
    }
    const int vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    context.measure("virtual.rect", std::to_string(vx) + "," + std::to_string(vy) + " " +
                                        std::to_string(vw) + "x" + std::to_string(vh));
    context.measure("monitors.union", std::to_string(bounds.left) + "," + std::to_string(bounds.top) + " " +
                                          std::to_string(bounds.right - bounds.left) + "x" +
                                          std::to_string(bounds.bottom - bounds.top));
    if (vx != bounds.left || vy != bounds.top || vw != bounds.right - bounds.left ||
        vh != bounds.bottom - bounds.top) {
        problems.add("virtual screen metrics differ from the union of the monitor rectangles");
    }
    return problems.outcome();
}

Outcome DisplayChecks::dpi_scaling_consistent(Context& context) {
    for (const MonitorInfoEx& entry : monitors()) {
        UINT x = 0;
        UINT y = 0;
        DEVICE_SCALE_FACTOR scale = DEVICE_SCALE_FACTOR_INVALID;
        if (SUCCEEDED(::GetDpiForMonitor(entry.monitor, MDT_EFFECTIVE_DPI, &x, &y))) {
            context.measure("initial." + Text::utf8(entry.info.szDevice) + ".dpi", x);
        }
        if (SUCCEEDED(::GetScaleFactorForMonitor(entry.monitor, &scale))) {
            context.measure("initial." + Text::utf8(entry.info.szDevice) + ".scale_percent", static_cast<int>(scale));
        }
    }
    enable_dpi_awareness();
    const std::vector<MonitorInfoEx> list = monitors();
    if (list.empty()) {
        return Outcome::fail("EnumDisplayMonitors reports no monitor");
    }
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    using ForWindow = UINT(WINAPI*)(HWND);
    using MetricsForDpi = int(WINAPI*)(int, UINT);
    auto dpi_for_window = reinterpret_cast<ForWindow>(
        reinterpret_cast<void*>(::GetProcAddress(user32, "GetDpiForWindow")));
    auto metrics_for_dpi = reinterpret_cast<MetricsForDpi>(
        reinterpret_cast<void*>(::GetProcAddress(user32, "GetSystemMetricsForDpi")));
    if (dpi_for_window == nullptr || metrics_for_dpi == nullptr) {
        return Outcome::fail("user32 lacks GetDpiForWindow or GetSystemMetricsForDpi");
    }
    const std::map<std::wstring, int> configured = configured_scales();
    Problems problems;
    for (const MonitorInfoEx& entry : list) {
        const std::string name = Text::utf8(entry.info.szDevice);
        UINT dpi_x = 0;
        UINT dpi_y = 0;
        if (FAILED(::GetDpiForMonitor(entry.monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
            problems.add(name + ": GetDpiForMonitor fails");
            continue;
        }
        DEVICE_SCALE_FACTOR shell = DEVICE_SCALE_FACTOR_INVALID;
        if (SUCCEEDED(::GetScaleFactorForMonitor(entry.monitor, &shell))) {
            context.measure(name + ".shell_scale_percent", static_cast<int>(shell));
        }
        context.measure(name + ".dpi", dpi_x);
        if (dpi_x != dpi_y) {
            problems.add(name + ": effective DPI differs between axes");
        }
        TestWindowConfig config;
        config.x = entry.info.rcMonitor.left + 40;
        config.y = entry.info.rcMonitor.top + 40;
        TestWindow window;
        std::string error;
        if (!window.create(config, error)) {
            problems.add(name + ": " + error);
        } else {
            const UINT window_dpi = dpi_for_window(window.hwnd());
            context.measure(name + ".window_dpi", window_dpi);
            if (window_dpi != dpi_x) {
                problems.add(name + ": GetDpiForWindow " + std::to_string(window_dpi) + " differs from the monitor's effective DPI " +
                             std::to_string(dpi_x));
            }
        }
        const int scrollbar_96 = metrics_for_dpi(SM_CXVSCROLL, 96);
        const int scrollbar = metrics_for_dpi(SM_CXVSCROLL, dpi_x);
        const int expected = ::MulDiv(scrollbar_96, static_cast<int>(dpi_x), 96);
        context.measure(name + ".scrollbar_width", scrollbar);
        if (std::abs(scrollbar - expected) > 1) {
            problems.add(name + ": GetSystemMetricsForDpi(SM_CXVSCROLL) is " + std::to_string(scrollbar) +
                         " at " + std::to_string(dpi_x) + " DPI, expected " + std::to_string(expected));
        }
        const auto found = configured.find(Text::upper(std::wstring(entry.info.szDevice)));
        if (found == configured.end()) {
            problems.add(name + ": the configured display scale cannot be read");
            continue;
        }
        context.measure(name + ".configured_percent", found->second);
        const int effective_percent = static_cast<int>((dpi_x * 100u + 48u) / 96u);
        if (effective_percent != found->second) {
            problems.add(name + ": effective scale " + std::to_string(effective_percent) + "% differs from the configured " +
                         std::to_string(found->second) + "%");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::env
