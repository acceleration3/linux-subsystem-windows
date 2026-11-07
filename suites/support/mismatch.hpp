#pragma once

#include <cstdint>
#include <string>

namespace lsw::suites {
struct Mismatch {
    std::uint64_t offset = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t expected = 0;
    std::uint32_t actual = 0;
    bool pixel = false;

    [[nodiscard]] std::string describe() const;
};
}  // namespace lsw::suites
