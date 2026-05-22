#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::gdi {

// Process-wide GDI+ startup. Idempotent — safe to call from any
// thread / any time. The first call wins; teardown on process exit.
void ensure_gdiplus();

// Standard base64 alphabet, no padding gotchas. ASCII-clean output.
[[nodiscard]] std::string base64_encode(const std::vector<std::uint8_t>& bytes);

// Pull the large (32x32) icon associated with ``path`` and return it
// PNG-encoded + base64. Empty string on any failure (no icon, file
// missing, GDI+ encoder missing, etc.). Used by the app-catalog to
// ship per-app icons up to the manager.
[[nodiscard]] std::string extract_icon_png_base64(const std::wstring& path);

// Pull the icon for a live HWND (WM_GETICON ICON_BIG → ICON_SMALL2 →
// ICON_SMALL → class GCLP_HICON → class GCLP_HICONSM → executable
// fallback) and return it PNG-encoded + base64. Used by the manager
// to give each mirror window the guest window's actual icon so the
// Linux taskbar shows it instead of the generic Qt icon.
[[nodiscard]] std::string extract_window_icon_png_base64(void* hwnd);

}  // namespace lsw::agent::gdi
