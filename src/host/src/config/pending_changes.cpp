#include "lsw/host/config/pending_changes.hpp"

#include <QHash>
#include <algorithm>
#include <QStringList>

namespace lsw::host::config {

namespace {

using K = ManagerSettings::Key;

// What the apply bar calls each key. Short enough to fit several on one
// line, because that bar's job is to be readable at a glance rather
// than complete.
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

// A list-valued setting has no useful "8 becomes 12" form, so it says
// how many entries it will have instead.
QString render(const QString& key, const QVariant& value) {
    if (key == K::kHostShares) {
        return QStringLiteral("%1 folders")
            .arg(ManagerSettings::decode_host_shares(value.toString()).size());
    }
    // A GPU list says how many; a UUID is long, and its first group is
    // what a person matches against the Graphics page.
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

}  // namespace

PendingChanges::PendingChanges(ManagerSettings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

bool PendingChanges::stage(const QString& key, const QVariant& value) {
    const QVariant current = settings_.value(key);
    if (current == value) {
        // Back to where it started. Nothing to apply, and nothing to
        // show in the bar.
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

    // A staged share of the folder, or of a parent: already the answer.
    std::optional<HostShare> chosen;
    for (const auto& share : std::as_const(staged)) {
        if (folder == share.host_path || folder.startsWith(share.host_path + QLatin1Char('/'))) {
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
        // Stored now what the staged list also holds: no longer a difference.
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

}  // namespace lsw::host::config
