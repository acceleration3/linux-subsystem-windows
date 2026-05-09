#pragma once

#include <string>
#include <utility>

namespace lsw::protocol {

// File descriptor of an accepted client connection. The receiver owns the
// fd and is responsible for closing it (typically by handing it to a
// FrameStream and a per-connection thread pair).
struct AcceptedFd {
    int fd = -1;

    [[nodiscard]] bool ok() const noexcept { return fd >= 0; }
};

// Inbound connection acceptor. One implementation per transport family
// (Unix-domain, vsock, TCP loopback). The acceptor is single-threaded —
// only one thread may call ``accept`` at a time, and ``close`` may be
// called from any thread to unblock it.
class Listener {
public:
    virtual ~Listener() = default;

    Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;

    // Block until a client connects. Returns an empty AcceptedFd when the
    // listener has been closed (``close`` called) or on a fatal accept
    // error; callers treat both as "stop".
    [[nodiscard]] virtual AcceptedFd accept() = 0;

    // Stop accepting and free any underlying resources. Safe to call more
    // than once and from any thread.
    virtual void close() noexcept = 0;

    // Human-readable name (e.g. ``unix:///tmp/lsw.sock``). For logs only.
    [[nodiscard]] virtual std::string description() const = 0;
};

}  // namespace lsw::protocol
