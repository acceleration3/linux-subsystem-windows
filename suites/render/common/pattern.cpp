#include "render/common/pattern.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::render {

Pattern::Bytes Pattern::checker(unsigned width, unsigned height, unsigned cell, std::uint32_t first, std::uint32_t second) {
    Bytes out(static_cast<std::size_t>(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const std::uint32_t value = (((x / cell) + (y / cell)) & 1u) != 0 ? second : first;
            std::memcpy(out.data() + (static_cast<std::size_t>(y) * width + x) * 4, &value, 4);
        }
    }
    return out;
}

Pattern::Bytes Pattern::gradient(unsigned width, unsigned height) {
    Bytes out(static_cast<std::size_t>(width) * height * 4);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const std::uint32_t value = pack(x * 255 / std::max(1u, width - 1), y * 255 / std::max(1u, height - 1),
                                             (x + y) * 255 / std::max(1u, width + height - 2), 255);
            std::memcpy(out.data() + (static_cast<std::size_t>(y) * width + x) * 4, &value, 4);
        }
    }
    return out;
}

float Pattern::unit_random(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

Pattern::Bytes Pattern::noise(unsigned width, unsigned height, std::uint32_t seed) {
    Bytes out(static_cast<std::size_t>(width) * height * 4);
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < out.size(); i += 4) {
        out[i] = static_cast<std::uint8_t>(unit_random(state) * 255.0f);
        out[i + 1] = static_cast<std::uint8_t>(unit_random(state) * 255.0f);
        out[i + 2] = static_cast<std::uint8_t>(unit_random(state) * 255.0f);
        out[i + 3] = 255;
    }
    return out;
}

unsigned Pattern::level_count(unsigned width, unsigned height) {
    unsigned levels = 1;
    unsigned size = std::max(width, height);
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

std::vector<Pattern::Bytes> Pattern::mip_chain(const Bytes& base, unsigned width, unsigned height, unsigned levels) {
    std::vector<Bytes> chain;
    chain.push_back(base);
    unsigned w = width;
    unsigned h = height;
    for (unsigned level = 1; level < levels; ++level) {
        const unsigned nw = std::max(1u, w / 2);
        const unsigned nh = std::max(1u, h / 2);
        Bytes next(static_cast<std::size_t>(nw) * nh * 4);
        const Bytes& prev = chain.back();
        for (unsigned y = 0; y < nh; ++y) {
            for (unsigned x = 0; x < nw; ++x) {
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (unsigned dy = 0; dy < 2; ++dy) {
                        for (unsigned dx = 0; dx < 2; ++dx) {
                            const unsigned sx = std::min(w - 1, x * 2 + dx);
                            const unsigned sy = std::min(h - 1, y * 2 + dy);
                            sum += prev[(static_cast<std::size_t>(sy) * w + sx) * 4 + c];
                        }
                    }
                    next[(static_cast<std::size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        chain.push_back(std::move(next));
        w = nw;
        h = nh;
    }
    return chain;
}

}  // namespace lsw::render
