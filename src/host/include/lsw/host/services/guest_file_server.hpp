#pragma once

#include <QByteArray>

#include <atomic>
#include <map>
#include <string>
#include <string_view>
#include <thread>

namespace lsw::host::services {

// Serves a few fixed files over HTTP on a loopback port, each under its
// own name, for as long as it is alive.
//
// Exists because the guest cannot read the host filesystem: to hand it a
// new agent binary (and the window mirror's guest half beside it) we put
// the bytes behind URLs the guest can reach at
// ``10.0.2.2:<port>/<name>`` through QEMU user-mode networking. It stays
// up for the whole redeploy window rather than serving once, so a
// transient download failure in the guest can simply be retried.
//
// RAII: the destructor stops the accept loop and joins the thread.
class GuestFileServer {
public:
    // Name, as it appears after the slash in the URL, to bytes.
    using Files = std::map<std::string, QByteArray, std::less<>>;

    explicit GuestFileServer(Files files) noexcept;
    ~GuestFileServer();

    GuestFileServer(const GuestFileServer&) = delete;
    GuestFileServer& operator=(const GuestFileServer&) = delete;

    // Bind 127.0.0.1:0 and start accepting. False on failure, with the
    // reason in ``error()``.
    [[nodiscard]] bool start();

    // Stop accepting and join. Idempotent; also called by the destructor.
    void stop();

    [[nodiscard]] unsigned short port() const noexcept { return port_; }

    // How many times a file was fully requested. Reported back to the
    // model on timeout: "served 0 downloads" and "served 3" point at very
    // different failures.
    [[nodiscard]] int downloads_served() const noexcept { return served_.load(); }

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    // The name an HTTP request asks for: the path of its request line
    // without the leading slash or any query. Empty when the request has
    // no request line this can read.
    [[nodiscard]] static std::string_view requested_name(std::string_view request);

private:
    void serve_loop();
    void answer(int connection, std::string_view request);

    Files files_;
    int listen_fd_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> served_{0};
    std::thread thread_;
    std::string error_;
};

}  // namespace lsw::host::services
