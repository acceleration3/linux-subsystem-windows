#pragma once

#include <QLatin1StringView>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVector>

namespace lsw::host::config {

// One host folder the guest can see, as a virtiofs device and a drive
// letter. There is no reason for there to be only one, so there isn't.
struct HostShare {
    QString host_path;
    QString drive;  // "Z:"
    bool read_only = false;

    [[nodiscard]] bool operator==(const HostShare& other) const noexcept {
        return host_path == other.host_path && drive == other.drive && read_only == other.read_only;
    }
};

// Every setting the manager owns, typed, with its default, and marked
// with whether changing it takes effect now or at the next boot.
//
// The split is the whole point of the settings page. A live key is
// written the moment it changes; a restart key is staged, shown in a
// diff at the foot of the page, and only reaches disk when the user
// asks for the restart. Nothing here decides to reboot a guest on its
// own.
class ManagerSettings : public QObject {
    Q_OBJECT
public:
    // A key is named once, here, and referred to by these constants
    // everywhere else, so a typo is a compile error rather than a
    // setting that silently reads its default forever. Views rather
    // than char pointers so they pass straight to the QString API;
    // wrapping one in QLatin1String() still compiles, and is no longer
    // needed.
    struct Key {
        // Machine (restart)
        static constexpr QLatin1StringView kCpuCores{"machine/cpu_cores"};
        static constexpr QLatin1StringView kMemoryMib{"machine/memory_mib"};
        static constexpr QLatin1StringView kDiskGib{"machine/disk_gib"};
        // Display (live). The resolutions reach a running guest as the
        // GPU device's ``modes`` over QMP (Lifecycle::set_display_modes),
        // so none of these needs a restart; the domain definition carries
        // the same value for the next boot.
        //
        // The sizes the guest's monitor offers: a QStringList of "WxH",
        // the first preferred (see DisplayModes). Empty, the default, is
        // the host monitors' own modes.
        static constexpr QLatin1StringView kDisplayModes{"display/modes"};
        // Put the size of the monitor the manager is on first, preferred.
        static constexpr QLatin1StringView kMatchHostOutput{"display/match_host_output"};
        static constexpr QLatin1StringView kScalingPercent{"display/scaling_percent"};
        // Graphics (restart): the accel-virt GPU device's own properties,
        // which QEMU reads once, when the device is realized.
        //
        // Which host GPU the guest's display adapter runs on: "auto"
        // (whatever the device's host-gpu=auto takes, the first Vulkan
        // device) or a Vulkan device UUID, which survives reboots and
        // driver updates where enumeration order does not. See HostGpus.
        static constexpr QLatin1StringView kHostGpu{"graphics/host_gpu"};
        // The device's host window (``hostmem=``), in MiB: the BAR of
        // host memory uploads, readbacks and host-visible Vulkan memory
        // live in.
        static constexpr QLatin1StringView kHostWindowMib{"graphics/host_window_mib"};
        // The host GPUs passed as render-only adapters (display=off),
        // one device each: a QStringList of Vulkan device UUIDs, empty by
        // default. One that is also the display GPU, or that is no longer
        // on the host, is left out when the domain is defined.
        static constexpr QLatin1StringView kRenderGpus{"graphics/render_gpus"};
        // Files (live, except the shares)
        static constexpr QLatin1StringView kSmbEnabled{"files/smb_enabled"};
        static constexpr QLatin1StringView kShareScope{"files/share_scope"};
        static constexpr QLatin1StringView kBookmarkDolphin{"files/bookmark_dolphin"};
        static constexpr QLatin1StringView kBookmarkGtk{"files/bookmark_gtk"};
        static constexpr QLatin1StringView kHostShares{"files/host_shares"};  // restart
        // The account the bookmarks authenticate as. Written once, by
        // the pipeline, so the manager can rewrite a bookmark long
        // after the install. The password is stored in clear, which is
        // the same thing user-places.xbel already does with it and the
        // same thing the answer file did before that; it protects a
        // loopback share on a machine the user already owns.
        static constexpr QLatin1StringView kGuestUser{"files/guest_user"};
        static constexpr QLatin1StringView kGuestPassword{"files/guest_password"};
        // Desktop entries (live)
        static constexpr QLatin1StringView kPublishEntries{"desktop/publish"};
        static constexpr QLatin1StringView kMenuCategory{"desktop/category"};
        static constexpr QLatin1StringView kFileAssociations{"desktop/mime"};
        // Guest
        static constexpr QLatin1StringView kAutostart{"guest/autostart"};   // live
        // Host (live)
        static constexpr QLatin1StringView kCloseToTray{"host/close_to_tray"};
        // Not a setting: how long the last boot took, from the start to
        // the agent answering, in milliseconds (0: none measured yet).
        // StatusHub writes it; the launch splash quotes it.
        static constexpr QLatin1StringView kLastBootMs{"state/last_boot_ms"};
    };

    explicit ManagerSettings(QObject* parent = nullptr);

    // True when changing this key only takes effect after the guest is
    // restarted. Everything not listed applies immediately.
    [[nodiscard]] static bool needs_restart(const QString& key);

    // The default for a key, or an invalid QVariant if the key is not
    // one of ours.
    [[nodiscard]] static QVariant default_value(const QString& key);

    // Reads the stored value, falling back to the default.
    [[nodiscard]] QVariant value(const QString& key) const;
    [[nodiscard]] int int_value(const QString& key) const;
    [[nodiscard]] bool bool_value(const QString& key) const;
    [[nodiscard]] QString string_value(const QString& key) const;
    [[nodiscard]] QStringList string_list_value(const QString& key) const;

    // Writes immediately. Use this for live keys, and for restart keys
    // only once the user has agreed to the restart.
    void set_value(const QString& key, const QVariant& value);

    // Typed helpers for the one setting that is a list rather than a
    // scalar. It round-trips through the string form QSettings can
    // store, so nothing else has to know how it is encoded.
    [[nodiscard]] QVector<HostShare> host_shares() const;
    void set_host_shares(const QVector<HostShare>& shares);

    // The string form ``kHostShares`` is stored in. Public for the
    // settings page, which stages an edited list as that string before
    // anything is written, and for the apply bar, which counts it.
    [[nodiscard]] static QString encode_host_shares(const QVector<HostShare>& shares);
    [[nodiscard]] static QVector<HostShare> decode_host_shares(const QString& blob);

    // Drive letters not already taken by a share, in the order a
    // person would pick them.
    [[nodiscard]] QStringList free_drive_letters() const;

    // The file the settings are stored in.
    [[nodiscard]] QString file_path() const;

Q_SIGNALS:
    void changed(const QString& key);

private:
    // The list setting is records of fields, joined with two ASCII
    // control characters no path contains: 0x1F between records and
    // 0x1E between fields — the reverse of their names, and kept that
    // way for what is already stored.
    [[nodiscard]] static QString encode_records(const QVector<QStringList>& records);
    [[nodiscard]] static QVector<QStringList> decode_records(const QString& blob);

    QSettings store_;
};

}  // namespace lsw::host::config
