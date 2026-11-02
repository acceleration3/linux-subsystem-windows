#pragma once

#include <QObject>

#include <cstdint>
#include <functional>
#include <string>

class QTcpServer;
class QTcpSocket;

namespace lsw::host::mcp {
class HttpServer : public QObject {
    Q_OBJECT
public:
    using BodyHandler = std::function<std::string(const std::string& body)>;

    explicit HttpServer(QObject* parent = nullptr);
    ~HttpServer() override;

    void set_handler(BodyHandler handler);

    [[nodiscard]] bool listen(std::uint16_t port);

    [[nodiscard]] std::uint16_t port() const noexcept;

private Q_SLOTS:
    void on_new_connection();

private:
    void try_serve(QTcpSocket* socket);

    QTcpServer* server_;
    BodyHandler handler_;
};

}
