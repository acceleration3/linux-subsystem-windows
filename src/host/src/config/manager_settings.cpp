#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/vm_sizing.hpp"

#include "lsw/host/config/display_modes.hpp"

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
        {K::kCpuCores, VmSizing::host_cores()},
        {K::kMemoryMib, 8192},
        {K::kDiskGib, 60},

        // accel-virt's default: the host monitors' own modes.
        {K::kDisplayModes, QStringList{}},
        {K::kMatchHostOutput, false},
        {K::kScalingPercent, 0},
        {K::kLivePreview, true},

        // "auto" is the device's own default, so a fresh install binds
        // the same GPU QEMU would have picked unasked.
        {K::kHostGpu, QStringLiteral("auto")},
        // What runs real applications. At 256 MiB the per-context arenas
        // run out and applications fail to create devices.
        {K::kHostWindowMib, 8192},
        // None: a second adapter is a second device the guest's driver
        // package has to be signed for, and most hosts have one GPU worth
        // using.
        {K::kRenderGpus, QStringList{}},

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

        {K::kAutostart, false},

        {K::kCloseToTray, true},

        {K::kLastBootMs, 0},
    };
    return table;
}

// Changing one of these rewrites the domain XML, which a running guest
// does not re-read. The display keys are not here: the resolution is
// pushed into the running device over QMP.
const QStringList& restart_keys() {
    static const QStringList keys = {
        K::kCpuCores,      K::kMemoryMib,  K::kDiskGib,    K::kHostGpu,
        K::kHostWindowMib, K::kRenderGpus, K::kHostShares,
    };
    return keys;
}

constexpr QChar kRecordSep = QLatin1Char('\x1f');
constexpr QChar kFieldSep = QLatin1Char('\x1e');

}  // namespace

ManagerSettings::ManagerSettings(QObject* parent)
    : QObject(parent), store_(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"),
                              QStringLiteral("manager")) {
}

QString ManagerSettings::file_path() const {
    return store_.fileName();
}

bool ManagerSettings::needs_restart(const QString& key) {
    return restart_keys().contains(key);
}

QVariant ManagerSettings::default_value(const QString& key) {
    return defaults().value(key, QVariant{});
}

QVariant ManagerSettings::value(const QString& key) const {
    const QVariant fallback = default_value(key);
    const QVariant stored = store_.value(key, fallback);
    // A list comes back from the INI file as whatever it looks like: an
    // empty one as an invalid variant, a one-element one as a string. Read
    // as the type the default has, so a staged list compares equal to the
    // same list stored.
    if (fallback.typeId() == QMetaType::QStringList) {
        return stored.toStringList();
    }
    return stored;
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

QStringList ManagerSettings::string_list_value(const QString& key) const {
    return value(key).toStringList();
}

void ManagerSettings::set_value(const QString& key, const QVariant& v) {
    if (value(key) == v) {
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

}  // namespace lsw::host::config
