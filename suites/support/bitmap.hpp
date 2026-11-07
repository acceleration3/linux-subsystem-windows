#pragma once

#include <cstdint>
#include <vector>

namespace lsw::suites {
struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] std::uint32_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] & 0x00FFFFFFu;
    }

    [[nodiscard]] bool valid(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
};
}  // namespace lsw::suites
