#include "lsw/agent/win/display_resolution.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace lsw::agent::win {

namespace {
constexpr ULONGLONG kReplugWaitMs = 2000;
constexpr DWORD kReplugStepMs = 20;

DEVMODEW blank_mode() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    return mode;
}

}  // namespace

std::optional<DisplayResolution::Resolution> DisplayResolution::current() {
    DEVMODEW mode = blank_mode();
    if (::EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) == FALSE) {
        return std::nullopt;
    }
    return Resolution{static_cast<int>(mode.dmPelsWidth), static_cast<int>(mode.dmPelsHeight)};
}

bool DisplayResolution::offered(const Resolution& wanted) {
    DEVMODEW mode = blank_mode();
    for (DWORD index = 0; ::EnumDisplaySettingsW(nullptr, index, &mode) != FALSE; ++index) {
        if (static_cast<int>(mode.dmPelsWidth) == wanted.width &&
            static_cast<int>(mode.dmPelsHeight) == wanted.height) {
            return true;
        }
    }
    return false;
}

bool DisplayResolution::apply(const Resolution& wanted) {
    const ULONGLONG deadline = ::GetTickCount64() + kReplugWaitMs;
    while (!offered(wanted)) {
        if (::GetTickCount64() >= deadline) {
            LSW_AGENT_WARN("display resolution: the display does not offer {}x{}", wanted.width,
                           wanted.height);
            return false;
        }
        ::Sleep(kReplugStepMs);
    }
    if (current() == wanted) {
        return true;
    }
    DEVMODEW mode = blank_mode();
    mode.dmPelsWidth = static_cast<DWORD>(wanted.width);
    mode.dmPelsHeight = static_cast<DWORD>(wanted.height);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    const LONG changed = ::ChangeDisplaySettingsExW(nullptr, &mode, nullptr, 0, nullptr);
    if (changed != DISP_CHANGE_SUCCESSFUL) {
        LSW_AGENT_WARN("display resolution: Windows refused {}x{}: {}", wanted.width, wanted.height,
                       changed);
        return false;
    }
    return true;
}

}  // namespace lsw::agent::win
