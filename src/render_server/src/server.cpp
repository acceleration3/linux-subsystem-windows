#include "lsw/render/dispatch_chain.hpp"
#include "lsw/render/server.hpp"

#include "lsw/render/connection.hpp"

#include <utility>

namespace lsw::render {

void Server::serve_one() {
    auto channel = listener_.accept();
    if (channel == nullptr) {
        return;
    }
    Session session;
    DispatchChain chain{session};
    build_(session, chain);
    Connection conn{std::move(channel)};
    active_.store(&conn, std::memory_order_release);
    if (stop_.load(std::memory_order_acquire)) {
        // ``request_stop`` ran between the accept and the publish, so it
        // could not have seen this connection. Checking after publishing
        // is what closes that window.
        active_.store(nullptr, std::memory_order_release);
        return;
    }
    conn.run(chain);
    active_.store(nullptr, std::memory_order_release);
}

void Server::serve_forever() {
    while (!stop_.load(std::memory_order_acquire)) {
        serve_one();
    }
}

void Server::request_stop() noexcept {
    stop_.store(true, std::memory_order_release);
    listener_.close();
    if (auto* conn = active_.load(std::memory_order_acquire); conn != nullptr) {
        conn->request_stop();
    }
}

}  // namespace lsw::render
