#pragma once

#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/transport.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace lsw::protocol {

// The client side of a ``FrameChannel``, for callers that think in
// requests and replies.
//
// This is where A1 is actually fixed. ``UnixSocketTransport`` matched a
// reply to its request by position: write, then read whatever comes
// back. With one transport per ``VkInstance`` and an application that
// records on more than one thread — every D3D translation layer does —
// two callers interleave their bytes on the way out and race for each
// other's answers on the way back. Serialising the whole round trip
// makes that correct and makes it slow, because a sync call then blocks
// every other thread for a full latency.
//
// So replies are matched by token instead:
//
//   * a short lock around the write, held for one ``write_frame`` and
//     nothing else — an async send takes only this
//   * a monotonic token per sync call, in a ``token -> waiter`` map
//   * one reader thread, which is the only thread that ever reads the
//     channel, handing each reply to the waiter that asked for it
//   * each caller parked on its own condition variable
//
// N threads therefore have N calls in flight and contend only for the
// instant it takes to push bytes into the channel.
//
// The same class serves both transports, which is the point of B3: the
// socket is a ``FdFrameChannel``, shared memory is a
// ``ShmemFrameChannel``, and there is no third implementation of this
// logic to keep in step.
class ChannelTransport final : public Transport {
public:
    ChannelTransport() = default;

    // Takes ownership of ``channel`` and starts the reader thread.
    explicit ChannelTransport(std::unique_ptr<FrameChannel> channel);

    ~ChannelTransport() override;

    ChannelTransport(const ChannelTransport&) = delete;
    ChannelTransport& operator=(const ChannelTransport&) = delete;
    ChannelTransport(ChannelTransport&&) = delete;
    ChannelTransport& operator=(ChannelTransport&&) = delete;

    // Adopt ``channel`` and start reading. Any previous channel is closed
    // first. Returns 0, or -EINVAL for a null or already-closed channel.
    [[nodiscard]] int adopt(std::unique_ptr<FrameChannel> channel);

    [[nodiscard]] bool is_open() const noexcept;

    // Stop the reader, fail every waiter, close the channel. Idempotent,
    // and safe to call from a thread other than the one in ``send_sync``.
    void close() noexcept;

    [[nodiscard]] std::string description() const;

    // The region the underlying channel lives in, or null for a socket.
    // Callers use this to decide whether memory can be shared with the
    // peer rather than copied to it.
    [[nodiscard]] std::shared_ptr<SharedRegion> backing_region() const {
        return channel_ == nullptr ? nullptr : channel_->backing_region();
    }

    [[nodiscard]] TransportResult send_sync(std::uint32_t cmd_id,
                                            std::span<const std::uint8_t> payload,
                                            Buffer* reply_out) override;

    [[nodiscard]] TransportResult send_async(std::uint32_t cmd_id,
                                             std::span<const std::uint8_t> payload) override;

    // Replies that arrived with no waiter expecting them. Non-zero means
    // a token was reused, dropped, or answered twice — worth asserting in
    // tests, because the symptom otherwise is a hang somewhere else.
    [[nodiscard]] std::uint64_t orphan_replies() const noexcept {
        return orphan_replies_.load();
    }

private:
    // One in-flight sync call. Lives on the caller's stack and is
    // reachable by the reader only while it is in ``waiters_``, which is
    // guarded by ``waiters_mutex_`` — so the reader can never touch a
    // frame that has already returned.
    struct Waiter {
        Buffer* reply_out = nullptr;
        std::condition_variable cv;
        bool done = false;
        TransportResult result = TransportResult::Ok;
    };

    void reader_loop();
    void fail_all_waiters(TransportResult reason) noexcept;

    std::unique_ptr<FrameChannel> channel_;

    // Held for exactly one write. Never held while waiting for a reply —
    // that is the whole difference from the lock this replaces.
    std::mutex write_mutex_;

    std::mutex waiters_mutex_;
    std::unordered_map<std::uint64_t, Waiter*> waiters_;

    std::atomic<std::uint64_t> next_token_{1};
    std::atomic<std::uint64_t> orphan_replies_{0};
    std::atomic<bool> stopping_{false};
    // Set by the reader on its way out, BEFORE it fails the waiters it
    // can see. A caller that registers in that window would otherwise
    // park on a condition variable nobody will ever notify; the wait
    // predicate reads this first, so it never blocks at all.
    std::atomic<bool> reader_done_{false};
    std::thread reader_;
};

}  // namespace lsw::protocol
