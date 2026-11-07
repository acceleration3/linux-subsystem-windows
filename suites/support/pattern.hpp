#pragma once

#include <cstddef>
#include <cstdint>

namespace lsw::suites {
class Pattern {
public:
    [[nodiscard]] static std::uint8_t byte(std::uint64_t seed, std::uint64_t offset);

    static void fill(std::uint64_t seed, std::uint64_t offset, void* destination, std::size_t size);

    [[nodiscard]] static std::uint32_t pixel(std::uint64_t seed, std::uint32_t x, std::uint32_t y);

    static void fill_pixels(std::uint64_t seed, std::uint32_t width, std::uint32_t height,
                            std::uint32_t stride, void* destination);

private:
    [[nodiscard]] static std::uint64_t mix(std::uint64_t value);
};
}  // namespace lsw::suites
