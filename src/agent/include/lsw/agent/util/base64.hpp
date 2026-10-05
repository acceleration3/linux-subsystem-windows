#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsw::agent::util {

// Standard base64 (RFC 4648, with padding). Used for every binary blob
// that crosses the JSON-line RPC channel — icons, PNG frames, console
// output.
//
// Its own class because it has nothing to do with Windows: the icon
// extractor merely happens to be its first caller.
class Base64 {
public:
    Base64() = delete;

    [[nodiscard]] static std::string encode(const std::vector<std::uint8_t>& bytes);
    // Strict RFC 4648 decoding; malformed input throws invalid_argument.
    [[nodiscard]] static std::vector<std::uint8_t> decode(std::string_view text);
};

}  // namespace lsw::agent::util
