#include "lsw/protocol/channel_transport.hpp"

#include "lsw/protocol/frame_flags.hpp"
#include "lsw/protocol/wire_frame.hpp"

#include <cerrno>
#include <utility>

namespace lsw::protocol {

ChannelTransport::ChannelTransport(std::unique_ptr<FrameChannel> channel) {
    static_cast<void>(adopt(std::move(channel)));
}

ChannelTransport::~ChannelTransport() {
    close();
}

int ChannelTransport::adopt(std::unique_ptr<FrameChannel> channel) {
    close();
    if (channel == nullptr || !channel->is_open()) {
        return -EINVAL;
    }
    channel_ = std::move(channel);
    stopping_.store(false);
    reader_done_.store(false);
    next_token_.store(1);
    orphan_replies_.store(0);
    reader_ = std::thread([this] { reader_loop(); });
    return 0;
}

bool ChannelTransport::is_open() const noexcept {
    return channel_ != nullptr && channel_->is_open() && !stopping_.load();
}

std::string ChannelTransport::description() const {
    return channel_ == nullptr ? std::string{"(no channel)"} : channel_->description();
}

void ChannelTransport::close() noexcept {
    if (channel_ == nullptr) {
        return;
    }
    stopping_.store(true);
    // Unblock the reader wherever it is parked, then let it finish before
    // the channel it is reading from goes away.
    channel_->shutdown_read();
    channel_->close();
    if (reader_.joinable()) {
        if (reader_.get_id() == std::this_thread::get_id()) {
            // Called from the reader itself (a handler that closed the
            // transport). Detach rather than deadlock on join; the thread
            // is on its way out anyway.
            reader_.detach();
        } else {
            reader_.join();
        }
    }
    fail_all_waiters(TransportResult::Disconnected);
    channel_.reset();
}

void ChannelTransport::fail_all_waiters(TransportResult reason) noexcept {
    std::unique_lock lk{waiters_mutex_};
    for (auto& [token, waiter] : waiters_) {
        waiter->result = reason;
        waiter->done = true;
        waiter->cv.notify_one();
    }
    waiters_.clear();
}

void ChannelTransport::reader_loop() {
    Frame f;
    while (!stopping_.load()) {
        const IoResult r = channel_->read_reply(f);
        if (r == IoResult::Timeout) {
            // A polling channel says "nothing yet", not "gone". There is
            // no EOF in shared memory, so a peer that died quietly is
            // detected by whoever owns the channel, not here.
            continue;
        }
        if (r != IoResult::Ok) {
            break;
        }

        // Everything below happens under the lock, including the notify.
        //
        // The notify in particular: a ``Waiter`` lives on the calling
        // thread's stack, and the instant ``done`` is visible with the
        // mutex free that thread may wake, return from ``send_sync``,
        // and take the frame with it. Notifying outside the lock is then
        // a write through a dangling pointer — which it was, and which
        // showed up as an intermittent segfault rather than as anything
        // legible. The waiter cannot leave ``cv.wait`` without
        // reacquiring this mutex, so holding it is what makes the
        // pointer safe to touch at all.
        const std::lock_guard lk{waiters_mutex_};
        const auto it = waiters_.find(f.reply_token);
        if (it == waiters_.end()) {
            // Nobody is waiting for this token. Either the server
            // answered twice or a token was recycled — both are protocol
            // bugs, and both are silent unless counted.
            orphan_replies_.fetch_add(1);
            continue;
        }
        Waiter* waiter = it->second;
        waiters_.erase(it);

        if (waiter->reply_out != nullptr && !f.payload.empty()) {
            waiter->reply_out->write(f.payload.data(), f.payload.size());
        }
        // The dispatch result lives in the first int32 of the payload,
        // not in the flags, so a server-side error is still a delivered
        // reply — same as the socket transport always did, and the
        // generated client code depends on it.
        waiter->result = TransportResult::Ok;
        waiter->done = true;
        waiter->cv.notify_one();
    }
    reader_done_.store(true);
    fail_all_waiters(TransportResult::Disconnected);
}

TransportResult ChannelTransport::send_sync(std::uint32_t cmd_id,
                                            std::span<const std::uint8_t> payload,
                                            Buffer* reply_out) {
    if (channel_ == nullptr || stopping_.load()) {
        return TransportResult::Disconnected;
    }
    const std::uint64_t token = next_token_.fetch_add(1);

    Waiter waiter;
    waiter.reply_out = reply_out;
    {
        const std::lock_guard lk{waiters_mutex_};
        waiters_.emplace(token, &waiter);
    }

    IoResult wrote = IoResult::Ok;
    {
        // Short and uncontended-by-design: just long enough to put one
        // frame into the channel. Nobody waits for a reply under it.
        const std::lock_guard lk{write_mutex_};
        wrote = channel_->write_frame(cmd_id, FrameFlags::needs_reply, token, payload);
    }
    if (wrote != IoResult::Ok) {
        const std::lock_guard lk{waiters_mutex_};
        waiters_.erase(token);
        return wrote == IoResult::Timeout ? TransportResult::Timeout
                                          : TransportResult::Disconnected;
    }

    std::unique_lock lk{waiters_mutex_};
    waiter.cv.wait(lk, [&] { return waiter.done || reader_done_.load(); });
    if (!waiter.done) {
        // The reader was already gone when we got here, so no reply is
        // coming and nothing will clean up our entry.
        waiters_.erase(token);
        return TransportResult::Disconnected;
    }
    return waiter.result;
}

TransportResult ChannelTransport::send_async(std::uint32_t cmd_id,
                                             std::span<const std::uint8_t> payload) {
    if (channel_ == nullptr || stopping_.load()) {
        return TransportResult::Disconnected;
    }
    IoResult wrote = IoResult::Ok;
    {
        const std::lock_guard lk{write_mutex_};
        wrote = channel_->write_frame(cmd_id, /*flags=*/0, /*reply_token=*/0, payload);
    }
    if (wrote == IoResult::Ok) {
        return TransportResult::Ok;
    }
    return wrote == IoResult::Timeout ? TransportResult::Timeout : TransportResult::Disconnected;
}

}  // namespace lsw::protocol
