#include "lsw/host/platform/host_addresses.hpp"

#include <QHostAddress>
#include <QNetworkInterface>

namespace lsw::host::platform {

QVector<HostAddresses::Interface> HostAddresses::system() {
    QVector<Interface> out;
    for (const QNetworkInterface& found : QNetworkInterface::allInterfaces()) {
        Interface interface;
        interface.name = found.name();
        const auto flags = found.flags();
        interface.up = flags.testFlag(QNetworkInterface::IsUp)
                       && flags.testFlag(QNetworkInterface::IsRunning);
        interface.loopback = flags.testFlag(QNetworkInterface::IsLoopBack);
        for (const QNetworkAddressEntry& entry : found.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                interface.ipv4 << entry.ip().toString();
            }
        }
        out.push_back(interface);
    }
    return out;
}

QVector<HostAddresses::Address> HostAddresses::reachable(const QVector<Interface>& interfaces) {
    QVector<Address> out;
    for (const Interface& interface : interfaces) {
        if (!interface.up || interface.loopback) {
            continue;
        }
        for (const QString& text : interface.ipv4) {
            const QHostAddress address(text);
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()
                || address.isLinkLocal()) {
                continue;
            }
            out.push_back({text, interface.name});
        }
    }
    return out;
}

QStringList HostAddresses::of(const QVector<Interface>& interfaces, const QString& name) {
    for (const Interface& interface : interfaces) {
        if (interface.name == name) {
            return interface.ipv4;
        }
    }
    return {};
}

}
