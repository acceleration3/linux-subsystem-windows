#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::config {
struct PortForward {
    enum class Protocol { Tcp, Udp };

    Protocol protocol = Protocol::Tcp;
    int host_port = 0;
    int guest_port = 0;
    bool from_network = false;

    [[nodiscard]] bool operator==(const PortForward& other) const noexcept = default;
};

struct PortPreset {
    QString name;
    QString description;
    QVector<PortForward> ports;
};

class PortForwards {
public:
    PortForwards() = delete;

    static constexpr int kLowestPort = 1;
    static constexpr int kHighestPort = 65535;

    [[nodiscard]] static const QVector<PortForward>& own();
    [[nodiscard]] static const QVector<PortPreset>& presets();
    [[nodiscard]] static bool applied(const QVector<PortForward>& entries, const PortPreset& preset);
    [[nodiscard]] static QVector<PortForward> with(QVector<PortForward> entries,
                                                   const PortPreset& preset, QStringList* refusals,
                                                   int first_host_port = first_unprivileged_port());
    [[nodiscard]] static QVector<PortForward> without(QVector<PortForward> entries,
                                                      const PortPreset& preset);
    [[nodiscard]] static QString mapping(const PortPreset& preset);

    [[nodiscard]] static int first_unprivileged_port();

    [[nodiscard]] static QString refusal(const QVector<PortForward>& entries,
                                         const PortForward& candidate,
                                         int first_host_port = first_unprivileged_port());
    [[nodiscard]] static QVector<PortForward> accepted(const QVector<PortForward>& entries,
                                                       int first_host_port = first_unprivileged_port());

    [[nodiscard]] static QString encode(const QVector<PortForward>& entries);
    [[nodiscard]] static QVector<PortForward> decode(const QString& blob);

    [[nodiscard]] static QString protocol_name(PortForward::Protocol protocol);
    [[nodiscard]] static QString ports_text(const PortForward& entry);
    [[nodiscard]] static QString hostfwd(const PortForward& entry);
    [[nodiscard]] static std::optional<PortForward> from_hostfwd(const QString& option);
};

}
