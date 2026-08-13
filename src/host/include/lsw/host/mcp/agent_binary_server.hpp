#pragma once

#include <QByteArray>

#include <atomic>
#include <thread>

namespace lsw::host::mcp {

// Serves one fixed blob over HTTP on a loopback port, for as long as it
// is alive.
//
// Exists because the guest cannot read the host filesystem: to hand it a
// new agent binary we put the bytes behind a URL the guest can reach at
// ``10.0.2.2:<port>`` through QEMU user-mode networking. It stays up for
// the whole redeploy window rather than serving once, so a transient
// download failure in the guest can simply be retried.
//
// RAII: the destructor stops the accept loop and joins the thread.
class AgentBinaryServer {
public:
    explicit AgentBinaryServer(QByteArray payload) noexcept;
    ~AgentBinaryServer();

    AgentBinaryServer(const AgentBinaryServer&) = delete;
    AgentBinaryServer& operator=(const AgentBinaryServer&) = delete;

    // Bind 127.0.0.1:0 and start accepting. False on failure, with the
    // reason in ``error()``.
    [[nodiscard]] bool start();

    // Stop accepting and join. Idempotent; also called by the destructor.
    void stop();

    [[nodiscard]] unsigned short port() const noexcept { return port_; }

    // How many times the blob has been fully requested. Reported back to
    // the model on timeout — "served 0 downloads" and "served 3" point at
    // very different failures.
    [[nodiscard]] int downloads_served() const noexcept { return served_.load(); }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    void serve_loop();

    QByteArray payload_;
    int listen_fd_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> served_{0};
    std::thread thread_;
    std::string error_;
};

}  // namespace lsw::host::mcp
