#pragma once

#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/frame_stream.hpp"

#include <string>

namespace lsw::protocol {

// Frames over a stream file descriptor.
//
// This is the socket demoted to an equal. Nothing here is new — the
// framing is still ``FrameStream``, byte for byte the format that has
// been on the wire since the first loopback run — but it now arrives
// through ``FrameChannel``, so the render server and the client
// transport stop naming a file descriptor and the shared-memory channel
// becomes a substitution rather than a rewrite.
//
// Worth keeping rather than deleting once ``shm://`` works: it is the
// only transport you can point a debugger, a strace or a socket dump at,
// which is precisely what you want in hand while chasing a race in the
// one you cannot.
//
// Owns the fd and closes it.
class FdFrameChannel final : public FrameChannel {
public:
    FdFrameChannel() noexcept = default;
    explicit FdFrameChannel(int fd) noexcept : io_(fd) {}
    ~FdFrameChannel() override;

    [[nodiscard]] int fd() const noexcept { return io_.fd(); }

    [[nodiscard]] bool is_open() const noexcept override { return io_.is_open(); }

    [[nodiscard]] IoResult read_frame(Frame& out) noexcept override {
        return io_.read_frame(out);
    }
    [[nodiscard]] IoResult read_reply(Frame& out) noexcept override {
        return io_.read_reply(out);
    }
    [[nodiscard]] IoResult write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                       std::uint64_t reply_token,
                                       std::span<const std::uint8_t> payload) noexcept override {
        return io_.write_frame(cmd_id, flags, reply_token, payload);
    }
    [[nodiscard]] IoResult write_reply(const Frame& request, std::span<const std::uint8_t> payload,
                                       bool error = false) noexcept override {
        return io_.write_reply(request, payload, error);
    }

    void shutdown_read() noexcept override { io_.shutdown_read(); }
    void close() noexcept override;

    [[nodiscard]] std::string description() const override { return description_; }

    void set_description(std::string d) { description_ = std::move(d); }

private:
    FrameStream io_;
    std::string description_ = "fd://";
};

}  // namespace lsw::protocol
