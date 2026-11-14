#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::platform {
class HostAddresses {
public:
    HostAddresses() = delete;

    struct Interface {
        QString name;
        bool up = false;
        bool loopback = false;
        QStringList ipv4;
    };

    struct Address {
        QString address;
        QString interface;

        [[nodiscard]] bool operator==(const Address&) const = default;
    };

    [[nodiscard]] static QVector<Interface> system();

    [[nodiscard]] static QVector<Address> reachable(const QVector<Interface>& interfaces);

    [[nodiscard]] static QStringList of(const QVector<Interface>& interfaces, const QString& name);
};

}
