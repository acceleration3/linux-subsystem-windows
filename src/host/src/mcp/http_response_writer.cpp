#include "lsw/host/mcp/http_response_writer.hpp"

#include <QByteArray>
#include <QTcpSocket>

namespace lsw::host::mcp {

namespace {

[[nodiscard]] const char* reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default:  return "Error";
    }
}

}  // namespace

void HttpResponseWriter::send(QTcpSocket* socket, int status, const std::string& body) {
    if (socket == nullptr) {
        return;
    }
    QByteArray resp;
    resp.append("HTTP/1.1 ");
    resp.append(QByteArray::number(status));
    resp.append(' ');
    resp.append(reason_phrase(status));
    resp.append("\r\n");
    resp.append("Content-Type: application/json\r\n");
    resp.append("Content-Length: ");
    resp.append(QByteArray::number(static_cast<qsizetype>(body.size())));
    resp.append("\r\n");
    resp.append("Connection: close\r\n");
    resp.append("Access-Control-Allow-Origin: *\r\n");
    resp.append("\r\n");
    resp.append(body.data(), static_cast<qsizetype>(body.size()));
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

}  // namespace lsw::host::mcp
