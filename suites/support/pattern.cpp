#include "support/pattern.hpp"

namespace lsw::suites {

std::uint64_t Pattern::mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

std::uint8_t Pattern::byte(std::uint64_t seed, std::uint64_t offset) {
    const std::uint64_t word = mix(seed * 0x2545f4914f6cdd1dull + (offset >> 3));
    return static_cast<std::uint8_t>(word >> ((offset & 7) * 8));
}

void Pattern::fill(std::uint64_t seed, std::uint64_t offset, void* destination, std::size_t size) {
    auto* out = static_cast<std::uint8_t*>(destination);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = byte(seed, offset + i);
    }
}

std::uint32_t Pattern::pixel(std::uint64_t seed, std::uint32_t x, std::uint32_t y) {
    const std::uint64_t word = mix(seed * 0x2545f4914f6cdd1dull + (static_cast<std::uint64_t>(y) << 32) + x);
    return static_cast<std::uint32_t>(word) | 0xff000000u;
}

void Pattern::fill_pixels(std::uint64_t seed, std::uint32_t width, std::uint32_t height,
                          std::uint32_t stride, void* destination) {
    auto* rows = static_cast<std::uint8_t*>(destination);
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(rows + static_cast<std::size_t>(y) * stride);
        for (std::uint32_t x = 0; x < width; ++x) {
            row[x] = pixel(seed, x, y);
        }
    }
}

}  // namespace lsw::suites
