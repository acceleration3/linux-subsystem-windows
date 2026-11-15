#include "sanity/iso/magic.hpp"

#include <cstring>

namespace lsw::suites::sanity::iso {

void Magic::fill(std::uint8_t* data, std::size_t size, std::uint64_t first_word) {
    for (std::size_t word = 0; word * 8 + 8 <= size; ++word) {
        const std::uint64_t value = (kHigh << 32) | ((first_word + word) & 0xffffffffull);
        std::memcpy(data + word * 8, &value, 8);
    }
}

std::uint64_t Magic::count(const std::uint8_t* data, std::size_t size) {
    std::uint64_t found = 0;
    for (std::size_t word = 0; word * 8 + 8 <= size; ++word) {
        std::uint64_t value = 0;
        std::memcpy(&value, data + word * 8, 8);
        if ((value >> 32) == kHigh) {
            ++found;
        }
    }
    return found;
}

}  // namespace lsw::suites::sanity::iso
