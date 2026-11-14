#include "lsw/host/platform/host_ports.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QUdpSocket>

namespace lsw::host::platform {

bool HostPorts::tcp_free(const QHostAddress& address, int port) {
    QTcpServer server;
    return server.listen(address, static_cast<quint16>(port));
}

bool HostPorts::udp_free(const QHostAddress& address, int port) {
    QUdpSocket socket;
    return socket.bind(address, static_cast<quint16>(port), QAbstractSocket::DontShareAddress);
}

}
