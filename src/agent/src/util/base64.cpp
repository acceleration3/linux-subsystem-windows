#include "lsw/agent/util/base64.hpp"

namespace lsw::agent::util {

std::string Base64::encode(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16)
                                  | (std::uint32_t(data[i + 1]) << 8) | std::uint32_t(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < data.size()) {
        std::uint32_t v = std::uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) {
            v |= std::uint32_t(data[i + 1]) << 8;
        }
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

}  // namespace lsw::agent::util
