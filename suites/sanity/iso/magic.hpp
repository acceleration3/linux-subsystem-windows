#pragma once

#include <cstddef>
#include <cstdint>

namespace lsw::suites::sanity::iso {
class Magic {
public:
    static constexpr std::uint64_t kHigh = 0xC0DEC0DEull;

    static void fill(std::uint8_t* data, std::size_t size, std::uint64_t first_word);

    [[nodiscard]] static std::uint64_t count(const std::uint8_t* data, std::size_t size);
};
}  // namespace lsw::suites::sanity::iso
