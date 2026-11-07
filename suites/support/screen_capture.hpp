#pragma once

#include "support/bitmap.hpp"

#include <windows.h>

#include <string>

namespace lsw::suites {
class ScreenCapture {
public:
    static constexpr UINT kRenderFullContent = 0x00000002;

    [[nodiscard]] static bool screen(const RECT& area, bool cursor, Bitmap& out, std::string& error);

    [[nodiscard]] static bool window_client(HWND window, Bitmap& out, std::string& error);

    [[nodiscard]] static bool print_window(HWND window, UINT flags, Bitmap& out, std::string& error);

    [[nodiscard]] static RECT client_on_screen(HWND window);

    [[nodiscard]] static bool cursor_state(POINT& position, POINT& hotspot, HCURSOR& handle);

    static void draw_cursor(HDC dc, int origin_x, int origin_y);

    [[nodiscard]] static bool matches(const Bitmap& got, const std::vector<std::uint32_t>& expected, int tolerance, std::string& where);

    [[nodiscard]] static bool matches_region(const Bitmap& got, int origin_x, int origin_y, int width, int height,
                                             const std::vector<std::uint32_t>& expected, int tolerance, std::string& where);

    [[nodiscard]] static std::string rgb_text(std::uint32_t value);
};
}  // namespace lsw::suites
