#pragma once

class QHostAddress;

namespace lsw::host::platform {
class HostPorts {
public:
    HostPorts() = delete;

    [[nodiscard]] static bool tcp_free(const QHostAddress& address, int port);
    [[nodiscard]] static bool udp_free(const QHostAddress& address, int port);
};

}  // namespace lsw::host::platform
