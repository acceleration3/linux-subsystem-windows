#include "lsw/protocol/unix_endpoint.hpp"

#include "lsw/protocol/frame.hpp"
#include "lsw/protocol/frame_io.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace lsw::protocol {

namespace {

[[nodiscard]] int fill_addr(sockaddr_un& addr, const std::string& path) noexcept {
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() + 1 > sizeof(addr.sun_path)) {
        return -ENAMETOOLONG;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return 0;
}

}  // namespace

// ---- UnixListener ---------------------------------------------------------

UnixListener::~UnixListener() {
    close();
}

int UnixListener::bind() {
    return bind(path_);
}

int UnixListener::bind(const std::string& path) {
    close();
    sockaddr_un addr{};
    if (const int rc = fill_addr(addr, path); rc != 0) {
        return rc;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -errno;
    }
    ::unlink(path.c_str());  // recover from prior abnormal exit
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        ::close(fd);
        return -err;
    }
    if (::listen(fd, 16) < 0) {
        const int err = errno;
        ::close(fd);
        return -err;
    }
    listen_fd_ = fd;
    path_ = path;
    return 0;
}

AcceptedFd UnixListener::accept() {
    while (listen_fd_ >= 0) {
        const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd >= 0) {
            return AcceptedFd{fd};
        }
        if (errno == EINTR) {
            continue;
        }
        return AcceptedFd{};
    }
    return AcceptedFd{};
}

void UnixListener::close() noexcept {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
    }
}

std::string UnixListener::description() const {
    return "unix://" + path_;
}

// ---- UnixSocketTransport --------------------------------------------------

UnixSocketTransport::UnixSocketTransport(int fd) noexcept : fd_(fd) {}

UnixSocketTransport::~UnixSocketTransport() {
    close();
}

UnixSocketTransport::UnixSocketTransport(UnixSocketTransport&& other) noexcept
    : fd_(other.fd_), next_token_(other.next_token_) {
    other.fd_ = -1;
}

UnixSocketTransport& UnixSocketTransport::operator=(UnixSocketTransport&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        next_token_ = other.next_token_;
        other.fd_ = -1;
    }
    return *this;
}

bool UnixSocketTransport::is_open() const noexcept {
    return fd_ >= 0;
}

void UnixSocketTransport::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int UnixSocketTransport::connect(const std::string& path) {
    close();
    sockaddr_un addr{};
    if (const int rc = fill_addr(addr, path); rc != 0) {
        return rc;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -errno;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int err = errno;
        ::close(fd);
        return -err;
    }
    fd_ = fd;
    next_token_ = 1;
    return 0;
}

TransportResult UnixSocketTransport::send_sync(std::uint32_t cmd_id,
                                               std::span<const std::uint8_t> payload,
                                               Buffer* reply_out) {
    if (fd_ < 0) {
        return TransportResult::Disconnected;
    }
    FrameStream io{fd_};
    const std::uint64_t token = next_token_++;
    if (io.write_frame(cmd_id, FrameFlags::needs_reply, token, payload) != IoResult::Ok) {
        return TransportResult::Disconnected;
    }
    Frame reply{};
    if (io.read_reply(reply) != IoResult::Ok) {
        return TransportResult::Disconnected;
    }
    if (reply.reply_token != token) {
        return TransportResult::InvalidFrame;
    }
    if (reply_out != nullptr && !reply.payload.empty()) {
        reply_out->write(reply.payload.data(), reply.payload.size());
    }
    return TransportResult::Ok;
}

TransportResult UnixSocketTransport::send_async(std::uint32_t cmd_id,
                                                std::span<const std::uint8_t> payload) {
    if (fd_ < 0) {
        return TransportResult::Disconnected;
    }
    FrameStream io{fd_};
    if (io.write_frame(cmd_id, /*flags=*/0, /*reply_token=*/0, payload) != IoResult::Ok) {
        return TransportResult::Disconnected;
    }
    return TransportResult::Ok;
}

}  // namespace lsw::protocol
