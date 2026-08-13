#pragma once

#include <cstdint>

namespace lsw::protocol {

// Frame header for the Vulkan command-stream protocol (ICD <-> render
// server). Unrelated to the manager<->agent link, which has its own
// framing in lsw/linkproto — see docs/link-protocol.md.
//
// Layout (little-endian, packed):
//   u32 cmd_id
//   u32 payload_len
//   u8  flags
//   u8  reserved[3]
//   u64 reply_token   (only present when FrameFlags::needs_reply set)
//
// Followed by `payload_len` bytes of payload.
struct FrameHeader {
    std::uint32_t cmd_id;
    std::uint32_t payload_len;
    std::uint8_t flags;
    std::uint8_t reserved[3];
};

static_assert(sizeof(FrameHeader) == 12, "FrameHeader must be 12 bytes on the wire");

}  // namespace lsw::protocol
