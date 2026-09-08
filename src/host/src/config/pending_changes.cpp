#include "lsw/host/config/pending_changes.hpp"

#include <QHash>
#include <QStringList>

namespace lsw::host::config {

namespace {

using K = ManagerSettings::Key;

// What the apply bar calls each key. Short enough to fit several on one
// line, because that bar's job is to be readable at a glance rather
// than complete.
QString label_for(const QString& key) {
    static const QHash<QString, QString> labels = {
        {QLatin1String(K::kCpuCores), QStringLiteral("cores")},
        {QLatin1String(K::kMemoryMib), QStringLiteral("memory")},
        {QLatin1String(K::kDiskGib), QStringLiteral("disk")},
        {QLatin1String(K::kGuestWidth), QStringLiteral("width")},
        {QLatin1String(K::kGuestHeight), QStringLiteral("height")},
        {QLatin1String(K::kMatchHostOutput), QStringLiteral("match output")},
        {QLatin1String(K::kScalingPercent), QStringLiteral("scaling")},
        {QLatin1String(K::kVramMib), QStringLiteral("video memory")},
        {QLatin1String(K::kChannels), QStringLiteral("channels")},
        {QLatin1String(K::kRingKib), QStringLiteral("ring")},
        {QLatin1String(K::kReplyKib), QStringLiteral("reply")},
        {QLatin1String(K::kBlobMib), QStringLiteral("blob")},
        {QLatin1String(K::kHostShares), QStringLiteral("shared folders")},
        {QLatin1String(K::kAgentPort), QStringLiteral("agent port")},
    };
    return labels.value(key, key);
}

// A list-valued setting has no useful "8 becomes 12" form, so it says
// how many entries it will have instead.
QString render(const QString& key, const QVariant& value) {
    if (key == QLatin1String(K::kHostShares)) {
        const auto text = value.toString();
        const int count =
            text.isEmpty() ? 0 : static_cast<int>(text.count(QLatin1Char('\x1f')) + 1);
        return QStringLiteral("%1 folders").arg(count);
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

bool PendingChanges::has(const QString& key) const {
    return staged_.contains(key);
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
