#include "lsw/host/platform/mount_table.hpp"

#include "lsw/host/platform/guest_smb_share.hpp"

#include <QFile>
#include <QList>

namespace lsw::host::platform {

QString MountTable::unescape(const QByteArray& field) {
    QByteArray out;
    out.reserve(field.size());
    for (qsizetype i = 0; i < field.size(); ++i) {
        const auto digit = [&](qsizetype at) { return field[at] >= '0' && field[at] <= '7'; };
        if (field[i] == '\\' && i + 3 < field.size() && digit(i + 1) && digit(i + 2) &&
            digit(i + 3)) {
            out.append(static_cast<char>(((field[i + 1] - '0') << 6) | ((field[i + 2] - '0') << 3) |
                                         (field[i + 3] - '0')));
            i += 3;
        } else {
            out.append(field[i]);
        }
    }
    return QString::fromUtf8(out);
}

MountTable MountTable::parse(const QByteArray& mountinfo) {
    MountTable table;
    for (const QByteArray& line : mountinfo.split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 10) {
            continue;
        }
        qsizetype dash = -1;
        for (qsizetype i = 6; i < fields.size(); ++i) {
            if (fields[i] == "-") {
                dash = i;
                break;
            }
        }
        if (dash < 0 || dash + 2 >= fields.size()) {
            continue;
        }
        Entry entry;
        entry.mount_point = unescape(fields[4]);
        entry.fstype = QString::fromUtf8(fields[dash + 1]);
        entry.source = unescape(fields[dash + 2]);
        if (dash + 3 < fields.size()) {
            entry.options = QString::fromUtf8(fields[dash + 3]);
        }
        table.entries_.push_back(std::move(entry));
    }
    return table;
}

MountTable MountTable::read(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return parse(file.readAll());
}

const MountTable::Entry* MountTable::find(const QString& mount_point) const {
    for (qsizetype i = entries_.size(); i > 0; --i) {
        if (entries_[i - 1].mount_point == mount_point) {
            return &entries_[i - 1];
        }
    }
    return nullptr;
}

QVector<MountTable::Entry> MountTable::guest_shares() const {
    QVector<Entry> found;
    for (const Entry& entry : entries_) {
        if (entry.fstype == kFuseType && GuestSmbShare::is_mount_source(entry.source)) {
            found.push_back(entry);
        }
    }
    return found;
}

}  // namespace lsw::host::platform
