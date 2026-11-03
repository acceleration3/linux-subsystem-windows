#include "lsw/host/services/host_share_layout.hpp"

#include <QDir>
#include <QFileInfo>

namespace lsw::host::services {

namespace {

QString clean(const QString& raw) {
    QString out;
    for (const char32_t c : raw.toUcs4()) {
        const bool unsafe = c < 0x20 || (c >= 0x7f && c < 0xa0) || c == '"' || c == '\\' || c == 0xfffd;
        out += unsafe ? QStringLiteral("_") : QString::fromUcs4(&c, 1);
    }
    return out.trimmed();
}

QString fit(const QString& text, int bytes) {
    QString out;
    int used = 0;
    for (const char32_t c : text.toUcs4()) {
        const QString piece = QString::fromUcs4(&c, 1);
        const int size = static_cast<int>(piece.toUtf8().size());
        if (used + size > bytes) {
            break;
        }
        out += piece;
        used += size;
    }
    return out.trimmed();
}

}

QString HostShareLayout::tag_for(const QString& host_path, const QStringList& taken) {
    QString base = clean(QFileInfo(QDir::cleanPath(host_path)).fileName());
    if (base.isEmpty()) {
        base = QStringLiteral("Host");
    }
    for (int n = 1;; ++n) {
        const QString suffix = n == 1 ? QString{} : QStringLiteral(" %1").arg(n);
        const QString tag = fit(base, kMaxTagBytes - static_cast<int>(suffix.toUtf8().size())) + suffix;
        if (!taken.contains(tag, Qt::CaseInsensitive)) {
            return tag;
        }
    }
}

QVector<vm::DomainShare> HostShareLayout::assign(const QVector<config::HostShare>& shares) {
    QVector<vm::DomainShare> out;
    QStringList taken;
    for (const auto& share : shares) {
        vm::DomainShare mapped;
        mapped.tag = tag_for(share.host_path, taken);
        mapped.host_path = share.host_path;
        mapped.read_only = share.read_only;
        mapped.drive = share.drive.left(1).toLower();
        taken << mapped.tag;
        out.push_back(mapped);
    }
    return out;
}

}
