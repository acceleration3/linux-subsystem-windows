#pragma once

#include "lsw/protocol/frame_dispatcher.hpp"
#include "lsw/protocol/frame_io.hpp"
#include "lsw/protocol/listener.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>

namespace lsw::server {

// One accepted connection. Reader and replay threads share a bounded ring
// per docs/architecture.md: the reader drains the wire, the replay thread
// dispatches into the FrameDispatcher and writes replies. Single-stream
// for Phase 2 — replies are written from the replay thread on the same
// fd. Phase 5 splits cmd / reply / bulk.
class Connection {
public:
    static constexpr std::size_t kRingCapacity = 64;

    Connection() noexcept = default;
    explicit Connection(::lsw::protocol::AcceptedFd accepted) noexcept;
    ~Connection() noexcept;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return io_.is_open(); }

    [[nodiscard]] int fd() const noexcept { return io_.fd(); }

    // Spawn a reader thread, run the replay loop on the calling thread,
    // join on disconnect / protocol error / BYE. Closes the fd before
    // returning.
    void run(::lsw::protocol::FrameDispatcher& dispatcher);

    void close() noexcept;

private:
    void reader_loop();
    void replay_loop(::lsw::protocol::FrameDispatcher& dispatcher);

    ::lsw::protocol::FrameStream io_;

    std::mutex ring_mutex_;
    std::condition_variable ring_cv_;
    std::deque<::lsw::protocol::Frame> ring_;
    std::atomic<bool> stop_{false};
};

}  // namespace lsw::server
