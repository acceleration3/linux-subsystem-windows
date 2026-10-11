#include "lsw/agent/win/display_scale.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/system/scale_steps.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <vector>

namespace lsw::agent::win {

namespace {

// The DPI scale requests of DisplayConfigGetDeviceInfo and
// DisplayConfigSetDeviceInfo. Not in the SDK: they are what the Settings
// app sends, and the layouts below are the ones it uses. -3 and -4 as the
// 32-bit word the header carries; the enum's type here is unsigned.
constexpr auto kGetDpiScale = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFDu);
constexpr auto kSetDpiScale = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFCu);

struct GetDpiScale {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    std::int32_t min_relative;  // 100 %, as steps from the recommended scale
    std::int32_t current_relative;
    std::int32_t max_relative;
};

struct SetDpiScale {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    std::int32_t relative;
};

// The active displays. The configuration can change between asking how
// big it is and reading it; then it is asked again.
std::vector<DISPLAYCONFIG_PATH_INFO> active_paths() {
    for (;;) {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
            ERROR_SUCCESS) {
            return {};
        }
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const LONG read = ::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                                               &mode_count, modes.data(), nullptr);
        if (read == ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        if (read != ERROR_SUCCESS) {
            LSW_AGENT_WARN("display scale: QueryDisplayConfig failed: {}", read);
            return {};
        }
        paths.resize(path_count);
        return paths;
    }
}

}  // namespace

DisplayScale::Outcome DisplayScale::apply(int percent) {
    Outcome outcome;
    for (const DISPLAYCONFIG_PATH_INFO& path : active_paths()) {
        GetDpiScale current{};
        current.header.type = kGetDpiScale;
        current.header.size = sizeof(current);
        current.header.adapterId = path.sourceInfo.adapterId;
        current.header.id = path.sourceInfo.id;
        if (const LONG got = ::DisplayConfigGetDeviceInfo(&current.header); got != ERROR_SUCCESS) {
            LSW_AGENT_WARN("display scale: source {} has no scale to read: {}", path.sourceInfo.id,
                           got);
            continue;
        }
        const system::ScaleSteps steps{-current.min_relative, current.max_relative};
        const int relative = steps.relative_for(percent);
        if (relative != current.current_relative) {
            SetDpiScale wanted{};
            wanted.header.type = kSetDpiScale;
            wanted.header.size = sizeof(wanted);
            wanted.header.adapterId = path.sourceInfo.adapterId;
            wanted.header.id = path.sourceInfo.id;
            wanted.relative = relative;
            if (const LONG set = ::DisplayConfigSetDeviceInfo(&wanted.header); set != ERROR_SUCCESS) {
                LSW_AGENT_WARN("display scale: source {} refused {} %: {}", path.sourceInfo.id,
                               steps.percent_at(relative), set);
                continue;
            }
        }
        ++outcome.displays;
        outcome.percent = steps.percent_at(relative);
        LSW_AGENT_INFO("display scale: source {} at {} % (asked {} %)", path.sourceInfo.id,
                       outcome.percent, percent);
    }
    return outcome;
}

}  // namespace lsw::agent::win
