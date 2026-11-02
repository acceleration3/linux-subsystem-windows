#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsw::agent::util {
class Base64 {
public:
    Base64() = delete;

    [[nodiscard]] static std::string encode(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] static std::vector<std::uint8_t> decode(std::string_view text);
};

}
