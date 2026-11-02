#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lsw::host::platform {
class MountTable {
public:
    struct Entry {
        QString mount_point;
        QString fstype;
        QString source;
        QString options;
    };

    static inline const QString kFuseType = QStringLiteral("fuse.lsw-windows-fs");

    [[nodiscard]] static MountTable parse(const QByteArray& mountinfo);

    [[nodiscard]] static MountTable
    read(const QString& path = QStringLiteral("/proc/self/mountinfo"));

    [[nodiscard]] const QVector<Entry>& entries() const noexcept { return entries_; }

    [[nodiscard]] const Entry* find(const QString& mount_point) const;

    [[nodiscard]] QVector<Entry> guest_shares() const;

private:
    [[nodiscard]] static QString unescape(const QByteArray& field);

    QVector<Entry> entries_;
};

}  // namespace lsw::host::platform
