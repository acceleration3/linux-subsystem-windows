#pragma once

#include <string>

namespace lsw::agent::win {

// Pulls an application icon out of the guest and hands it back as a
// base64 PNG, ready to drop into an RPC reply.
//
// Two sources, because Windows has two: the icon resource compiled into
// an .exe, and the icon a live window advertises via WM_GETICON /
// GCLP_HICON (which apps often set to something better).
class IconExtractor {
public:
    IconExtractor() = delete;

    // Largest icon resource in ``path``, as base64 PNG. Empty on failure.
    [[nodiscard]] static std::string from_executable(const std::wstring& path);

    // Icon the live window ``hwnd`` advertises, as base64 PNG. Empty on
    // failure. ``hwnd`` is an HWND, kept as void* so this header does not
    // drag in windows.h.
    [[nodiscard]] static std::string from_window(void* hwnd);
};

}  // namespace lsw::agent::win
