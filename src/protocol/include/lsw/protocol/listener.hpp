#pragma once

#include "lsw/protocol/frame_channel.hpp"

#include <memory>
#include <string>

namespace lsw::protocol {

// Inbound connection acceptor. One implementation per transport family
// (Unix-domain today, shared memory next to it).
//
// ``accept`` yields a ``FrameChannel`` rather than a file descriptor,
// which is what lets the render server be transport-agnostic in fact and
// not merely in intent: a shared-memory peer has no fd to hand over, and
// while ``accept`` returned one, "add a transport" meant "find every
// place that assumed a socket".
//
// The acceptor is single-threaded — only one thread may call ``accept``
// at a time, and ``close`` may be called from any thread to unblock it.
class Listener {
public:
    virtual ~Listener() = default;

    Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;

    // Block until a client connects. Returns nullptr when the listener
    // has been closed (``close`` called) or on a fatal accept error;
    // callers treat both as "stop".
    [[nodiscard]] virtual std::unique_ptr<FrameChannel> accept() = 0;

    // Stop accepting and free any underlying resources. Safe to call more
    // than once and from any thread.
    virtual void close() noexcept = 0;

    // The region connections from this listener share with their peer,
    // or null for a socket listener. The server needs it to turn a
    // client's arena offset into a pointer it can import.
    [[nodiscard]] virtual std::shared_ptr<SharedRegion> backing_region() const { return nullptr; }

    // Human-readable name (e.g. ``unix:///tmp/lsw.sock``). For logs only.
    [[nodiscard]] virtual std::string description() const = 0;
};

}  // namespace lsw::protocol
