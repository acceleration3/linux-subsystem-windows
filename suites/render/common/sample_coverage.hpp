#pragma once

#include "render/common/image.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lsw::render {
struct CoverageRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::uint32_t rgba = 0;
};

class SampleCoverage {
public:
    using Position = std::array<int, 2>;

    [[nodiscard]] static Image resolve_rects(std::uint32_t width, std::uint32_t height, std::uint32_t clear_rgba, const std::vector<CoverageRect>& rects,
                                             const std::vector<Position>& positions, unsigned samples) {
        Image image(width, height, PixelKind::Rgba8Unorm);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const unsigned pixel = (x & 1u) + 2u * (y & 1u);
                unsigned sum[4] = {0, 0, 0, 0};
                for (unsigned s = 0; s < samples; ++s) {
                    const Position& at = positions[pixel * samples + s];
                    const double sx = static_cast<double>(x) + 0.5 + static_cast<double>(at[0]) / 16.0;
                    const double sy = static_cast<double>(y) + 0.5 + static_cast<double>(at[1]) / 16.0;
                    std::uint32_t value = clear_rgba;
                    for (const CoverageRect& rect : rects) {
                        if (sx >= snap(rect.x0) && sx < snap(rect.x1) && sy >= snap(rect.y0) && sy < snap(rect.y1)) {
                            value = rect.rgba;
                        }
                    }
                    for (unsigned c = 0; c < 4; ++c) {
                        sum[c] += (value >> (8 * c)) & 255u;
                    }
                }
                std::uint32_t out = 0;
                for (unsigned c = 0; c < 4; ++c) {
                    const auto mean = static_cast<std::uint32_t>(std::lround(static_cast<double>(sum[c]) / static_cast<double>(samples)));
                    out |= mean << (8 * c);
                }
                image.set_rgba8(x, y, out);
            }
        }
        return image;
    }

private:
    [[nodiscard]] static double snap(float v) { return std::nearbyint(static_cast<double>(v) * 256.0) / 256.0; }
};
}  // namespace lsw::render
