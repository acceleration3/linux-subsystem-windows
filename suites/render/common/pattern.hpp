#pragma once

#include <cstdint>
#include <vector>

namespace lsw::render {
class Pattern {
public:
    using Bytes = std::vector<std::uint8_t>;

    [[nodiscard]] static std::uint32_t pack(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    [[nodiscard]] static Bytes checker(unsigned width, unsigned height, unsigned cell, std::uint32_t first, std::uint32_t second);

    [[nodiscard]] static Bytes gradient(unsigned width, unsigned height);

    [[nodiscard]] static Bytes noise(unsigned width, unsigned height, std::uint32_t seed);

    [[nodiscard]] static std::vector<Bytes> mip_chain(const Bytes& base, unsigned width, unsigned height, unsigned levels);

    [[nodiscard]] static unsigned level_count(unsigned width, unsigned height);

    [[nodiscard]] static float unit_random(std::uint32_t& state);
};
}  // namespace lsw::render
