#pragma once

#include "lsw/host/config/port_forwards.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::services {
struct VmStatus;
}

namespace lsw::host::manager {
class NetworkState {
public:
    NetworkState() = delete;

    enum class ForwardState { Active, AfterRestart, NextStart, PortInUse };

    struct Live {
        bool running = false;
        bool known = false;
        QVector<config::PortForward> forwards;
        QStringList bridges;
        QString guest_ipv4;

        [[nodiscard]] bool operator==(const Live&) const = default;
    };

    [[nodiscard]] static Live live(const services::VmStatus& vm);

    [[nodiscard]] static bool host_port_free(const config::PortForward& forward);

    [[nodiscard]] static ForwardState forward_state(const config::PortForward& forward,
                                                    const Live& live, bool port_free);

    [[nodiscard]] static QString summary(const Live& live, const QString& next_bridge,
                                         int next_forwards, int waiting);
};

}  // namespace lsw::host::manager
