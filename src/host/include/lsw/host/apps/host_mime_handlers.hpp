#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::apps {
class HostMimeHandlers {
public:
    struct Dirs {
        QString config_home;
        QString data_home;
        QStringList data_dirs;

        [[nodiscard]] static Dirs from_environment();
    };

    explicit HostMimeHandlers(const Dirs& dirs);

    [[nodiscard]] bool has_host_handler(const QString& type) const;

    [[nodiscard]] bool explicit_default(const QString& type, const QString& entry) const;

private:
    [[nodiscard]] bool installed(const QString& id) const;

    Dirs dirs_;
    QVector<QHash<QString, QStringList>> caches_;
    QHash<QString, QStringList> defaults_;
    mutable QHash<QString, bool> installed_;
};

}
