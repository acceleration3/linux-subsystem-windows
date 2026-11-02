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
    out.push_back({kSystem, QStringLiteral("ControlSet001\\Control\\BitLocker"),
                   QStringLiteral("PreventDeviceEncryption"), 1});
    return out;
}

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

[[nodiscard]] QByteArray hive_name(const QString& s) { return s.toLatin1(); }

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

}

QVector<RegistryTweak> RegistryTweaks::for_level(DebloatLevel level) {
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

}
