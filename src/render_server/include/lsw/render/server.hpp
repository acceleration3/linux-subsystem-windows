#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/render/connection.hpp"
#include "lsw/render/dispatch_chain.hpp"
#include "lsw/render/handler.hpp"
#include "lsw/render/session.hpp"

#include <atomic>
#include <functional>

namespace lsw::render {

// Hook the Server calls once per accepted connection to populate the
// per-connection DispatchChain. The Server owns the Session and the
// DispatchChain; the builder just appends handlers in priority order.
using DispatchChainBuilder = std::function<void(Session&, DispatchChain&)>;

// Multi-connection acceptor loop. Holds a reference to a ``Listener`` that
// the caller already bound. For each accepted connection: build a fresh
// Session + DispatchChain via the user-supplied builder, run a Connection
// to completion, repeat.
//
// Single-threaded today: connections are served sequentially. Phase 4
// adds a thread-per-connection pool (the doc's per-connection layout
// already plans this).
class Server {
public:
    Server(::lsw::protocol::Listener& listener, DispatchChainBuilder build) noexcept
        : listener_(listener), build_(std::move(build)) {}

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;
    ~Server() = default;

    // Accept exactly one connection and serve it until disconnect.
    void serve_one();

    // Loop accepting connections until ``request_stop`` is called. Returns
    // when the listener stops accepting (close called from another thread).
    void serve_forever();

    // Asynchronously stop ``serve_forever``. Closes the listener so the
    // next ``accept`` returns nothing, AND stops the connection being
    // served right now — a socket server could rely on the client
    // eventually hanging up, but a shared-memory reader polls a ring and
    // would never notice a signal at all. Safe from any thread.
    void request_stop() noexcept;

private:
    ::lsw::protocol::Listener& listener_;
    DispatchChainBuilder build_;
    std::atomic<bool> stop_{false};

    // The connection ``serve_one`` is running, so ``request_stop`` can
    // reach it. Atomic rather than mutex-guarded on purpose:
    // ``request_stop`` is reached from a signal handler, and taking a
    // lock there deadlocks outright if the signal lands on the thread
    // that already holds it. A pointer publish is the strongest thing
    // available that cannot.
    std::atomic<Connection*> active_{nullptr};
};

}  // namespace lsw::render
