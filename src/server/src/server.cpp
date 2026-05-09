#include "lsw/server/server.hpp"

#include "lsw/server/connection.hpp"

namespace lsw::server {

void Server::serve_one() {
    auto accepted = listener_.accept();
    if (!accepted.ok()) {
        return;
    }
    Session session;
    DispatchChain chain{session};
    build_(session, chain);
    Connection conn{accepted};
    conn.run(chain);
}

void Server::serve_forever() {
    while (!stop_.load(std::memory_order_acquire)) {
        serve_one();
    }
}

void Server::request_stop() noexcept {
    stop_.store(true, std::memory_order_release);
    listener_.close();
}

}  // namespace lsw::server
