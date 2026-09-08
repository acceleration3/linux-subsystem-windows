#include "lsw/host/config/manager_settings.hpp"

#include <QHash>
#include <QStringList>

#include <thread>

namespace lsw::host::config {

namespace {

using K = ManagerSettings::Key;

// The defaults, in one table. A key absent from here is not one of
// ours, which is what ``default_value`` returns an invalid variant to
// say.
const QHash<QString, QVariant>& defaults() {
    static const QHash<QString, QVariant> table = {
        {QLatin1String(K::kCpuCores), 4},
        {QLatin1String(K::kMemoryMib), 8192},
        {QLatin1String(K::kDiskGib), 60},

        {QLatin1String(K::kGuestWidth), 1920},
        {QLatin1String(K::kGuestHeight), 1080},
        {QLatin1String(K::kMatchHostOutput), false},
        {QLatin1String(K::kScalingPercent), 100},
        {QLatin1String(K::kRefreshHz), 60},
        // What the adapter admits to owning. Applications gate their
        // GPU path on this long before they open a driver, and a basic
        // display adapter reports zero.
        {QLatin1String(K::kVramMib), 1024},
        {QLatin1String(K::kLivePreview), true},

        // The render server's own defaults, so a fresh install matches
        // what it would have done unasked.
        {QLatin1String(K::kChannels), 4},
        {QLatin1String(K::kRingKib), 1024},
        {QLatin1String(K::kReplyKib), 8192},
        // 512, not 256: the three areas plus the header round up to a
        // 1 GiB region, which is the size every domain defined so far
        // declares. A smaller default rounds to 512 and a guest whose
        // XML says 1024 then maps a region the server never made.
        {QLatin1String(K::kBlobMib), 512},
        {QLatin1String(K::kWarnCongestion), true},
        {QLatin1String(K::kChannelBindings), QString{}},
        {QLatin1String(K::kNothingFreePolicy), QStringLiteral("wait")},
        {QLatin1String(K::kChannelWaitSeconds), 15},

        {QLatin1String(K::kSmbEnabled), true},
        {QLatin1String(K::kShareScope), QStringLiteral("C$")},
        {QLatin1String(K::kBookmarkDolphin), true},
        {QLatin1String(K::kBookmarkGtk), true},
        {QLatin1String(K::kHostShares), QString{}},
        {QLatin1String(K::kGuestUser), QStringLiteral("lsw")},
        {QLatin1String(K::kGuestPassword), QStringLiteral("lsw")},

        {QLatin1String(K::kPublishEntries), true},
        {QLatin1String(K::kMenuCategory), QStringLiteral("Windows")},
        {QLatin1String(K::kFileAssociations), false},

        {QLatin1String(K::kAgentPort), 5102},
        {QLatin1String(K::kAutostart), false},

        {QLatin1String(K::kCloseToTray), true},
    };
    return table;
}

// Changing one of these rewrites the domain XML or the render server's
// command line, neither of which a running guest re-reads.
const QStringList& restart_keys() {
    static const QStringList keys = {
        QLatin1String(K::kCpuCores),    QLatin1String(K::kMemoryMib),
        QLatin1String(K::kDiskGib),     QLatin1String(K::kGuestWidth),
        QLatin1String(K::kGuestHeight), QLatin1String(K::kMatchHostOutput),
        QLatin1String(K::kScalingPercent), QLatin1String(K::kVramMib),
        QLatin1String(K::kChannels),    QLatin1String(K::kRingKib),
        QLatin1String(K::kReplyKib),    QLatin1String(K::kBlobMib),
        QLatin1String(K::kHostShares),  QLatin1String(K::kAgentPort),
    };
    return keys;
}

constexpr QChar kRecordSep = QLatin1Char('\x1f');
constexpr QChar kFieldSep = QLatin1Char('\x1e');

}  // namespace

ManagerSettings::ManagerSettings(QObject* parent)
    : QObject(parent),
      store_(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"),
             QStringLiteral("manager")) {
    // The wizard sizes the machine against the host it runs on, and a
    // four-core default on a twenty-thread box is a poor first
    // impression. Only used when nothing has been stored yet.
    if (!store_.contains(QLatin1String(K::kCpuCores))) {
        const auto threads = static_cast<int>(std::thread::hardware_concurrency());
        if (threads > 1) {
            store_.setValue(QLatin1String(K::kCpuCores), qMin(threads, 8));
        }
    }
}

bool ManagerSettings::needs_restart(const QString& key) {
    return restart_keys().contains(key);
}

QVariant ManagerSettings::default_value(const QString& key) {
    return defaults().value(key, QVariant{});
}

QVariant ManagerSettings::value(const QString& key) const {
    return store_.value(key, default_value(key));
}

int ManagerSettings::int_value(const QString& key) const {
    return value(key).toInt();
}

bool ManagerSettings::bool_value(const QString& key) const {
    return value(key).toBool();
}

QString ManagerSettings::string_value(const QString& key) const {
    return value(key).toString();
}

void ManagerSettings::set_value(const QString& key, const QVariant& v) {
    if (store_.value(key, default_value(key)) == v) {
        return;
    }
    store_.setValue(key, v);
    store_.sync();
    Q_EMIT changed(key);
}

QVector<HostShare> ManagerSettings::host_shares() const {
    QVector<HostShare> out;
    const QString blob = string_value(QLatin1String(K::kHostShares));
    if (blob.isEmpty()) {
        return out;
    }
    const auto records = blob.split(kRecordSep, Qt::SkipEmptyParts);
    out.reserve(records.size());
    for (const auto& record : records) {
        const auto fields = record.split(kFieldSep);
        if (fields.size() < 2) {
            continue;
        }
        HostShare share;
        share.host_path = fields.at(0);
        share.drive = fields.at(1);
        share.read_only = fields.value(2) == QLatin1String("ro");
        out.push_back(share);
    }
    return out;
}

void ManagerSettings::set_host_shares(const QVector<HostShare>& shares) {
    QStringList records;
    records.reserve(shares.size());
    for (const auto& share : shares) {
        records << QStringList{share.host_path, share.drive,
                               share.read_only ? QStringLiteral("ro")
                                               : QStringLiteral("rw")}
                       .join(kFieldSep);
    }
    set_value(QLatin1String(K::kHostShares), records.join(kRecordSep));
}

QVector<ChannelBinding> ManagerSettings::channel_bindings() const {
    QVector<ChannelBinding> out;
    const QString blob = string_value(QLatin1String(K::kChannelBindings));
    if (blob.isEmpty()) {
        return out;
    }
    const auto records = blob.split(kRecordSep, Qt::SkipEmptyParts);
    out.reserve(records.size());
    for (const auto& record : records) {
        const auto fields = record.split(kFieldSep);
        if (fields.isEmpty()) {
            continue;
        }
        ChannelBinding binding;
        binding.app_id = fields.at(0);
        binding.channel = fields.value(1, QStringLiteral("-1")).toInt();
        binding.busy = policy_from_string(fields.value(2));
        out.push_back(binding);
    }
    return out;
}

void ManagerSettings::set_channel_bindings(const QVector<ChannelBinding>& bindings) {
    QStringList records;
    records.reserve(bindings.size());
    for (const auto& binding : bindings) {
        records << QStringList{binding.app_id, QString::number(binding.channel),
                               policy_to_string(binding.busy)}
                       .join(kFieldSep);
    }
    set_value(QLatin1String(K::kChannelBindings), records.join(kRecordSep));
}

QStringList ManagerSettings::free_drive_letters() const {
    QStringList taken;
    for (const auto& share : host_shares()) {
        taken << share.drive.toUpper();
    }
    QStringList out;
    // Backwards from Z, which is where people put a mapped drive, and
    // stopping before the letters Windows has already spoken for.
    for (char letter = 'Z'; letter >= 'E'; --letter) {
        const QString candidate = QString(QLatin1Char(letter)) + QLatin1Char(':');
        if (!taken.contains(candidate)) {
            out << candidate;
        }
    }
    return out;
}

QString ManagerSettings::policy_to_string(ChannelBusyPolicy policy) {
    switch (policy) {
    case ChannelBusyPolicy::Wait:
        return QStringLiteral("wait");
    case ChannelBusyPolicy::Refuse:
        return QStringLiteral("refuse");
    case ChannelBusyPolicy::TakeAnyFree:
        break;
    }
    return QStringLiteral("any");
}

ChannelBusyPolicy ManagerSettings::policy_from_string(const QString& text) {
    if (text == QLatin1String("wait")) {
        return ChannelBusyPolicy::Wait;
    }
    if (text == QLatin1String("refuse")) {
        return ChannelBusyPolicy::Refuse;
    }
    return ChannelBusyPolicy::TakeAnyFree;
}

QString ManagerSettings::policy_label(ChannelBusyPolicy policy) {
    switch (policy) {
    case ChannelBusyPolicy::Wait:
        return QStringLiteral("Wait for it");
    case ChannelBusyPolicy::Refuse:
        return QStringLiteral("Do not start");
    case ChannelBusyPolicy::TakeAnyFree:
        break;
    }
    return QStringLiteral("Take any free one");
}

}  // namespace lsw::host::config
