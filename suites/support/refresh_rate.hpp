#pragma once

#include <dwmapi.h>

#include <vector>

namespace lsw::suites {
class RefreshRate {
public:
    [[nodiscard]] static bool dwm_timing(DWM_TIMING_INFO& out);

    [[nodiscard]] static double hz();

    [[nodiscard]] static double period_ms() { return 1000.0 / hz(); }

    [[nodiscard]] static double settings_hz();

    [[nodiscard]] static std::vector<int> mode_rates();
};
}  // namespace lsw::suites
