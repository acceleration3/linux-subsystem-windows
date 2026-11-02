#include "lsw/host/config/pending_changes.hpp"

#include <QDir>
#include <QHash>
#include <algorithm>
#include <QStringList>

namespace lsw::host::config {

namespace {

using K = ManagerSettings::Key;

QString label_for(const QString& key) {
    static const QHash<QString, QString> labels = {
        {K::kCpuCores, QStringLiteral("cores")},
        {K::kMemoryMib, QStringLiteral("memory")},
        {K::kDiskGib, QStringLiteral("disk")},
        {K::kHostGpu, QStringLiteral("display GPU")},
        {K::kHostWindowMib, QStringLiteral("host window")},
        {K::kRenderGpus, QStringLiteral("render GPUs")},
        {K::kHostShares, QStringLiteral("shared folders")},
    };
    return labels.value(key, key);
}

QString render(const QString& key, const QVariant& value) {
    if (key == K::kHostShares) {
        return QStringLiteral("%1 folders")
            .arg(ManagerSettings::decode_host_shares(value.toString()).size());
    }
    if (value.typeId() == QMetaType::QStringList) {
        return QString::number(value.toStringList().size());
    }
    if (key == K::kHostGpu) {
        const QString uuid = value.toString();
        return uuid.size() > 8 ? uuid.left(8) : uuid;
    }
    if (key == K::kHostWindowMib && value.toInt() % 1024 == 0) {
        return QStringLiteral("%1 GiB").arg(value.toInt() / 1024);
    }
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    }
    return value.toString();
}

}

PendingChanges::PendingChanges(ManagerSettings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

bool PendingChanges::stage(const QString& key, const QVariant& value) {
    const QVariant current = settings_.value(key);
    if (current == value) {
        if (staged_.remove(key) > 0) {
            Q_EMIT changed();
        }
        return !staged_.isEmpty();
    }
    if (staged_.value(key) == value) {
        return !staged_.isEmpty();
    }
    staged_.insert(key, value);
    Q_EMIT changed();
    return true;
}

QVariant PendingChanges::staged_or_current(const QString& key) const {
    const auto it = staged_.constFind(key);
    if (it != staged_.constEnd()) {
        return it.value();
    }
    return settings_.value(key);
}

QStringList PendingChanges::summary() const {
    QStringList out;
    out.reserve(staged_.size());
    for (auto it = staged_.constBegin(); it != staged_.constEnd(); ++it) {
        out << QStringLiteral("%1 %2 → %3")
                   .arg(label_for(it.key()), render(it.key(), settings_.value(it.key())),
                        render(it.key(), it.value()));
    }
    return out;
}

std::optional<HostShare> PendingChanges::add_share(const QString& folder) {
    QVector<HostShare> stored = settings_.host_shares();
    const bool has_staged = staged_.contains(K::kHostShares);
    QVector<HostShare> staged =
        has_staged ? ManagerSettings::decode_host_shares(staged_.value(K::kHostShares).toString()) : stored;

    std::optional<HostShare> chosen;
    for (const auto& share : std::as_const(staged)) {
        const QString root = QDir::cleanPath(share.host_path);
        if (folder == root || folder.startsWith(root + QLatin1Char('/'))) {
            chosen = share;
            break;
        }
    }
    if (!chosen) {
        QVector<HostShare> both = stored;
        for (const auto& share : std::as_const(staged)) {
            if (std::none_of(both.begin(), both.end(), [&](const HostShare& s) { return s.drive == share.drive; })) {
                both.push_back(share);
            }
        }
        const QStringList letters = ManagerSettings::free_drive_letters(both);
        if (letters.isEmpty()) {
            return std::nullopt;
        }
        chosen = HostShare{folder, letters.first(), false};
        if (has_staged) {
            staged.push_back(*chosen);
            stage(K::kHostShares, ManagerSettings::encode_host_shares(staged));
        }
    }
    const bool stored_already = std::any_of(stored.begin(), stored.end(), [&](const HostShare& s) {
        return s.host_path == chosen->host_path && s.drive == chosen->drive;
    });
    if (!stored_already) {
        stored.push_back(*chosen);
        settings_.set_host_shares(stored);
        if (has_staged) {
            stage(K::kHostShares, ManagerSettings::encode_host_shares(staged));
        }
    }
    return chosen;
}

void PendingChanges::commit() {
    for (auto it = staged_.constBegin(); it != staged_.constEnd(); ++it) {
        settings_.set_value(it.key(), it.value());
    }
    staged_.clear();
    Q_EMIT changed();
}

void PendingChanges::revert() {
    if (staged_.isEmpty()) {
        return;
    }
    staged_.clear();
    Q_EMIT changed();
}

}
