#include "lsw/protocol/frame_stream.hpp"

#include "lsw/protocol/frame_header.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace lsw::protocol {

namespace {

[[nodiscard]] int read_exact(int fd, void* buf, std::size_t n) noexcept {
    auto* p = static_cast<std::uint8_t*>(buf);
    while (n > 0) {
        const ssize_t r = ::read(fd, p, n);
        if (r > 0) {
            p += r;
            n -= static_cast<std::size_t>(r);
            continue;
        }
        if (r == 0) {
            return -EPIPE;
        }
        if (errno == EINTR) {
            continue;
        }
        return -errno;
    }
    return 0;
}

[[nodiscard]] int write_exact(int fd, const void* buf, std::size_t n) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    while (n > 0) {
        const ssize_t r = ::write(fd, p, n);
        if (r > 0) {
            p += r;
            n -= static_cast<std::size_t>(r);
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return -errno;
    }
    return 0;
}

// Single-syscall write of three contiguous regions, falling back to
// byte-buffered if writev is short.
[[nodiscard]] IoResult write_three(int fd, const void* a, std::size_t a_len, const void* b,
                                   std::size_t b_len, const void* c, std::size_t c_len) noexcept {
    if (a_len == 0 && b_len == 0 && c_len == 0) {
        return IoResult::Ok;
    }
    if (write_exact(fd, a, a_len) != 0) {
        return IoResult::Disconnected;
    }
    if (b_len > 0 && write_exact(fd, b, b_len) != 0) {
        return IoResult::Disconnected;
    }
    if (c_len > 0 && write_exact(fd, c, c_len) != 0) {
        return IoResult::Disconnected;
    }
    return IoResult::Ok;
}

}  // namespace

IoResult FrameStream::read_frame(Frame& out) noexcept {
    if (fd_ < 0) {
        return IoResult::Disconnected;
    }
    FrameHeader hdr{};
    if (read_exact(fd_, &hdr, sizeof(hdr)) != 0) {
        return IoResult::Disconnected;
    }
    out.cmd_id = hdr.cmd_id;
    out.flags = hdr.flags;
    out.reply_token = 0;
    if ((hdr.flags & FrameFlags::needs_reply) != 0) {
        if (read_exact(fd_, &out.reply_token, sizeof(out.reply_token)) != 0) {
            return IoResult::Disconnected;
        }
    }
    out.payload.resize(hdr.payload_len);
    if (hdr.payload_len > 0) {
        if (read_exact(fd_, out.payload.data(), out.payload.size()) != 0) {
            return IoResult::Disconnected;
        }
    }
    return IoResult::Ok;
}

IoResult FrameStream::read_reply(Frame& out) noexcept {
    if (fd_ < 0) {
        return IoResult::Disconnected;
    }
    FrameHeader hdr{};
    if (read_exact(fd_, &hdr, sizeof(hdr)) != 0) {
        return IoResult::Disconnected;
    }
    out.cmd_id = hdr.cmd_id;
    out.flags = hdr.flags;
    if (read_exact(fd_, &out.reply_token, sizeof(out.reply_token)) != 0) {
        return IoResult::Disconnected;
    }
    out.payload.resize(hdr.payload_len);
    if (hdr.payload_len > 0) {
        if (read_exact(fd_, out.payload.data(), out.payload.size()) != 0) {
            return IoResult::Disconnected;
        }
    }
    return IoResult::Ok;
}

IoResult FrameStream::write_frame(std::uint32_t cmd_id, std::uint8_t flags,
                                  std::uint64_t reply_token,
                                  std::span<const std::uint8_t> payload) noexcept {
    if (fd_ < 0) {
        return IoResult::Disconnected;
    }
    FrameHeader hdr{};
    hdr.cmd_id = cmd_id;
    hdr.payload_len = static_cast<std::uint32_t>(payload.size());
    hdr.flags = flags;
    const bool include_token = (flags & FrameFlags::needs_reply) != 0;
    return write_three(fd_, &hdr, sizeof(hdr), include_token ? &reply_token : nullptr,
                       include_token ? sizeof(reply_token) : 0, payload.data(), payload.size());
}

IoResult FrameStream::write_reply(const Frame& request, std::span<const std::uint8_t> payload,
                                  bool error) noexcept {
    if (fd_ < 0) {
        return IoResult::Disconnected;
    }
    FrameHeader hdr{};
    hdr.cmd_id = request.cmd_id;
    hdr.payload_len = static_cast<std::uint32_t>(payload.size());
    // Bit 0 in a reply means ERROR (see docs/protocol.md). Replies always
    // carry a reply_token, regardless of flag bits.
    hdr.flags = error ? std::uint8_t{1} : std::uint8_t{0};
    const std::uint64_t token = request.reply_token;
    return write_three(fd_, &hdr, sizeof(hdr), &token, sizeof(token), payload.data(),
                       payload.size());
}

void FrameStream::shutdown_read() noexcept {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RD);
    }
}

}  // namespace lsw::protocol
