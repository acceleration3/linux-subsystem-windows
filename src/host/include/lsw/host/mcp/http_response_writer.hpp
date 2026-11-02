#pragma once

#include <string>

class QTcpSocket;

namespace lsw::host::mcp {
class HttpResponseWriter {
public:
    HttpResponseWriter() = delete;

    static void send(QTcpSocket* socket, int status, const std::string& body);
};

}
