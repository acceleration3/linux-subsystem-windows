#include "lsw/host/config/port_forwards.hpp"

#include <QFile>
#include <QStringList>
#include <algorithm>

namespace lsw::host::config {

namespace {

constexpr QLatin1StringView kTcp{"tcp"};
constexpr QLatin1StringView kUdp{"udp"};
constexpr QLatin1StringView kLocal{"local"};
constexpr QLatin1StringView kNetwork{"network"};
constexpr QLatin1StringView kLoopback{"127.0.0.1"};
constexpr QLatin1StringView kEveryAddress{"0.0.0.0"};
constexpr int kUsualFirstUnprivilegedPort = 1024;

bool in_range(int port) {
    return port >= PortForwards::kLowestPort && port <= PortForwards::kHighestPort;
}

bool same_host_port(const PortForward& a, const PortForward& b) {
    return a.protocol == b.protocol && a.host_port == b.host_port;
}

bool same_ports(const PortForward& a, const PortForward& b) {
    return same_host_port(a, b) && a.guest_port == b.guest_port;
}

constexpr QLatin1StringView kHostfwd{"hostfwd="};

QLatin1StringView wire(PortForward::Protocol protocol) {
    return protocol == PortForward::Protocol::Udp ? kUdp : kTcp;
}

}  // namespace

const QVector<PortForward>& PortForwards::own() {
    static const QVector<PortForward> entries = {
        {PortForward::Protocol::Tcp, 5102, 5102, false},
        {PortForward::Protocol::Tcp, 5103, 5103, false},
        {PortForward::Protocol::Tcp, 5445, 445, false},
    };
    return entries;
}

const QVector<PortPreset>& PortForwards::presets() {
    using Protocol = PortForward::Protocol;
    static const QVector<PortPreset> all = {
        {QStringLiteral("Sunshine / Moonlight"),
         QStringLiteral("What Moonlight needs to reach Sunshine in Windows from the network."),
         {
             {Protocol::Tcp, 47984, 47984, true},
             {Protocol::Tcp, 47989, 47989, true},
             {Protocol::Tcp, 48010, 48010, true},
             {Protocol::Udp, 47998, 47998, true},
             {Protocol::Udp, 47999, 47999, true},
             {Protocol::Udp, 48000, 48000, true},
         }},
        {QStringLiteral("Remote Desktop"),
         QStringLiteral("Connect to the Windows desktop with a Remote Desktop client."),
         {
             {Protocol::Tcp, 3389, 3389, false},
             {Protocol::Udp, 3389, 3389, false},
         }},
        {QStringLiteral("SQL Server"),
         QStringLiteral("Reach a SQL Server that runs in Windows."),
         {
             {Protocol::Tcp, 1433, 1433, false},
         }},
        {QStringLiteral("SSH"),
         QStringLiteral("Reach the OpenSSH server of Windows."),
         {
             {Protocol::Tcp, 2222, 22, false},
         }},
        {QStringLiteral("Web server"),
         QStringLiteral("Reach a web server that runs in Windows."),
         {
             {Protocol::Tcp, 8080, 80, false},
             {Protocol::Tcp, 8443, 443, false},
         }},
    };
    return all;
}

bool PortForwards::applied(const QVector<PortForward>& entries, const PortPreset& preset) {
    return std::all_of(
        preset.ports.begin(), preset.ports.end(), [&entries](const PortForward& port) {
            return std::any_of(entries.begin(), entries.end(), [&port](const PortForward& entry) {
                return same_ports(entry, port);
            });
        });
}

QVector<PortForward> PortForwards::with(QVector<PortForward> entries, const PortPreset& preset,
                                        QStringList* refusals, int first_host_port) {
    for (const PortForward& port : preset.ports) {
        const bool present =
            std::any_of(entries.begin(), entries.end(),
                        [&port](const PortForward& entry) { return same_ports(entry, port); });
        if (present) {
            continue;
        }
        const QString reason = refusal(entries, port, first_host_port);
        if (reason.isEmpty()) {
            entries.push_back(port);
        } else if (refusals != nullptr) {
            *refusals << reason;
        }
    }
    return entries;
}

QVector<PortForward> PortForwards::without(QVector<PortForward> entries, const PortPreset& preset) {
    entries.removeIf([&preset](const PortForward& entry) {
        return std::any_of(preset.ports.begin(), preset.ports.end(),
                           [&entry](const PortForward& port) { return same_ports(entry, port); });
    });
    return entries;
}

QString PortForwards::mapping(const PortPreset& preset) {
    QStringList parts;
    bool from_network = false;
    for (const PortForward& port : preset.ports) {
        parts << ports_text(port);
        from_network = from_network || port.from_network;
    }
    return QStringLiteral("%1. %2").arg(
        parts.join(QStringLiteral(", ")),
        from_network ? QStringLiteral("Reachable from the network.")
                     : QStringLiteral("Reachable from this computer only; switch a row to "
                                      "open it to the network."));
}

int PortForwards::first_unprivileged_port() {
    QFile file(QStringLiteral("/proc/sys/net/ipv4/ip_unprivileged_port_start"));
    if (!file.open(QIODevice::ReadOnly)) {
        return kUsualFirstUnprivilegedPort;
    }
    bool ok = false;
    const int first = file.readAll().trimmed().toInt(&ok);
    return ok ? first : kUsualFirstUnprivilegedPort;
}

QString PortForwards::refusal(const QVector<PortForward>& entries, const PortForward& candidate,
                              int first_host_port) {
    if (!in_range(candidate.host_port) || !in_range(candidate.guest_port)) {
        return QStringLiteral("A port is a number from %1 to %2.")
            .arg(kLowestPort)
            .arg(kHighestPort);
    }
    if (candidate.host_port < first_host_port) {
        return QStringLiteral("This computer lets only the administrator listen on ports below %1.")
            .arg(first_host_port);
    }
    const auto taken = [&candidate](const PortForward& entry) {
        return same_host_port(entry, candidate);
    };
    if (std::any_of(own().begin(), own().end(), taken)) {
        return QStringLiteral("LSW itself uses %1 port %2 of this computer.")
            .arg(protocol_name(candidate.protocol))
            .arg(candidate.host_port);
    }
    if (std::any_of(entries.begin(), entries.end(), taken)) {
        return QStringLiteral("%1 port %2 of this computer is already forwarded.")
            .arg(protocol_name(candidate.protocol))
            .arg(candidate.host_port);
    }
    return {};
}

QVector<PortForward> PortForwards::accepted(const QVector<PortForward>& entries,
                                            int first_host_port) {
    QVector<PortForward> out;
    for (const PortForward& entry : entries) {
        if (refusal(out, entry, first_host_port).isEmpty()) {
            out.push_back(entry);
        }
    }
    return out;
}

QString PortForwards::encode(const QVector<PortForward>& entries) {
    QStringList records;
    records.reserve(entries.size());
    for (const PortForward& entry : entries) {
        records << QStringLiteral("%1:%2:%3:%4")
                       .arg(wire(entry.protocol))
                       .arg(entry.host_port)
                       .arg(entry.guest_port)
                       .arg(entry.from_network ? kNetwork : kLocal);
    }
    return records.join(QLatin1Char(' '));
}

QVector<PortForward> PortForwards::decode(const QString& blob) {
    QVector<PortForward> out;
    for (const QString& record : blob.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QStringList fields = record.split(QLatin1Char(':'));
        if (fields.size() != 4 || (fields.at(0) != kTcp && fields.at(0) != kUdp) ||
            (fields.at(3) != kLocal && fields.at(3) != kNetwork)) {
            continue;
        }
        PortForward entry;
        entry.protocol =
            fields.at(0) == kUdp ? PortForward::Protocol::Udp : PortForward::Protocol::Tcp;
        entry.host_port = fields.at(1).toInt();
        entry.guest_port = fields.at(2).toInt();
        entry.from_network = fields.at(3) == kNetwork;
        out.push_back(entry);
    }
    return out;
}

QString PortForwards::protocol_name(PortForward::Protocol protocol) {
    return protocol == PortForward::Protocol::Udp ? QStringLiteral("UDP") : QStringLiteral("TCP");
}

QString PortForwards::ports_text(const PortForward& entry) {
    if (entry.host_port == entry.guest_port) {
        return QStringLiteral("%1 %2").arg(protocol_name(entry.protocol)).arg(entry.host_port);
    }
    return QStringLiteral("%1 %2 → %3")
        .arg(protocol_name(entry.protocol))
        .arg(entry.host_port)
        .arg(entry.guest_port);
}

QString PortForwards::hostfwd(const PortForward& entry) {
    return QStringLiteral("%1%2:%3:%4-:%5")
        .arg(kHostfwd, wire(entry.protocol), entry.from_network ? kEveryAddress : kLoopback)
        .arg(entry.host_port)
        .arg(entry.guest_port);
}

std::optional<PortForward> PortForwards::from_hostfwd(const QString& option) {
    if (!option.startsWith(kHostfwd)) {
        return std::nullopt;
    }
    const QStringList sides = option.mid(kHostfwd.size()).split(QLatin1Char('-'));
    if (sides.size() != 2) {
        return std::nullopt;
    }
    const QStringList host = sides.at(0).split(QLatin1Char(':'));
    const QStringList guest = sides.at(1).split(QLatin1Char(':'));
    if (host.size() != 3 || guest.size() != 2 || (host.at(0) != kTcp && host.at(0) != kUdp)) {
        return std::nullopt;
    }
    PortForward entry;
    entry.protocol = host.at(0) == kUdp ? PortForward::Protocol::Udp : PortForward::Protocol::Tcp;
    entry.from_network = host.at(1) != kLoopback;
    entry.host_port = host.at(2).toInt();
    entry.guest_port = guest.at(1).toInt();
    if (!in_range(entry.host_port) || !in_range(entry.guest_port)) {
        return std::nullopt;
    }
    return entry;
}

}  // namespace lsw::host::config
