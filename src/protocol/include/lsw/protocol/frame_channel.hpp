#pragma once

#include "lsw/protocol/io_result.hpp"
#include "lsw/protocol/shared_region.hpp"
#include "lsw/protocol/wire_frame.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace lsw::protocol {

// A bidirectional stream of frames, however it is carried.
//
// This is the seam that makes "local" and "in a VM" the same code. Above
// it, nothing — not the ICD, not the render server, not the dispatch
// chain — knows whether frames move through a socket or through pages
// two processes both have mapped. Below it there are exactly two
// implementations:
//
//   FdFrameChannel     a stream socket (unix locally; the old TCP path)
//   ShmemFrameChannel  a duplex pair of rings in shared memory
//
// and the shared-memory one is identical whether the peer is another
// process on this machine (POSIX shm) or a guest (the same pages exposed
// through an IVSHMEM BAR). Only the mapping differs, and that is
// ``SharedRegion``'s problem, not this interface's.
//
// Blocking and synchronous. One thread may read, one may write; callers
// that share a channel across threads serialise themselves.
class FrameChannel {
public:
    virtual ~FrameChannel() = default;

    FrameChannel() = default;
    FrameChannel(const FrameChannel&) = delete;
    FrameChannel& operator=(const FrameChannel&) = delete;
    FrameChannel(FrameChannel&&) = delete;
    FrameChannel& operator=(FrameChannel&&) = delete;

    [[nodiscard]] virtual bool is_open() const noexcept = 0;

    // Read one inbound request frame.
    [[nodiscard]] virtual IoResult read_frame(Frame& out) noexcept = 0;

    // Read a reply frame. Replies always carry a reply_token regardless
    // of flag bits; ``flags & 1`` indicates a server dispatch error.
    [[nodiscard]] virtual IoResult read_reply(Frame& out) noexcept = 0;

    [[nodiscard]] virtual IoResult write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                               std::uint64_t reply_token,
                                               std::span<const std::uint8_t> payload) noexcept = 0;

    // The canonical reply to ``request``: same cmd_id and reply_token.
    [[nodiscard]] virtual IoResult write_reply(const Frame& request,
                                               std::span<const std::uint8_t> payload,
                                               bool error = false) noexcept = 0;

    // Unblock a reader parked in ``read_frame`` without tearing the
    // channel down. Safe to call from another thread — it is how a
    // server stops a connection thread.
    virtual void shutdown_read() noexcept = 0;

    virtual void close() noexcept = 0;

    // The region this channel lives in, when it lives in one.
    //
    // Null for a socket, and that is the whole meaning of it: memory can
    // only be shared rather than copied when both peers have the same
    // pages mapped, and this is how a caller asks whether that is the
    // case. Phase G's import path is enabled by a non-null answer here
    // and by nothing else — not by a build flag, not by knowing whether
    // there is a VM.
    [[nodiscard]] virtual std::shared_ptr<SharedRegion> backing_region() const { return nullptr; }

    // For logs only.
    [[nodiscard]] virtual std::string description() const = 0;
};

}  // namespace lsw::protocol
