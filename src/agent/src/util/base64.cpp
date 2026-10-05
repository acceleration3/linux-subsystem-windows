#include "lsw/agent/util/base64.hpp"

#include <stdexcept>

namespace lsw::agent::util {

std::vector<std::uint8_t> Base64::decode(std::string_view text) {
    if (text.size() % 4 != 0)
        throw std::invalid_argument("invalid base64 length");
    const auto digit = [](char c) -> unsigned {
        if (c >= 'A' && c <= 'Z')
            return static_cast<unsigned>(c - 'A');
        if (c >= 'a' && c <= 'z')
            return static_cast<unsigned>(c - 'a') + 26;
        if (c >= '0' && c <= '9')
            return static_cast<unsigned>(c - '0') + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        throw std::invalid_argument("invalid base64 character");
    };
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const unsigned a = digit(text[i]);
        const unsigned b = digit(text[i + 1]);
        const bool pad2 = text[i + 2] == '=';
        const bool pad3 = text[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != text.size()))
            throw std::invalid_argument("invalid base64 padding");
        const unsigned c = pad2 ? 0 : digit(text[i + 2]);
        const unsigned d = pad3 ? 0 : digit(text[i + 3]);
        if ((pad2 && (b & 15U)) || (pad3 && !pad2 && (c & 3U)))
            throw std::invalid_argument("non-canonical base64 padding");
        const unsigned value = (a << 18) | (b << 12) | (c << 6) | d;
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        if (!pad2)
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        if (!pad3)
            out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

std::string Base64::encode(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) |
                                std::uint32_t(data[i + 2]);
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
