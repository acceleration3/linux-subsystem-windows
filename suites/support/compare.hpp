#pragma once

#include "support/mismatch.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lsw::suites {
class Compare {
public:
    [[nodiscard]] static std::optional<Mismatch> bytes(const void* expected, const void* actual,
                                                       std::size_t size);

    [[nodiscard]] static std::optional<Mismatch> pixels(const void* expected, const void* actual,
                                                        std::uint32_t width, std::uint32_t height,
                                                        std::uint32_t expected_stride,
                                                        std::uint32_t actual_stride);
};
}  // namespace lsw::suites
