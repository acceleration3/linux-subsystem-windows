#include "lsw/render/connection.hpp"

#include "lsw/protocol/buffer.hpp"
#include "lsw/protocol/frame.hpp"

#include <thread>
#include <utility>

namespace lsw::render {

Connection::Connection(std::unique_ptr<::lsw::protocol::FrameChannel> channel) noexcept
    : channel_(std::move(channel)) {}

Connection::~Connection() noexcept {
    close();
}

Connection::Connection(Connection&& other) noexcept : channel_(std::move(other.channel_)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        channel_ = std::move(other.channel_);
    }
    return *this;
}

void Connection::reader_loop() {
    using ::lsw::protocol::Frame;
    using ::lsw::protocol::IoResult;

    while (!stop_.load(std::memory_order_acquire)) {
        Frame f;
        const IoResult r = channel_->read_frame(f);
        if (r == IoResult::Timeout) {
            // A polling channel reporting "nothing yet" is not a
            // disconnect; a socket never says this at all.
            continue;
        }
        if (r != IoResult::Ok) {
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

        // BYE is the transport's business, not a handler's. Letting it
        // reach the chain means every handler passes on it, the chain
        // reports "no handler claimed cmd_id=0xffffffff", and a clean
        // disconnect is logged as a protocol error — which is how a
        // normal guest shutdown looked until this line existed.
        if (f.cmd_id == kCmdBye) {
            break;
        }

        Buffer reply;
        const bool ok = dispatcher.dispatch(
            f.cmd_id, std::span<const std::uint8_t>{f.payload.data(), f.payload.size()}, reply);

        if ((f.flags & FrameFlags::needs_reply) != 0) {
            if (channel_->write_reply(f, reply.bytes(), /*error=*/!ok) != IoResult::Ok) {
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
    // Unblock the reader wherever it is parked — a socket read, or a
    // poll on a ring.
    channel_->shutdown_read();
    reader.join();
    close();
}

void Connection::request_stop() noexcept {
    stop_.store(true, std::memory_order_release);
    if (channel_ != nullptr) {
        channel_->shutdown_read();
    }
    ring_cv_.notify_all();
}

void Connection::close() noexcept {
    if (channel_ != nullptr) {
        channel_->close();
    }
}

}  // namespace lsw::render
