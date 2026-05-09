#include "lsw/server/connection.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <thread>
#include <unistd.h>
#include <utility>

namespace lsw::server {

Connection::Connection(::lsw::protocol::AcceptedFd accepted) noexcept : io_(accepted.fd) {}

Connection::~Connection() noexcept {
    close();
}

Connection::Connection(Connection&& other) noexcept : io_(other.io_.fd()) {
    other.io_.set_fd(-1);
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        io_.set_fd(other.io_.fd());
        other.io_.set_fd(-1);
    }
    return *this;
}

void Connection::reader_loop() {
    using ::lsw::protocol::Frame;
    using ::lsw::protocol::IoResult;

    while (!stop_.load(std::memory_order_acquire)) {
        Frame f;
        if (io_.read_frame(f) != IoResult::Ok) {
            break;
        }
        {
            std::unique_lock lk(ring_mutex_);
            ring_cv_.wait(lk, [this] {
                return ring_.size() < kRingCapacity || stop_.load(std::memory_order_acquire);
            });
            if (stop_.load(std::memory_order_acquire)) {
                break;
            }
            ring_.push_back(std::move(f));
        }
        ring_cv_.notify_one();
    }
    stop_.store(true, std::memory_order_release);
    ring_cv_.notify_all();
}

void Connection::replay_loop(::lsw::protocol::FrameDispatcher& dispatcher) {
    using ::lsw::protocol::Buffer;
    using ::lsw::protocol::Frame;
    using ::lsw::protocol::IoResult;
    using ::lsw::protocol::kCmdBye;
    namespace FrameFlags = ::lsw::protocol::FrameFlags;

    while (true) {
        Frame f;
        {
            std::unique_lock lk(ring_mutex_);
            ring_cv_.wait(
                lk, [this] { return !ring_.empty() || stop_.load(std::memory_order_acquire); });
            if (ring_.empty()) {
                break;
            }
            f = std::move(ring_.front());
            ring_.pop_front();
        }
        ring_cv_.notify_one();

        Buffer reply;
        const bool ok = dispatcher.dispatch(
            f.cmd_id, std::span<const std::uint8_t>{f.payload.data(), f.payload.size()}, reply);

        if ((f.flags & FrameFlags::needs_reply) != 0) {
            if (io_.write_reply(f, reply.bytes(), /*error=*/!ok) != IoResult::Ok) {
                break;
            }
        }

        if (!ok || f.cmd_id == kCmdBye) {
            break;
        }
    }
    stop_.store(true, std::memory_order_release);
    ring_cv_.notify_all();
}

void Connection::run(::lsw::protocol::FrameDispatcher& dispatcher) {
    if (!is_open()) {
        return;
    }
    stop_.store(false, std::memory_order_release);
    std::thread reader([this] { reader_loop(); });
    replay_loop(dispatcher);
    // Unblock the reader if it's parked in ::read.
    io_.shutdown_read();
    reader.join();
    close();
}

void Connection::close() noexcept {
    if (io_.is_open()) {
        ::close(io_.fd());
        io_.set_fd(-1);
    }
}

}  // namespace lsw::server
