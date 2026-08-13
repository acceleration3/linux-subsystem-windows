#pragma once

#include <cstdint>

namespace lsw::protocol {

// Reserved cmd_id values.
inline constexpr std::uint32_t kCmdHello = 0x00000001;
inline constexpr std::uint32_t kCmdHelloAck = 0x00000002;
inline constexpr std::uint32_t kCmdPing = 0x00000003;
inline constexpr std::uint32_t kCmdPong = 0x00000004;
inline constexpr std::uint32_t kCmdAsyncError = 0x00000005;
// Bind a surface to a window the CLIENT already owns. Only meaningful
// when client and server share an X display, i.e. the local loopback
// harness — a guest has no window on the host's display.
inline constexpr std::uint32_t kCmdCreateXcbSurface = 0x00000010;
inline constexpr std::uint32_t kCmdDestroySurface = 0x00000011;
// Ask the SERVER to create a window and give back a surface for it.
// This is the real path: the host owns every window an app presents to,
// because that is the whole point — a guest application's pixels land in
// a host window. The client passes only a size and a title.
inline constexpr std::uint32_t kCmdCreateHostSurface = 0x00000012;
// Memory shadow (Phase 1, no shared memory). The client allocates a
// guest-side buffer on vkMapMemory, then ships dirty bytes upstream
// via kCmdMemoryWrite on unmap / vkFlushMappedMemoryRanges, or pulls
// fresh bytes down via kCmdMemoryRead on vkInvalidateMappedMemoryRanges.
inline constexpr std::uint32_t kCmdMemoryWrite = 0x00000020;
inline constexpr std::uint32_t kCmdMemoryRead = 0x00000021;
// One command buffer's recorded ``vkCmd*`` stream, shipped as a single
// message at ``vkEndCommandBuffer``.
//
// A command buffer is externally synchronised, so exactly one thread
// owns it while recording — which makes the accumulation buffer purely
// thread-local and the hot path lock-free. Thousands of tiny writes
// become one large one, which is what the ring prefers and what stops N
// recording threads from serialising on the transport.
//
// Payload:
//     [count:u32]  then ``count`` x  [cmd_id:u32][len:u32][payload:len]
//
// Sub-commands are replayed in order and none of them expects a reply —
// only async commands are ever batched.
inline constexpr std::uint32_t kCmdBatch = 0x00000030;
// Allocate device memory backed by bytes the client has already placed
// in the region's ``hostmem`` arena, so the server imports the pointer
// instead of being sent copies of it (VK_EXT_external_memory_host).
//
// Request: [device:u64][size:u64][memory_type_index:u32][arena_offset:u64]
// Reply:   [result:i32][memory:u64][imported:u8]
//
// ``imported == 0`` is an ORDINARY answer, not an error: the driver
// lacks the extension, or the host pointer does not meet its
// ``minImportedHostPointerAlignment``, or the region is not shared with
// this peer at all. The client then allocates the normal way and uses
// the copy path. Which of those it was is the server's business and
// never crosses the wire — that is what keeps the decision out of the
// client, out of the layout, and off any particular machine.
inline constexpr std::uint32_t kCmdAllocateImported = 0x00000031;
inline constexpr std::uint32_t kCmdBye = 0xFFFFFFFFu;

// Magic value sent in HELLO. ASCII 'LSW\0'.
inline constexpr std::uint32_t kProtocolMagic = 0x0057534Cu;

// Protocol version. Bump major on breaking change; minor on additive.
inline constexpr std::uint16_t kProtocolMajor = 1;
inline constexpr std::uint16_t kProtocolMinor = 0;

}  // namespace lsw::protocol
