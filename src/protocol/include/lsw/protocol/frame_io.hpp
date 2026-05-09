#pragma once

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace lsw::protocol {

enum class IoResult : std::int32_t {
    Ok = 0,
    Disconnected = -1,
    InvalidFrame = -2,
    Timeout = -3,
};

// In-memory representation of a wire frame. ``reply_token`` is meaningful
// iff ``flags & FrameFlags::needs_reply``.
struct Frame {
    std::uint32_t cmd_id = 0;
    std::uint8_t flags = 0;
    std::uint64_t reply_token = 0;
    std::vector<std::uint8_t> payload;
};

// Synchronous, blocking frame I/O over a stream-oriented file descriptor.
// One source of truth for the wire format documented in docs/protocol.md.
//
// Does NOT own the fd — caller manages lifetime. The ``shutdown_read``
// helper exists to unblock a reader thread parked in ``recv``.
class FrameStream {
public:
    FrameStream() noexcept = default;

    explicit FrameStream(int fd) noexcept : fd_(fd) {}

    [[nodiscard]] int fd() const noexcept { return fd_; }

    void set_fd(int fd) noexcept { fd_ = fd; }

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    // Read one inbound request frame off the wire. The ``needs_reply``
    // flag determines whether a reply token follows the header.
    [[nodiscard]] IoResult read_frame(Frame& out) noexcept;

    // Read a reply frame. Replies always carry a reply_token regardless
    // of flag bits — see docs/protocol.md. ``flags & 1`` indicates server
    // dispatch error.
    [[nodiscard]] IoResult read_reply(Frame& out) noexcept;

    // Write one frame onto the wire.
    [[nodiscard]] IoResult write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                       std::uint64_t reply_token,
                                       std::span<const std::uint8_t> payload) noexcept;

    // Convenience: write the canonical reply for an inbound frame
    // ``request``: same cmd_id and reply_token, no flags.
    [[nodiscard]] IoResult write_reply(const Frame& request, std::span<const std::uint8_t> payload,
                                       bool error = false) noexcept;

    // Half-close the read side without closing the fd; unblocks a reader
    // parked in ::read. Safe to call from another thread.
    void shutdown_read() noexcept;

private:
    int fd_ = -1;
};

}  // namespace lsw::protocol
