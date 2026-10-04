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
        {K::kCpuCores, QStringLiteral("cores")},
        {K::kMemoryMib, QStringLiteral("memory")},
        {K::kDiskGib, QStringLiteral("disk")},
        {K::kGuestWidth, QStringLiteral("width")},
        {K::kGuestHeight, QStringLiteral("height")},
        {K::kMatchHostOutput, QStringLiteral("match output")},
        {K::kScalingPercent, QStringLiteral("scaling")},
        {K::kVramMib, QStringLiteral("video memory")},
        {K::kChannels, QStringLiteral("channels")},
        {K::kRingKib, QStringLiteral("ring")},
        {K::kReplyKib, QStringLiteral("reply")},
        {K::kBlobMib, QStringLiteral("blob")},
        {K::kHostShares, QStringLiteral("shared folders")},
        {K::kAgentPort, QStringLiteral("agent port")},
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
