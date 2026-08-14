#include "lsw/host/platform/vsock_client.hpp"

#include "lsw/base/log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace lsw::host::platform {

// VsockClient (legacy name) is a TCP-loopback client. The agent
// listens on guest:5102 and QEMU's user-mode-networking ``hostfwd``
// rule (domain.xml) forwards host:127.0.0.1:5102 straight to it.
//
// A real AF_VSOCK / virtio-vsock transport was attempted and reverted:
// this class of host kernel exposes a /dev/vhost-vsock devtmpfs node
// but has no working ``vhost_vsock`` (not a module, not built in), so
// QEMU's open() of it returns ENODEV ("unable to open vhost-vsock
// device: No such device") and the domain won't start. TCP-over-
// hostfwd works regardless of host vsock support. The ``cid`` arg is
// accepted but ignored (kept so callers don't churn).

VsockClient::VsockClient(QObject* parent) : QObject(parent) {}

VsockClient::~VsockClient() {
    close();
}

int VsockClient::connect(quint32 /*cid*/, quint32 port) {
    close();
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) {
        LSW_ERROR("agent tcp socket(): {}", std::strerror(errno));
        return -errno;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    // Nagle off.
    //
    // This carries input: a stream of small one-way writes with no
    // replies to piggyback an ACK on. Left on, Nagle holds each write
    // until the previous one is acknowledged, the guest's delayed ACK
    // sits on that for tens of milliseconds, and what arrives is a
    // burst — a key-down and its key-up delivered together, or a key
    // held far longer than it was pressed. It reads as input that is
    // sometimes late, sometimes stuck, and never quite reproducible.
    {
        const int one = 1;
        if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
            LSW_WARN("agent tcp TCP_NODELAY: {}", std::strerror(errno));
        }
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int e = errno;
        // Connect failures are expected during install / boot — the
        // post-install watcher hammers this every few seconds while
        // waiting for the agent. DEBUG; callers promote based on rc.
        const auto line = QStringLiteral("agent tcp connect(127.0.0.1:%1): %2")
                              .arg(port)
                              .arg(QString::fromLocal8Bit(std::strerror(e)));
        LSW_DEBUG("{}", line.toStdString());
        ::close(fd);
        return -e;
    }
    // No SO_RCVTIMEO: the AgentRpcClient reader thread blocks here
    // indefinitely waiting for a reply or an agent-initiated push.
    // A timeout would make idle look like a disconnect and tear the
    // persistent connection down.
    fd_ = fd;
    return 0;
}

void VsockClient::close() noexcept {
    if (fd_ >= 0) {
        // ``shutdown`` first so a recv blocked on this fd in the
        // reader thread wakes immediately with a 0-byte read.
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

int VsockClient::send_all(const QByteArray& data) {
    if (fd_ < 0) {
        return -EPIPE;
    }
    qint64 off = 0;
    while (off < data.size()) {
        const ssize_t n =
            ::send(fd_, data.constData() + off, static_cast<size_t>(data.size() - off), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        off += n;
    }
    return 0;
}

QByteArray VsockClient::recv_some(int n) {
    if (fd_ < 0 || n <= 0) {
        return {};
    }
    QByteArray buf;
    buf.resize(n);
    const ssize_t got = ::recv(fd_, buf.data(), static_cast<size_t>(n), 0);
    if (got <= 0) {
        close();
        return {};
    }
    buf.resize(static_cast<int>(got));
    return buf;
}

}  // namespace lsw::host::platform
