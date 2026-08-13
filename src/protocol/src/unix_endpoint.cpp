#include "lsw/protocol/unix_endpoint.hpp"

#include "lsw/protocol/fd_frame_channel.hpp"
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

std::unique_ptr<FrameChannel> UnixListener::accept() {
    while (listen_fd_ >= 0) {
        const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd >= 0) {
            auto channel = std::make_unique<FdFrameChannel>(fd);
            channel->set_description("unix://" + path_);
            return channel;
        }
        if (errno == EINTR) {
            continue;
        }
        return nullptr;
    }
    return nullptr;
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

// ---- Connecting end -------------------------------------------------------

int connect_unix_socket(const std::string& path) {
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
    return fd;
}

}  // namespace lsw::protocol
