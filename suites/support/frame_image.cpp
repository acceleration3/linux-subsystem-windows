#include "support/frame_image.hpp"

namespace lsw::suites {

std::uint32_t FrameImage::color(int frame, int quadrant) {
    const std::uint32_t r = static_cast<std::uint32_t>((frame % 8) * 30 + quadrant * 6 + 12);
    const std::uint32_t g = static_cast<std::uint32_t>(((frame / 8) % 8) * 30 + quadrant * 6 + 12);
    const std::uint32_t b = static_cast<std::uint32_t>((frame * 29 + quadrant * 61 + 70) & 255);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

RECT FrameImage::quadrant(int width, int height, int index) {
    const int left = (index & 1) != 0 ? width / 2 : 0;
    const int top = (index & 2) != 0 ? height / 2 : 0;
    return {left, top, (index & 1) != 0 ? width : width / 2, (index & 2) != 0 ? height : height / 2};
}

std::uint32_t FrameImage::at(int frame, int width, int height, int x, int y) {
    return color(frame, (x >= width / 2 ? 1 : 0) + (y >= height / 2 ? 2 : 0));
}

std::vector<std::uint32_t> FrameImage::render(int frame, int width, int height) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = at(frame, width, height, x, y);
        }
    }
    return out;
}

std::vector<std::uint32_t> FrameImage::gradient_checker(int width, int height, int phase) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t r = width > 1 ? static_cast<std::uint32_t>(x * 255 / (width - 1)) : 128u;
            const std::uint32_t g = height > 1 ? static_cast<std::uint32_t>(y * 255 / (height - 1)) : 128u;
            const std::uint32_t b = (((x / 4) + (y / 4) + phase) & 1) != 0 ? 0xC4u : 0x3Bu;
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    return out;
}

}  // namespace lsw::suites
