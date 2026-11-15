#include "lsw/host/manager/network_state.hpp"

#include "lsw/host/platform/host_ports.hpp"
#include "lsw/host/services/vm_status.hpp"

#include <QHostAddress>

namespace lsw::host::manager {

namespace {

using config::PortForward;
using config::PortForwards;

QString mode_text(const QString& bridge) {
    return bridge.isEmpty() ? QStringLiteral("shared with this computer")
                            : QStringLiteral("bridged to %1").arg(bridge);
}

QString capitalised(QString text) {
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

}  // namespace

NetworkState::Live NetworkState::live(const services::VmStatus& vm) {
    Live out;
    out.running = vm.running();
    if (!out.running || !vm.running_device) {
        return out;
    }
    out.known = true;
    for (const PortForward& forward : vm.running_device->forwards) {
        if (!PortForwards::own().contains(forward)) {
            out.forwards.push_back(forward);
        }
    }
    out.bridges = vm.running_device->bridges;
    out.guest_ipv4 = vm.guest_ipv4;
    return out;
}

bool NetworkState::host_port_free(const PortForward& forward) {
    const QHostAddress address(forward.from_network ? QHostAddress::AnyIPv4
                                                    : QHostAddress::LocalHost);
    return forward.protocol == PortForward::Protocol::Udp
               ? platform::HostPorts::udp_free(address, forward.host_port)
               : platform::HostPorts::tcp_free(address, forward.host_port);
}

NetworkState::ForwardState NetworkState::forward_state(const PortForward& forward, const Live& live,
                                                       bool port_free) {
    if (live.running && live.forwards.contains(forward)) {
        return ForwardState::Active;
    }
    if (!port_free) {
        return ForwardState::PortInUse;
    }
    return live.running ? ForwardState::AfterRestart : ForwardState::NextStart;
}

QString NetworkState::summary(const Live& live, const QString& next_bridge, int next_forwards,
                              int waiting) {
    QStringList parts;
    if (!live.running) {
        parts << QStringLiteral("Guest off")
              << QStringLiteral("starts %1").arg(mode_text(next_bridge));
        parts << (next_forwards == 0   ? QStringLiteral("no port to forward")
                  : next_forwards == 1 ? QStringLiteral("1 port to forward")
                                       : QStringLiteral("%1 ports to forward").arg(next_forwards));
    } else if (!live.known) {
        parts << QStringLiteral("Running");
    } else {
        parts << capitalised(mode_text(live.bridges.join(QStringLiteral(", "))));
        const auto count = live.forwards.size();
        parts << (count == 0   ? QStringLiteral("no port forwarded")
                  : count == 1 ? QStringLiteral("1 port forwarded")
                               : QStringLiteral("%1 ports forwarded").arg(count));
    }
    if (waiting == 1) {
        parts << QStringLiteral("1 change waits for a restart");
    } else if (waiting > 1) {
        parts << QStringLiteral("%1 changes wait for a restart").arg(waiting);
    }
    return parts.join(QStringLiteral(" · "));
}

}  // namespace lsw::host::manager
