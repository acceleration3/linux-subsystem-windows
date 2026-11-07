#include "sanity/env/display_checks.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/problems.hpp"

// clang-format off
#include <windows.h>
#include <shellscalingapi.h>
// clang-format on

#include <algorithm>
#include <set>

namespace lsw::suites::sanity::env {

namespace {

void enable_dpi_awareness() {
    ::SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
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
    enable_dpi_awareness();
    const std::vector<MonitorInfoEx> list = monitors();
    if (list.empty()) {
        return Outcome::fail("EnumDisplayMonitors reports no monitor");
    }
    Problems problems;
    for (const MonitorInfoEx& entry : list) {
        const std::string name = Text::utf8(entry.info.szDevice);
        UINT first_x = 0;
        UINT first_y = 0;
        if (FAILED(::GetDpiForMonitor(entry.monitor, MDT_EFFECTIVE_DPI, &first_x, &first_y))) {
            problems.add(name + ": GetDpiForMonitor fails");
            continue;
        }
        DEVICE_SCALE_FACTOR scale = DEVICE_SCALE_FACTOR_INVALID;
        if (FAILED(::GetScaleFactorForMonitor(entry.monitor, &scale)) ||
            scale == DEVICE_SCALE_FACTOR_INVALID) {
            problems.add(name + ": GetScaleFactorForMonitor fails");
            continue;
        }
        context.measure(name + ".dpi", first_x);
        context.measure(name + ".scale_percent", static_cast<int>(scale));
        const int dpi_percent = static_cast<int>((first_x * 100u + 48u) / 96u);
        if (first_x != first_y) {
            problems.add(name + ": effective DPI differs between axes");
        }
        if (std::abs(dpi_percent - static_cast<int>(scale)) > 1) {
            problems.add(name + ": effective DPI " + std::to_string(first_x) + " is " +
                         std::to_string(dpi_percent) + "% but the scale factor is " +
                         std::to_string(static_cast<int>(scale)) + "%");
        }
        for (int repeat = 0; repeat < 10; ++repeat) {
            UINT x = 0;
            UINT y = 0;
            DEVICE_SCALE_FACTOR again = DEVICE_SCALE_FACTOR_INVALID;
            if (FAILED(::GetDpiForMonitor(entry.monitor, MDT_EFFECTIVE_DPI, &x, &y)) ||
                FAILED(::GetScaleFactorForMonitor(entry.monitor, &again)) || x != first_x ||
                y != first_y || again != scale) {
                problems.add(name + ": DPI or scale factor changes between calls");
                break;
            }
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::env
