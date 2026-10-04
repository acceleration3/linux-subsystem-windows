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
        {K::kCpuCores, 4},
        {K::kMemoryMib, 8192},
        {K::kDiskGib, 60},

        {K::kGuestWidth, 1920},
        {K::kGuestHeight, 1080},
        {K::kMatchHostOutput, false},
        {K::kScalingPercent, 100},
        {K::kRefreshHz, 60},
        // What the adapter admits to owning. Applications gate their
        // GPU path on this long before they open a driver, and a basic
        // display adapter reports zero.
        {K::kVramMib, 1024},
        {K::kLivePreview, true},

        // The render server's own defaults, so a fresh install matches
        // what it would have done unasked.
        {K::kChannels, 4},
        // -1: whichever GPU drives the display, which is what EGL hands
        // out unasked. Named explicitly so a laptop's discrete card can
        // be chosen over the one wired to the screen.
        {K::kGlDevice, -1},
        {K::kRingKib, 1024},
        {K::kReplyKib, 8192},
        // 512, not 256: the three areas plus the header round up to a
        // 1 GiB region, which is the size every domain defined so far
        // declares. A smaller default rounds to 512 and a guest whose
        // XML says 1024 then maps a region the server never made.
        {K::kBlobMib, 512},
        {K::kWarnCongestion, true},
        {K::kChannelBindings, QString{}},
        {K::kNothingFreePolicy, QStringLiteral("wait")},
        {K::kChannelWaitSeconds, 15},

        {K::kSmbEnabled, true},
        {K::kShareScope, QStringLiteral("C$")},
        {K::kBookmarkDolphin, true},
        {K::kBookmarkGtk, true},
        {K::kHostShares, QString{}},
        {K::kGuestUser, QStringLiteral("lsw")},
        {K::kGuestPassword, QStringLiteral("lsw")},

        {K::kPublishEntries, true},
        {K::kMenuCategory, QStringLiteral("Windows")},
        {K::kFileAssociations, false},

        {K::kAgentPort, 5102},
        {K::kAutostart, false},

        {K::kCloseToTray, true},
    };
    return table;
}

// Changing one of these rewrites the domain XML or the render server's
// command line, neither of which a running guest re-reads.
const QStringList& restart_keys() {
    static const QStringList keys = {
        K::kCpuCores,        K::kMemoryMib,      K::kDiskGib, K::kGuestWidth, K::kGuestHeight,
        K::kMatchHostOutput, K::kScalingPercent, K::kVramMib, K::kChannels,   K::kGlDevice,
        K::kRingKib,         K::kReplyKib,       K::kBlobMib, K::kHostShares, K::kAgentPort,
    };
    return keys;
}

constexpr QChar kRecordSep = QLatin1Char('\x1f');
constexpr QChar kFieldSep = QLatin1Char('\x1e');

}  // namespace

ManagerSettings::ManagerSettings(QObject* parent)
    : QObject(parent), store_(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"),
                              QStringLiteral("manager")) {
    // The wizard sizes the machine against the host it runs on, and a
    // four-core default on a twenty-thread box is a poor first
    // impression. Only used when nothing has been stored yet.
    if (!store_.contains(K::kCpuCores)) {
        const auto threads = static_cast<int>(std::thread::hardware_concurrency());
        if (threads > 1) {
            store_.setValue(K::kCpuCores, qMin(threads, 8));
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

QString ManagerSettings::encode_records(const QVector<QStringList>& records) {
    QStringList joined;
    joined.reserve(records.size());
    for (const auto& fields : records) {
        joined << fields.join(kFieldSep);
    }
    return joined.join(kRecordSep);
}

QVector<QStringList> ManagerSettings::decode_records(const QString& blob) {
    QVector<QStringList> out;
    const auto records = blob.split(kRecordSep, Qt::SkipEmptyParts);
    out.reserve(records.size());
    for (const auto& record : records) {
        out.push_back(record.split(kFieldSep));
    }
    return out;
}

QString ManagerSettings::encode_host_shares(const QVector<HostShare>& shares) {
    QVector<QStringList> records;
    records.reserve(shares.size());
    for (const auto& share : shares) {
        records.push_back({share.host_path, share.drive,
                           share.read_only ? QStringLiteral("ro") : QStringLiteral("rw")});
    }
    return encode_records(records);
}

QVector<HostShare> ManagerSettings::decode_host_shares(const QString& blob) {
    QVector<HostShare> out;
    for (const auto& fields : decode_records(blob)) {
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

QVector<HostShare> ManagerSettings::host_shares() const {
    return decode_host_shares(string_value(K::kHostShares));
}

void ManagerSettings::set_host_shares(const QVector<HostShare>& shares) {
    set_value(K::kHostShares, encode_host_shares(shares));
}

QVector<ChannelBinding> ManagerSettings::channel_bindings() const {
    QVector<ChannelBinding> out;
    for (const auto& fields : decode_records(string_value(K::kChannelBindings))) {
        ChannelBinding binding;
        binding.app_id = fields.at(0);
        binding.channel = fields.value(1, QStringLiteral("-1")).toInt();
        binding.busy = policy_from_string(fields.value(2));
        out.push_back(binding);
    }
    return out;
}

void ManagerSettings::set_channel_bindings(const QVector<ChannelBinding>& bindings) {
    QVector<QStringList> records;
    records.reserve(bindings.size());
    for (const auto& binding : bindings) {
        records.push_back(
            {binding.app_id, QString::number(binding.channel), policy_to_string(binding.busy)});
    }
    set_value(K::kChannelBindings, encode_records(records));
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
