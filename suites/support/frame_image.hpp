#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace lsw::suites {
class FrameImage {
public:
    static constexpr int kFrames = 64;

    [[nodiscard]] static std::uint32_t color(int frame, int quadrant);

    [[nodiscard]] static RECT quadrant(int width, int height, int quadrant);

    [[nodiscard]] static std::uint32_t at(int frame, int width, int height, int x, int y);

    [[nodiscard]] static std::vector<std::uint32_t> render(int frame, int width, int height);

    [[nodiscard]] static std::uint32_t swap_red_blue(std::uint32_t value) {
        return (value & 0xFF00FF00u) | ((value & 0xFFu) << 16) | ((value >> 16) & 0xFFu);
    }

    [[nodiscard]] static int centre_x(const RECT& rect) { return (rect.left + rect.right) / 2; }

    [[nodiscard]] static int centre_y(const RECT& rect) { return (rect.top + rect.bottom) / 2; }
};
}  // namespace lsw::suites
