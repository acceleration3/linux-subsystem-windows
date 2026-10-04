#pragma once

#include "lsw/linkproto/byte_order.hpp"

#include <cstdint>

// The agent<->manager envelope. Normative description: docs/link-protocol.md.
//
//   [type:u8][len:u32 big-endian][payload:len bytes]
//
// Length-prefixing (rather than newline framing) is what lets binary
// payloads share the connection with JSON without escaping.

namespace lsw::linkproto {

enum class FrameType : std::uint8_t {
    // JSON control message: request, reply or event.
    Control = 1,
};

inline constexpr std::size_t kHeaderSize = 1 + 4;  // type + length

// The guest-side listener the envelope travels to, reached from the host
// through QEMU's user-mode ``hostfwd``. The domain XML template forwards
// the same port; keep the two together.
inline constexpr std::uint16_t kAgentTcpPort = 5102;

// Write the 5-byte header into ``out``, which must have room for
// ``kHeaderSize`` bytes.
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

}  // namespace lsw::linkproto
