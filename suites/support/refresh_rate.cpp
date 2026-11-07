#include "support/refresh_rate.hpp"

#include <windows.h>

#include <algorithm>

namespace lsw::suites {

bool RefreshRate::dwm_timing(DWM_TIMING_INFO& out) {
    out = {};
    out.cbSize = sizeof(out);
    return SUCCEEDED(::DwmGetCompositionTimingInfo(nullptr, &out));
}

double RefreshRate::settings_hz() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) != FALSE && mode.dmDisplayFrequency > 1) {
        return static_cast<double>(mode.dmDisplayFrequency);
    }
    return 0.0;
}

double RefreshRate::hz() {
    DWM_TIMING_INFO timing;
    if (dwm_timing(timing) && timing.rateRefresh.uiDenominator != 0 && timing.rateRefresh.uiNumerator != 0) {
        return static_cast<double>(timing.rateRefresh.uiNumerator) / static_cast<double>(timing.rateRefresh.uiDenominator);
    }
    const double settings = settings_hz();
    return settings > 0.0 ? settings : 60.0;
}

std::vector<int> RefreshRate::mode_rates() {
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    std::vector<int> rates;
    if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current) == FALSE) {
        return rates;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; ::EnumDisplaySettingsW(nullptr, i, &mode) != FALSE; ++i) {
        if (mode.dmPelsWidth == current.dmPelsWidth && mode.dmPelsHeight == current.dmPelsHeight && mode.dmBitsPerPel == current.dmBitsPerPel &&
            mode.dmDisplayFrequency > 1) {
            rates.push_back(static_cast<int>(mode.dmDisplayFrequency));
        }
    }
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
    return rates;
}

}  // namespace lsw::suites
