#pragma once

#include <QString>
#include <QVector>

namespace lsw::host::config {
struct PortForward {
    enum class Protocol { Tcp, Udp };

    Protocol protocol = Protocol::Tcp;
    int host_port = 0;
    int guest_port = 0;
    bool from_network = false;

    [[nodiscard]] bool operator==(const PortForward& other) const noexcept = default;
};

class PortForwards {
public:
    PortForwards() = delete;

    static constexpr int kLowestPort = 1;
    static constexpr int kHighestPort = 65535;

    [[nodiscard]] static const QVector<PortForward>& own();
    [[nodiscard]] static QVector<PortForward> sunshine();

    [[nodiscard]] static int first_unprivileged_port();

    [[nodiscard]] static QString refusal(const QVector<PortForward>& entries,
                                         const PortForward& candidate,
                                         int first_host_port = first_unprivileged_port());
    [[nodiscard]] static QVector<PortForward> accepted(const QVector<PortForward>& entries,
                                                       int first_host_port = first_unprivileged_port());

    [[nodiscard]] static QString encode(const QVector<PortForward>& entries);
    [[nodiscard]] static QVector<PortForward> decode(const QString& blob);

    [[nodiscard]] static QString protocol_name(PortForward::Protocol protocol);
    [[nodiscard]] static QString hostfwd(const PortForward& entry);
};

}
