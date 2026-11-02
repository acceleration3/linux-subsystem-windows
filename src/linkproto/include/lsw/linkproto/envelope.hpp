#pragma once

#include "lsw/linkproto/byte_order.hpp"

#include <cstdint>

namespace lsw::linkproto {

enum class FrameType : std::uint8_t {
    Control = 1,
};

inline constexpr std::size_t kHeaderSize = 1 + 4;

inline constexpr std::uint16_t kAgentTcpPort = 5102;

inline void write_header(std::uint8_t* out, FrameType type,
                         std::uint32_t payload_len) noexcept {
    out[0] = static_cast<std::uint8_t>(type);
    write_u32_be(out + 1, payload_len);
}

[[nodiscard]] inline std::uint32_t read_payload_len(const std::uint8_t* header) noexcept {
    return read_u32_be(header + 1);
}

[[nodiscard]] inline FrameType read_type(const std::uint8_t* header) noexcept {
    return static_cast<FrameType>(header[0]);
}

}
