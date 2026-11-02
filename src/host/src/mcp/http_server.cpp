#include "lsw/host/mcp/http_server.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/mcp/http_response_writer.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtConcurrent>

namespace lsw::host::mcp {

HttpServer::HttpServer(QObject* parent)
    : QObject(parent), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &HttpServer::on_new_connection);
}

HttpServer::~HttpServer() = default;

void HttpServer::set_handler(BodyHandler handler) { handler_ = std::move(handler); }

bool HttpServer::listen(std::uint16_t port) {
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        LSW_ERROR("mcp: listen on localhost:{} failed: {}", port,
                  server_->errorString().toStdString());
        return false;
    }
    LSW_INFO("mcp: listening on http://127.0.0.1:{}", server_->serverPort());
    return true;
}

std::uint16_t HttpServer::port() const noexcept {
    return server_->isListening() ? static_cast<std::uint16_t>(server_->serverPort()) : 0;
}

void HttpServer::on_new_connection() {
    while (auto* sock = server_->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { try_serve(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpServer::try_serve(QTcpSocket* socket) {
    const auto peeked = socket->peek(64 * 1024);
    const qsizetype header_end = peeked.indexOf("\r\n\r\n");
    if (header_end < 0) {
        return;
    }
    const qsizetype body_start = header_end + 4;

    const QList<QByteArray> lines = peeked.left(header_end).split('\n');
    if (lines.isEmpty()) {
        HttpResponseWriter::send(socket, 400, R"({"error":"bad request"})");
        return;
    }
    const auto request_line = QString::fromUtf8(lines.first()).trimmed();
    int content_length = 0;
    static const QString kLenHeader = QStringLiteral("Content-Length:");
    for (const auto& raw : lines) {
        const auto line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(kLenHeader, Qt::CaseInsensitive)) {
            content_length = line.mid(kLenHeader.size()).trimmed().toInt();
        }
    }

    if (socket->bytesAvailable() < body_start + content_length) {
        return;
    }
    socket->read(body_start);
    const QByteArray body = socket->read(content_length);

    if (!request_line.startsWith(QStringLiteral("POST"))) {
        HttpResponseWriter::send(socket, 405, R"({"error":"method not allowed; use POST"})");
        return;
    }
    if (!handler_) {
        HttpResponseWriter::send(socket, 500, R"({"error":"no handler installed"})");
        return;
    }

    const QPointer<QTcpSocket> socket_ptr{socket};
    const QPointer<HttpServer> self{this};
    const std::string request_body = body.toStdString();
    (void)QtConcurrent::run([self, socket_ptr, request_body]() {
        if (!self) {
            return;
        }
        const std::string response = self->handler_(request_body);
        QMetaObject::invokeMethod(
            self.data(),
            [socket_ptr, response]() {
                if (socket_ptr) {
                    HttpResponseWriter::send(socket_ptr.data(), 200, response);
                }
            },
            Qt::QueuedConnection);
    });
}

}
