#include "lsw/manager/vm/ivshmem_server.hpp"

#include "lsw/manager/vm/shmem_region.hpp"
#include "lsw/log.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

namespace lsw::manager::vm {

namespace {

constexpr std::size_t kMaxSocketPath = sizeof(sockaddr_un::sun_path);

}  // namespace

IvshmemServer::IvshmemServer(const QUuid& vm_uuid, ShmemRegion& region,
                             QObject* parent)
    : QObject(parent), uuid_(vm_uuid), region_(region) {
    eventfds_.fill(-1);
    socket_path_ = QStringLiteral("/tmp/lsw-ivshmem-%1.sock")
                       .arg(vm_uuid.toString(QUuid::WithoutBraces));
}

IvshmemServer::~IvshmemServer() { stop(); }

int IvshmemServer::start() {
    if (worker_.joinable()) {
        LSW_WARN("IvshmemServer::start called twice; ignoring");
        return 0;
    }
    const auto path_utf8 = socket_path_.toUtf8();
    if (static_cast<std::size_t>(path_utf8.size()) + 1 > kMaxSocketPath) {
        LSW_ERROR("ivshmem socket path too long ({} bytes): {}",
                  path_utf8.size(), socket_path_.toStdString());
        return -1;
    }
    ::unlink(path_utf8.constData());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        LSW_ERROR("ivshmem socket() failed: {}", std::strerror(errno));
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_utf8.constData(),
                static_cast<std::size_t>(path_utf8.size()));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) != 0) {
        LSW_ERROR("ivshmem bind({}) failed: {}", socket_path_.toStdString(),
                  std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return -1;
    }
    if (::listen(listen_fd_, 1) != 0) {
        LSW_ERROR("ivshmem listen() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(path_utf8.constData());
        return -1;
    }
    ::chmod(path_utf8.constData(), 0600);

    // One eventfd per MSI vector — QEMU writes to these when the guest
    // doorbells us. Plus one for the worker shutdown signal.
    for (int i = 0; i < kVectorCount; ++i) {
        eventfds_[i] = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (eventfds_[i] < 0) {
            LSW_ERROR("ivshmem eventfd[{}] failed: {}", i,
                      std::strerror(errno));
            for (int j = 0; j < i; ++j) { ::close(eventfds_[j]); eventfds_[j] = -1; }
            ::close(listen_fd_); listen_fd_ = -1;
            ::unlink(path_utf8.constData());
            return -1;
        }
    }
    stop_eventfd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (stop_eventfd_ < 0) {
        LSW_ERROR("ivshmem stop eventfd failed: {}", std::strerror(errno));
        for (int i = 0; i < kVectorCount; ++i) { ::close(eventfds_[i]); eventfds_[i] = -1; }
        ::close(listen_fd_); listen_fd_ = -1;
        ::unlink(path_utf8.constData());
        return -1;
    }

    LSW_INFO("ivshmem server listening at {} ({} vectors)",
             socket_path_.toStdString(), kVectorCount);
    stop_.store(false);
    worker_ = std::thread([this] { worker_loop(); });
    return 0;
}

void IvshmemServer::stop() {
    if (worker_.joinable()) {
        stop_.store(true);
        // Kick the worker out of poll() immediately.
        if (stop_eventfd_ >= 0) {
            const std::uint64_t one = 1;
            ::write(stop_eventfd_, &one, sizeof(one));
        }
        worker_.join();
    }
    for (int i = 0; i < kVectorCount; ++i) {
        if (eventfds_[i] >= 0) { ::close(eventfds_[i]); eventfds_[i] = -1; }
    }
    if (stop_eventfd_ >= 0) { ::close(stop_eventfd_); stop_eventfd_ = -1; }
    if (listen_fd_ >= 0)    { ::close(listen_fd_);    listen_fd_ = -1; }
    if (!socket_path_.isEmpty()) {
        ::unlink(socket_path_.toUtf8().constData());
    }
}

bool IvshmemServer::send_msg(int peer_fd, std::int64_t value, int fd) {
    msghdr msg{};
    iovec iov{};
    iov.iov_base = &value;
    iov.iov_len  = sizeof(value);
    msg.msg_iov    = &iov;
    msg.msg_iovlen = 1;
    union {
        char buf[CMSG_SPACE(sizeof(int))];
        cmsghdr align;
    } cbuf{};
    if (fd >= 0) {
        msg.msg_control    = cbuf.buf;
        msg.msg_controllen = sizeof(cbuf.buf);
        auto* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    }
    const ssize_t n = ::sendmsg(peer_fd, &msg, MSG_NOSIGNAL);
    if (n != static_cast<ssize_t>(sizeof(value))) {
        LSW_ERROR("ivshmem sendmsg failed: n={} errno={}",
                  static_cast<int>(n), errno);
        return false;
    }
    return true;
}

void IvshmemServer::close_peer() {
    // No-op placeholder — peer fd cleanup happens inline in worker_loop.
}

void IvshmemServer::worker_loop() {
    int qemu_fd = -1;
    // poll set: [listen, stop, qemu (optional), eventfd[0..63]] = up to 67 fds.
    constexpr int kPollSlots = 3 + kVectorCount;
    pollfd pfd[kPollSlots]{};

    while (!stop_.load()) {
        int n = 0;
        pfd[n++] = {listen_fd_,   POLLIN, 0};
        pfd[n++] = {stop_eventfd_, POLLIN, 0};
        if (qemu_fd >= 0) {
            pfd[n++] = {qemu_fd, POLLIN, 0};
        }
        // Only watch eventfds AFTER QEMU has handshaken — they only
        // fire once QEMU's started running anyway.
        if (qemu_fd >= 0) {
            for (int v = 0; v < kVectorCount; ++v) {
                pfd[n++] = {eventfds_[v], POLLIN, 0};
            }
        }
        const int rc = ::poll(pfd, n, -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            LSW_WARN("ivshmem poll errno={} — bailing", errno);
            break;
        }
        for (int i = 0; i < n && !stop_.load(); ++i) {
            if ((pfd[i].revents & POLLIN) == 0) continue;

            if (pfd[i].fd == listen_fd_) {
                int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
                if (fd < 0) {
                    LSW_WARN("ivshmem accept errno={}", errno);
                    continue;
                }
                if (qemu_fd >= 0) {
                    LSW_WARN("ivshmem: refusing extra connection (already have peer)");
                    ::close(fd);
                    continue;
                }
                qemu_fd = fd;
                LSW_INFO("ivshmem: qemu connected; running handshake");
                // Wire order per QEMU's hw/misc/ivshmem-pci.c
                // (``ivshmem_recv_setup``): version → vm_id → shm_fd
                // → repeated (peer_id, eventfd) pairs. Sending shm_fd
                // before vm_id makes QEMU bail with "server sent
                // invalid ID message" (it reads the -1 marker as the
                // vm_id and rejects it as out of range).
                bool ok = send_msg(qemu_fd, kProtocolVersion, -1);
                if (ok) ok = send_msg(qemu_fd, kQemuPeerId, -1);       // your id
                if (ok) ok = send_msg(qemu_fd, -1, region_.fd());      // shm fd
                for (int v = 0; ok && v < kVectorCount; ++v) {
                    ok = send_msg(qemu_fd, kSelfPeerId, eventfds_[v]); // peer 0 vectors
                }
                if (!ok) {
                    LSW_ERROR("ivshmem handshake failed; closing peer socket");
                    ::close(qemu_fd);
                    qemu_fd = -1;
                } else {
                    LSW_INFO("ivshmem: handshake complete");
                }
            } else if (pfd[i].fd == stop_eventfd_) {
                stop_.store(true);
                break;
            } else if (pfd[i].fd == qemu_fd) {
                char drain[64];
                const ssize_t r = ::recv(qemu_fd, drain, sizeof(drain), 0);
                if (r <= 0) {
                    LSW_INFO("ivshmem: peer disconnected (r={})", static_cast<int>(r));
                    ::close(qemu_fd);
                    qemu_fd = -1;
                }
            } else {
                // eventfd[V] — drain the counter, emit slot_ready
                // queued onto the consumer's thread.
                int slot = -1;
                for (int v = 0; v < kVectorCount; ++v) {
                    if (eventfds_[v] == pfd[i].fd) { slot = v; break; }
                }
                if (slot < 0) continue;
                std::uint64_t counter = 0;
                const ssize_t er = ::read(pfd[i].fd, &counter, sizeof(counter));
                if (er != sizeof(counter)) {
                    if (er < 0 && errno != EAGAIN) {
                        LSW_WARN("ivshmem eventfd[{}] read errno={}", slot, errno);
                    }
                    continue;
                }
                QMetaObject::invokeMethod(
                    this, [this, slot] { Q_EMIT slot_ready(slot); },
                    Qt::QueuedConnection);
            }
        }
    }
    if (qemu_fd >= 0) { ::close(qemu_fd); }
}

}  // namespace lsw::manager::vm
