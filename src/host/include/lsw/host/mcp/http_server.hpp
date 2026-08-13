#pragma once

#include <QObject>

#include <cstdint>
#include <functional>
#include <string>

class QTcpServer;
class QTcpSocket;

namespace lsw::host::mcp {

// Localhost HTTP/1.1 endpoint that hands request bodies to a handler and
// writes back whatever it returns.
//
// One concern: bytes on a socket. It does not know that the bodies are
// JSON-RPC, and it is where the threading rule lives — handlers run on a
// worker (they block on the guest agent) and the reply is marshalled
// back to this thread to be written.
class HttpServer : public QObject {
    Q_OBJECT
public:
    // Called on a worker thread with the request body; returns the
    // response body. Must not touch GUI state.
    using BodyHandler = std::function<std::string(const std::string& body)>;

    explicit HttpServer(QObject* parent = nullptr);
    ~HttpServer() override;

    // Set before ``listen``. Requests arriving with no handler get a 500.
    void set_handler(BodyHandler handler);

    // Bind 127.0.0.1:``port``. Returns false (logged) if the bind fails;
    // callers treat that as non-fatal.
    [[nodiscard]] bool listen(std::uint16_t port);

    // Bound port, or 0 when not listening.
    [[nodiscard]] std::uint16_t port() const noexcept;

private Q_SLOTS:
    void on_new_connection();

private:
    // Try to parse one complete request out of ``socket``'s buffer. Does
    // nothing until both headers and the full Content-Length body have
    // arrived; another readyRead will bring the rest.
    void try_serve(QTcpSocket* socket);

    QTcpServer* server_;
    BodyHandler handler_;
};

}  // namespace lsw::host::mcp
