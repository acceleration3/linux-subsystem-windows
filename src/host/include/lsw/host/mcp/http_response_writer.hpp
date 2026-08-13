#pragma once

#include <string>

class QTcpSocket;

namespace lsw::host::mcp {

// Writes one HTTP/1.1 response and closes the connection.
//
// Split from the server because it is the only part that knows the
// response byte layout, and it is worth being able to read that in
// isolation from the parsing and threading around it.
class HttpResponseWriter {
public:
    HttpResponseWriter() = delete;

    // Send ``body`` as ``application/json`` with the given status, then
    // disconnect. Keep-alive is deliberately not offered — one request
    // per connection keeps the parser trivial. No-op on a null socket.
    static void send(QTcpSocket* socket, int status, const std::string& body);
};

}  // namespace lsw::host::mcp
