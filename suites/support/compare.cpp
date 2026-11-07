#include "support/compare.hpp"

#include <cstring>

namespace lsw::suites {

std::optional<Mismatch> Compare::bytes(const void* expected, const void* actual, std::size_t size) {
    const auto* a = static_cast<const std::uint8_t*>(expected);
    const auto* b = static_cast<const std::uint8_t*>(actual);
    if (std::memcmp(a, b, size) == 0) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            Mismatch mismatch;
            mismatch.offset = i;
            mismatch.expected = a[i];
            mismatch.actual = b[i];
            return mismatch;
        }
    }
    return std::nullopt;
}

std::optional<Mismatch> Compare::pixels(const void* expected, const void* actual,
                                        std::uint32_t width, std::uint32_t height,
                                        std::uint32_t expected_stride, std::uint32_t actual_stride) {
    const auto* a = static_cast<const std::uint8_t*>(expected);
    const auto* b = static_cast<const std::uint8_t*>(actual);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* row_a = reinterpret_cast<const std::uint32_t*>(a + static_cast<std::size_t>(y) * expected_stride);
        const auto* row_b = reinterpret_cast<const std::uint32_t*>(b + static_cast<std::size_t>(y) * actual_stride);
        for (std::uint32_t x = 0; x < width; ++x) {
            if (row_a[x] != row_b[x]) {
                Mismatch mismatch;
                mismatch.pixel = true;
                mismatch.x = x;
                mismatch.y = y;
                mismatch.expected = row_a[x];
                mismatch.actual = row_b[x];
                return mismatch;
            }
        }
    }
    return std::nullopt;
}

}  // namespace lsw::suites
