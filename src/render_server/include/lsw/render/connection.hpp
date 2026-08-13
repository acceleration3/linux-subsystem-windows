#pragma once

#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/frame_dispatcher.hpp"
#include "lsw/protocol/frame_io.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

namespace lsw::render {

// One accepted connection. Reader and replay threads share a bounded
// ring: the reader drains the channel, the replay thread dispatches into
// the FrameDispatcher and writes replies. Single-stream — replies go back
// on the channel they arrived on, from the replay thread.
//
// The connection holds a ``FrameChannel``, not a file descriptor. It has
// never cared which transport it was on; until B2 it simply had no way
// to say so, and the type it named made a shared-memory peer — which has
// no fd at all — impossible to represent.
class Connection {
public:
    static constexpr std::size_t kRingCapacity = 64;

    Connection() noexcept = default;
    explicit Connection(std::unique_ptr<::lsw::protocol::FrameChannel> channel) noexcept;
    ~Connection() noexcept;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    [[nodiscard]] bool is_open() const noexcept {
        return channel_ != nullptr && channel_->is_open();
    }

    // Spawn a reader thread, run the replay loop on the calling thread,
    // join on disconnect / protocol error / BYE. Closes the channel
    // before returning.
    void run(::lsw::protocol::FrameDispatcher& dispatcher);

    // Ask ``run`` to return, from another thread. Needed because a
    // shared-memory channel has no blocking read to interrupt with a
    // signal — the reader is polling a ring and has to be told to stop.
    void request_stop() noexcept;

    void close() noexcept;

private:
    void reader_loop();
    void replay_loop(::lsw::protocol::FrameDispatcher& dispatcher);

    std::unique_ptr<::lsw::protocol::FrameChannel> channel_;

    std::mutex ring_mutex_;
    std::condition_variable ring_cv_;
    std::deque<::lsw::protocol::Frame> ring_;
    std::atomic<bool> stop_{false};
};

}  // namespace lsw::render
