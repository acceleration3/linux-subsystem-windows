#pragma once

#include "lsw/protocol/doorbell.hpp"
#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/shared_region.hpp"
#include "lsw/protocol/sub_region.hpp"

#include "lsw/linkproto/byte_ring.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::protocol {

// Frames over a duplex pair of rings in shared memory.
//
// Layout of the region handed to ``bind``: two ``linkproto`` byte rings
// back to back, first half client→server, second half server→client.
// Which half this end writes to is the only thing ``Role`` decides —
// the two ends are otherwise the same code.
//
// A frame is one ring record: [cmd_id:u32][flags:u8][reply_token:u64]
// then the payload. No length prefix of its own — the ring already
// frames records, and a reader can never see half of one.
//
// There is no connection state, because there is no connection: the
// region either exists or it does not. ``shutdown_read`` sets a flag the
// reader checks, which is how a server thread is stopped.
class ShmemFrameChannel final : public FrameChannel {
public:
    enum class Role : std::uint8_t {
        Client,  // writes the first ring, reads the second
        Server,  // the mirror image
    };

    ShmemFrameChannel() = default;

    // Bind to ``region``, which must already contain initialised rings
    // (``init_region`` does that). Borrows both region and doorbell —
    // neither is owned. Returns 0, or -EINVAL if the rings do not look
    // initialised.
    [[nodiscard]] int bind(SharedRegion& region, Role role, Doorbell& doorbell);

    // Bind to the channel of ``channel_kind`` inside ``region``, holding
    // the region mapped for as long as this channel lives.
    //
    // This is the entry point both peers use, and the one place the
    // "locate a channel by kind, never by offset" rule is enforced in
    // this module: the manager may move or resize any channel by
    // rewriting one table entry, and neither end notices.
    //
    // With no ``doorbell`` the channel uses its own ``SpinPollDoorbell``,
    // which is the implementation that works in both directions
    // everywhere. The result is self-contained — a listener can hand one
    // out and forget about it.
    [[nodiscard]] int bind_channel(std::shared_ptr<SharedRegion> region,
                                   std::uint32_t channel_kind, Role role,
                                   Doorbell* doorbell = nullptr);

    // Lay out and zero the rings in a fresh region. Called once by
    // whichever side creates it (locally the server; for a VM, the
    // manager). Returns 0 or -EINVAL when the region is too small.
    [[nodiscard]] static int init_region(SharedRegion& region);

    // How long a read waits before reporting Timeout. Default is
    // effectively forever, which is what a server's reader thread wants;
    // a client waiting on a reply sets something finite so a dead peer
    // surfaces as an error rather than a hang.
    void set_read_timeout(std::chrono::milliseconds timeout) noexcept {
        read_timeout_ = timeout;
    }

    // How long a write retries a full ring before reporting Timeout.
    //
    // A full ring is back-pressure, not an error: the peer is behind, and
    // the producer refuses records whole, so retrying is safe. It has to
    // block rather than drop, because a dropped ``vkCmd*`` is not a lost
    // packet — it is a command buffer that renders something else.
    void set_write_timeout(std::chrono::milliseconds timeout) noexcept {
        write_timeout_ = timeout;
    }

    // Largest payload a single frame may carry, set by the ring's
    // capacity. Anything above it can never be sent and is rejected
    // outright rather than retried to the timeout.
    [[nodiscard]] std::uint32_t max_payload() const noexcept;

    // Region bytes needed for a channel whose rings can each carry
    // ``payload_bytes`` in one frame. Callers sizing a region backwards
    // from a message size want this.
    [[nodiscard]] static std::uint64_t region_bytes_for_payload(std::uint32_t payload_bytes);

    [[nodiscard]] bool is_open() const noexcept override { return bound_ && !closed_.load(); }

    [[nodiscard]] IoResult read_frame(Frame& out) noexcept override;
    [[nodiscard]] IoResult read_reply(Frame& out) noexcept override;
    [[nodiscard]] IoResult write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                       std::uint64_t reply_token,
                                       std::span<const std::uint8_t> payload) noexcept override;
    [[nodiscard]] IoResult write_reply(const Frame& request, std::span<const std::uint8_t> payload,
                                       bool error = false) noexcept override;
    void shutdown_read() noexcept override;
    void close() noexcept override;

    // Non-null only when bound via ``bind_channel``, which is the case
    // that owns its mapping. See FrameChannel::backing_region.
    [[nodiscard]] std::shared_ptr<SharedRegion> backing_region() const override {
        return owned_region_;
    }

    [[nodiscard]] std::string description() const override { return description_; }

private:
    [[nodiscard]] IoResult read_any(Frame& out) noexcept;
    [[nodiscard]] IoResult write_record(std::uint32_t cmd_id, std::uint8_t flags,
                                        std::uint64_t reply_token,
                                        std::span<const std::uint8_t> payload) noexcept;

    // Set only by ``bind_channel``: the mapping and the window into it
    // that this channel is a view of. ``bind`` leaves both empty and
    // borrows instead, which is what the tests and the parity checks
    // want.
    std::shared_ptr<SharedRegion> owned_region_;
    SubRegion window_;
    SpinPollDoorbell own_doorbell_;

    linkproto::ByteRingProducer out_;
    linkproto::ByteRingConsumer in_;
    Doorbell* doorbell_ = nullptr;
    bool bound_ = false;
    std::atomic<bool> closed_{false};
    std::atomic<bool> read_shutdown_{false};
    // Bumped on every write. The reader watches it to know when to stop
    // backing off: a message just went out, so a reply is imminent.
    //
    // Without this the backoff is blind. A reader that has been quiet
    // between frames has decayed to its longest slice, so the first
    // reply of the next frame waits up to that long — on BOTH legs of
    // every synchronous call, because neither direction has an
    // interrupt the other can rely on. That is latency the ring itself
    // never causes: measured against a real guest, the ring sat empty
    // at 275 KiB/s while vkcube managed 46 fps.
    std::atomic<std::uint64_t> writes_{0};
    // Reader-thread only: when this channel last carried anything.
    // Decides the fast/slow poll tier.
    std::chrono::steady_clock::time_point last_activity_{};
    std::chrono::milliseconds read_timeout_{std::chrono::hours{24}};
    std::chrono::milliseconds write_timeout_{std::chrono::seconds{10}};
    std::string description_ = "shm://(unbound)";
    // Reader and writer each get their own staging buffer: the interface
    // permits one thread in each at the same time, so sharing one would
    // be a data race in the class rather than in its callers.
    std::vector<std::uint8_t> read_scratch_;
    std::vector<std::uint8_t> write_scratch_;
};

}  // namespace lsw::protocol
