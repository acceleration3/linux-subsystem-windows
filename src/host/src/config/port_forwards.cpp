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

QLatin1StringView wire(PortForward::Protocol protocol) {
    return protocol == PortForward::Protocol::Udp ? kUdp : kTcp;
}

}

const QVector<PortForward>& PortForwards::own() {
    static const QVector<PortForward> entries = {
        {PortForward::Protocol::Tcp, 5102, 5102, false},
        {PortForward::Protocol::Tcp, 5103, 5103, false},
        {PortForward::Protocol::Tcp, 5445, 445, false},
    };
    return entries;
}

QVector<PortForward> PortForwards::sunshine() {
    return {
        {PortForward::Protocol::Tcp, 47984, 47984, true},
        {PortForward::Protocol::Tcp, 47989, 47989, true},
        {PortForward::Protocol::Tcp, 48010, 48010, true},
        {PortForward::Protocol::Udp, 47998, 47998, true},
        {PortForward::Protocol::Udp, 47999, 47999, true},
        {PortForward::Protocol::Udp, 48000, 48000, true},
    };
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
        return QStringLiteral("A port is a number from %1 to %2.").arg(kLowestPort).arg(kHighestPort);
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
        if (fields.size() != 4 || (fields.at(0) != kTcp && fields.at(0) != kUdp)
            || (fields.at(3) != kLocal && fields.at(3) != kNetwork)) {
            continue;
        }
        PortForward entry;
        entry.protocol = fields.at(0) == kUdp ? PortForward::Protocol::Udp
                                              : PortForward::Protocol::Tcp;
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

QString PortForwards::hostfwd(const PortForward& entry) {
    return QStringLiteral("hostfwd=%1:%2:%3-:%4")
        .arg(wire(entry.protocol), entry.from_network ? kEveryAddress : kLoopback)
        .arg(entry.host_port)
        .arg(entry.guest_port);
}

}
