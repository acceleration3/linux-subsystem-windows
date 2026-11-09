#pragma once

#include <QLatin1StringView>
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVector>

namespace lsw::host::config {
struct HostShare {
    QString host_path;
    QString drive;
    bool read_only = false;

    [[nodiscard]] bool operator==(const HostShare& other) const noexcept {
        return host_path == other.host_path && drive == other.drive && read_only == other.read_only;
    }
};

class ManagerSettings : public QObject {
    Q_OBJECT
public:
    struct Key {
        static constexpr QLatin1StringView kCpuCores{"machine/cpu_cores"};
        static constexpr QLatin1StringView kMemoryMib{"machine/memory_mib"};
        static constexpr QLatin1StringView kDiskGib{"machine/disk_gib"};
        static constexpr QLatin1StringView kDisplayModes{"display/modes"};
        static constexpr QLatin1StringView kMatchHostOutput{"display/match_host_output"};
        static constexpr QLatin1StringView kDisplayRefresh{"display/refresh"};
        static constexpr QLatin1StringView kScalingPercent{"display/scaling_percent"};
        static constexpr QLatin1StringView kHostGpu{"graphics/host_gpu"};
        static constexpr QLatin1StringView kHostWindowMib{"graphics/host_window_mib"};
        static constexpr QLatin1StringView kVramReserveMib{"graphics/vram_reserve_mib"};
        static constexpr QLatin1StringView kRenderGpus{"graphics/render_gpus"};
        static constexpr QLatin1StringView kHybridGraphics{"graphics/hybrid_graphics"};
        static constexpr QLatin1StringView kSmbEnabled{"files/smb_enabled"};
        static constexpr QLatin1StringView kShareScope{"files/share_scope"};
        static constexpr QLatin1StringView kBookmarkDolphin{"files/bookmark_dolphin"};
        static constexpr QLatin1StringView kBookmarkGtk{"files/bookmark_gtk"};
        static constexpr QLatin1StringView kHostShares{"files/host_shares"};
        static constexpr QLatin1StringView kGuestUser{"files/guest_user"};
        static constexpr QLatin1StringView kGuestPassword{"files/guest_password"};
        static constexpr QLatin1StringView kPublishEntries{"desktop/publish"};
        static constexpr QLatin1StringView kMenuCategory{"desktop/category"};
        static constexpr QLatin1StringView kFileAssociations{"desktop/mime"};
        static constexpr QLatin1StringView kShareClipboard{"desktop/share_clipboard"};
        static constexpr QLatin1StringView kUsbAlways{"usb/always"};
        static constexpr QLatin1StringView kAutostart{"guest/autostart"};
        static constexpr QLatin1StringView kCloseToTray{"host/close_to_tray"};
        static constexpr QLatin1StringView kLogManager{"logs/manager"};
        static constexpr QLatin1StringView kLogMirrors{"logs/mirrors"};
        static constexpr QLatin1StringView kLogAgent{"logs/agent"};
        static constexpr QLatin1StringView kLogGuestMirrors{"logs/guest_mirrors"};
        static constexpr QLatin1StringView kLogGpu{"logs/gpu"};
        static constexpr QLatin1StringView kLogDebugUntil{"logs/debug_until"};
        static constexpr QLatin1StringView kLastBootMs{"state/last_boot_ms"};
    };

    explicit ManagerSettings(QObject* parent = nullptr);

    [[nodiscard]] static bool needs_restart(const QString& key);

    [[nodiscard]] static QVariant default_value(const QString& key);

    [[nodiscard]] QVariant value(const QString& key) const;
    [[nodiscard]] int int_value(const QString& key) const;
    [[nodiscard]] bool bool_value(const QString& key) const;
    [[nodiscard]] QString string_value(const QString& key) const;
    [[nodiscard]] QStringList string_list_value(const QString& key) const;

    [[nodiscard]] QMap<QString, QVariant> snapshot() const;

    void set_value(const QString& key, const QVariant& value);

    [[nodiscard]] QVector<HostShare> host_shares() const;
    void set_host_shares(const QVector<HostShare>& shares);

    [[nodiscard]] static QString encode_host_shares(const QVector<HostShare>& shares);
    [[nodiscard]] static QVector<HostShare> decode_host_shares(const QString& blob);

    [[nodiscard]] QStringList free_drive_letters() const;
    [[nodiscard]] static QStringList free_drive_letters(const QVector<HostShare>& shares);

    [[nodiscard]] QString file_path() const;

Q_SIGNALS:
    void changed(const QString& key);

private:
    [[nodiscard]] static QString encode_records(const QVector<QStringList>& records);
    [[nodiscard]] static QVector<QStringList> decode_records(const QString& blob);

    QSettings store_;
};

}
