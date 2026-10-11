#include "lsw/host/provision/debloat/registry_tweaks.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <QFile>

#include <hivex.h>

#include <vector>

namespace lsw::host::provision::debloat {

namespace {

constexpr auto kSoftware = "SOFTWARE";
constexpr auto kSystem = "SYSTEM";
constexpr auto kDefault = "DEFAULT";
constexpr auto kNtUser = "NTUSER";

// Consumer content: the Start-menu suggestions, the "recommended" apps
// and the silent installs that arrive after first logon. These are the
// reason a fresh Windows grows applications nobody asked for, and none
// of them are packages that could be removed from the image instead.
[[nodiscard]] QVector<RegistryTweak> content_delivery() {
    const QString key =
        QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager");
    QVector<RegistryTweak> out;
    for (const char* name : {"ContentDeliveryAllowed", "FeatureManagementEnabled",
                             "OemPreInstalledAppsEnabled", "PreInstalledAppsEnabled",
                             "PreInstalledAppsEverEnabled", "SilentInstalledAppsEnabled",
                             "SoftLandingEnabled", "SubscribedContentEnabled",
                             "SystemPaneSuggestionsEnabled"}) {
        out.push_back({kNtUser, key, QString::fromLatin1(name), 0});
    }
    return out;
}

// Telemetry and the personalisation that feeds on it.
[[nodiscard]] QVector<RegistryTweak> privacy() {
    return {
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\DataCollection"),
         QStringLiteral("AllowTelemetry"), 0},
        {kSystem, QStringLiteral("ControlSet001\\Services\\dmwappushservice"),
         QStringLiteral("Start"), 4},
        {kNtUser, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo"),
         QStringLiteral("Enabled"), 0},
        {kNtUser, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Privacy"),
         QStringLiteral("TailoredExperiencesWithDiagnosticDataEnabled"), 0},
        {kNtUser, QStringLiteral("Software\\Microsoft\\Input\\TIPC"), QStringLiteral("Enabled"), 0},
        {kNtUser, QStringLiteral("Software\\Microsoft\\InputPersonalization"),
         QStringLiteral("RestrictImplicitInkCollection"), 1},
        {kNtUser, QStringLiteral("Software\\Microsoft\\InputPersonalization"),
         QStringLiteral("RestrictImplicitTextCollection"), 1},
        {kNtUser, QStringLiteral("Software\\Microsoft\\InputPersonalization\\TrainedDataStore"),
         QStringLiteral("HarvestContacts"), 0},
        {kNtUser,
         QStringLiteral("Software\\Microsoft\\Speech_OneCore\\Settings\\OnlineSpeechPrivacy"),
         QStringLiteral("HasAccepted"), 0},
    };
}

// The shell surfaces a guest running one application has no use for,
// and which cost memory on every logon.
[[nodiscard]] QVector<RegistryTweak> shell() {
    return {
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\CloudContent"),
         QStringLiteral("DisableWindowsConsumerFeatures"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\CloudContent"),
         QStringLiteral("DisableCloudOptimizedContent"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\CloudContent"),
         QStringLiteral("DisableConsumerAccountStateContent"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\WindowsCopilot"),
         QStringLiteral("TurnOffWindowsCopilot"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\Explorer"),
         QStringLiteral("DisableSearchBoxSuggestions"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\Windows Chat"),
         QStringLiteral("ChatIcon"), 3},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\PushToInstall"),
         QStringLiteral("DisablePushToInstall"), 1},
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\OneDrive"),
         QStringLiteral("DisableFileSyncNGSC"), 1},
        {kNtUser,
         QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"),
         QStringLiteral("TaskbarMn"), 0},
    };
}

// Game Bar, whose package goes with the Xbox apps. Its files go, but
// Windows still asks for it when a game starts or the controller's Xbox
// button is pressed, and with nothing to answer ``ms-gamingoverlay:``
// the shell puts up "Get an app to open this link" over the game. That
// window is no application's, so it is never mirrored, and it takes
// every click meant for the window under it.
[[nodiscard]] QVector<RegistryTweak> game_bar() {
    return {
        {kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\GameDVR"),
         QStringLiteral("AllowGameDVR"), 0},
        {kNtUser, QStringLiteral("System\\GameConfigStore"), QStringLiteral("GameDVR_Enabled"), 0},
        {kNtUser, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR"),
         QStringLiteral("AppCaptureEnabled"), 0},
        {kNtUser, QStringLiteral("Software\\Microsoft\\GameBar"),
         QStringLiteral("UseNexusForGameBarEnabled"), 0},
    };
}

// Setup-time gates. LSW's domain has no TPM and boots firmware with no
// Secure Boot keys, so an unmodified Windows 11 installer refuses to run at
// all — these are what make the guest installable rather than what make
// it smaller, and they apply at every level including None.
[[nodiscard]] QVector<RegistryTweak> setup_bypasses() {
    QVector<RegistryTweak> out;
    for (const char* name : {"BypassCPUCheck", "BypassRAMCheck", "BypassSecureBootCheck",
                             "BypassStorageCheck", "BypassTPMCheck"}) {
        out.push_back({kSystem, QStringLiteral("Setup\\LabConfig"), QString::fromLatin1(name), 1});
    }
    out.push_back({kSystem, QStringLiteral("Setup\\MoSetup"),
                   QStringLiteral("AllowUpgradesWithUnsupportedTPMOrCPU"), 1});
    for (const char* hive : {kDefault, kNtUser}) {
        for (const char* name : {"SV1", "SV2"}) {
            out.push_back({QString::fromLatin1(hive),
                           QStringLiteral("Control Panel\\UnsupportedHardwareNotificationCache"),
                           QString::fromLatin1(name), 0});
        }
    }
    // Device encryption on a guest whose disk the host must be able to
    // read back is a way to lose the disk.
    out.push_back({kSystem, QStringLiteral("ControlSet001\\Control\\BitLocker"),
                   QStringLiteral("PreventDeviceEncryption"), 1});
    return out;
}

// Turning the servicing layer off. Only at Maximum, and only because
// its files are gone by then: leaving the services pointing at removed
// binaries is what turns a trimmed image into a boot loop.
[[nodiscard]] QVector<RegistryTweak> disable_servicing() {
    QVector<RegistryTweak> out;
    for (const char* service : {"WinDefend", "WdNisSvc", "WdNisDrv", "WdFilter", "Sense",
                                "wuauserv", "WaaSMedicSvc", "UsoSvc"}) {
        out.push_back({kSystem,
                       QStringLiteral("ControlSet001\\Services\\%1").arg(QLatin1String(service)),
                       QStringLiteral("Start"), 4});
    }
    out.push_back({kSoftware, QStringLiteral("Policies\\Microsoft\\Windows\\WindowsUpdate\\AU"),
                   QStringLiteral("NoAutoUpdate"), 1});
    return out;
}

// hivex works in the hive's own encoding for names; the paths here are
// ASCII, so a latin1 round trip is exact and avoids dragging a
// conversion library in for key names.
[[nodiscard]] QByteArray hive_name(const QString& s) { return s.toLatin1(); }

// Walk to ``key``, creating each missing level. Returns 0 on failure.
[[nodiscard]] hive_node_h ensure_key(hive_h* hive, const QString& key) {
    hive_node_h node = hivex_root(hive);
    if (node == 0) {
        return 0;
    }
    for (const QString& part : key.split(QLatin1Char('\\'), Qt::SkipEmptyParts)) {
        hive_node_h child = hivex_node_get_child(hive, node, hive_name(part).constData());
        if (child == 0) {
            child = hivex_node_add_child(hive, node, hive_name(part).constData());
        }
        if (child == 0) {
            return 0;
        }
        node = child;
    }
    return node;
}

}  // namespace

QVector<RegistryTweak> RegistryTweaks::for_level(DebloatLevel level) {
    // The setup bypasses are not a debloat: without them the installer
    // refuses this domain outright, so they go in whatever was chosen.
    QVector<RegistryTweak> out = setup_bypasses();
    if (level == DebloatLevel::None) {
        return out;
    }
    out += content_delivery();
    out += privacy();
    out += shell();
    out += game_bar();
    if (level == DebloatLevel::Maximum) {
        out += disable_servicing();
    }
    return out;
}

bool RegistryTweaks::apply(const QString& config_dir, const QString& ntuser_path,
                           const QVector<RegistryTweak>& tweaks, QString* error_out) {
    // Group by hive: each file is opened once, written once, committed
    // once. Opening per tweak would rewrite SOFTWARE forty times.
    QStringList hives;
    for (const RegistryTweak& tweak : tweaks) {
        if (!hives.contains(tweak.hive)) {
            hives.append(tweak.hive);
        }
    }

    for (const QString& name : hives) {
        const QString path = name == QLatin1String(kNtUser)
                                 ? ntuser_path
                                 : QDir(config_dir).filePath(name);
        if (!QFile::exists(path)) {
            LSW_INFO("debloat: no {} hive in this image; its tweaks are skipped",
                     name.toStdString());
            continue;
        }
        hive_h* hive = hivex_open(path.toUtf8().constData(), HIVEX_OPEN_WRITE);
        if (hive == nullptr) {
            if (error_out != nullptr) {
                *error_out = QStringLiteral("cannot open the %1 hive at %2").arg(name, path);
            }
            return false;
        }

        int written = 0;
        for (const RegistryTweak& tweak : tweaks) {
            if (tweak.hive != name) {
                continue;
            }
            const hive_node_h node = ensure_key(hive, tweak.key);
            if (node == 0) {
                hivex_close(hive);
                if (error_out != nullptr) {
                    *error_out = QStringLiteral("cannot create %1\\%2").arg(name, tweak.key);
                }
                return false;
            }
            // Little-endian DWORD, which is what REG_DWORD is on every
            // architecture Windows ships on.
            std::vector<char> data(4);
            for (int byte = 0; byte < 4; ++byte) {
                data[static_cast<std::size_t>(byte)] =
                    static_cast<char>((tweak.dword >> (8 * byte)) & 0xFFU);
            }
            const QByteArray value_name = tweak.value.toLatin1();
            hive_set_value set{};
            set.key = const_cast<char*>(value_name.constData());
            set.t = hive_t_dword;
            set.len = data.size();
            set.value = data.data();
            if (hivex_node_set_value(hive, node, &set, 0) != 0) {
                hivex_close(hive);
                if (error_out != nullptr) {
                    *error_out =
                        QStringLiteral("cannot set %1\\%2\\%3").arg(name, tweak.key, tweak.value);
                }
                return false;
            }
            ++written;
        }

        if (hivex_commit(hive, nullptr, 0) != 0) {
            hivex_close(hive);
            if (error_out != nullptr) {
                *error_out = QStringLiteral("cannot write the %1 hive back").arg(name);
            }
            return false;
        }
        hivex_close(hive);
        LSW_INFO("debloat: {} values written to the {} hive", written, name.toStdString());
    }
    return true;
}

}  // namespace lsw::host::provision::debloat
