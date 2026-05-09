#pragma once

#include "lsw/protocol/listener.hpp"
#include "lsw/server/handler.hpp"
#include "lsw/server/session.hpp"

#include <atomic>
#include <functional>

namespace lsw::server {

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
    // next ``accept`` returns empty. Safe from any thread.
    void request_stop() noexcept;

private:
    ::lsw::protocol::Listener& listener_;
    DispatchChainBuilder build_;
    std::atomic<bool> stop_{false};
};

}  // namespace lsw::server
