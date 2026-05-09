#pragma once

#include <cstdint>

namespace lsw::protocol {

// Wire-protocol frame header. See docs/protocol.md.
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

namespace FrameFlags {
inline constexpr std::uint8_t needs_reply = 1u << 0;
inline constexpr std::uint8_t has_bulk_ref = 1u << 1;
inline constexpr std::uint8_t trace = 1u << 2;
}  // namespace FrameFlags

// Reserved cmd_id values.
inline constexpr std::uint32_t kCmdHello = 0x00000001;
inline constexpr std::uint32_t kCmdHelloAck = 0x00000002;
inline constexpr std::uint32_t kCmdPing = 0x00000003;
inline constexpr std::uint32_t kCmdPong = 0x00000004;
inline constexpr std::uint32_t kCmdAsyncError = 0x00000005;
inline constexpr std::uint32_t kCmdCreateXcbSurface = 0x00000010;
inline constexpr std::uint32_t kCmdDestroySurface = 0x00000011;
// Memory shadow (Phase 1, no shared memory). The client allocates a
// guest-side buffer on vkMapMemory, then ships dirty bytes upstream
// via kCmdMemoryWrite on unmap / vkFlushMappedMemoryRanges, or pulls
// fresh bytes down via kCmdMemoryRead on vkInvalidateMappedMemoryRanges.
inline constexpr std::uint32_t kCmdMemoryWrite = 0x00000020;
inline constexpr std::uint32_t kCmdMemoryRead = 0x00000021;
inline constexpr std::uint32_t kCmdBye = 0xFFFFFFFFu;

// Magic value sent in HELLO. ASCII 'LSW\0'.
inline constexpr std::uint32_t kProtocolMagic = 0x0057534Cu;

// Protocol version. Bump major on breaking change; minor on additive.
inline constexpr std::uint16_t kProtocolMajor = 1;
inline constexpr std::uint16_t kProtocolMinor = 0;

}  // namespace lsw::protocol
