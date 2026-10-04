#pragma once

#include <QLatin1StringView>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QVector>
#include <cstdint>

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

// Which channel an application draws on, and what to do when that
// channel is already claimed.
//
// A channel carries one command stream, so two processes cannot share
// one: the ring is single-producer by construction and a second writer
// interleaves its commands into nonsense. That leaves exactly three
// honest answers, and all three are here.
enum class ChannelBusyPolicy : std::uint8_t {
    TakeAnyFree,  // what happens today for everything
    Wait,         // block until the pinned channel is released
    Refuse,       // fail the launch and say why
};

struct ChannelBinding {
    QString app_id;
    int channel = -1;  // -1 means "any", which is the default for everything
    ChannelBusyPolicy busy = ChannelBusyPolicy::TakeAnyFree;
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
        // Display
        static constexpr QLatin1StringView kGuestWidth{"display/width"};
        static constexpr QLatin1StringView kGuestHeight{"display/height"};
        static constexpr QLatin1StringView kMatchHostOutput{"display/match_host_output"};
        static constexpr QLatin1StringView kScalingPercent{"display/scaling_percent"};
        static constexpr QLatin1StringView kRefreshHz{"display/refresh_hz"};  // live
        static constexpr QLatin1StringView kVramMib{"display/vram_mib"};
        static constexpr QLatin1StringView kLivePreview{"display/live_preview"};  // live
        // Render channels (restart, except the warning toggle)
        static constexpr QLatin1StringView kChannels{"render/channels"};
        // Which GPU the render server draws on: an index into the
        // devices EGL enumerates, or -1 for whichever drives the
        // display. See RenderServer::gl_devices.
        static constexpr QLatin1StringView kGlDevice{"render/gl_device"};
        static constexpr QLatin1StringView kRingKib{"render/ring_kib"};
        static constexpr QLatin1StringView kReplyKib{"render/reply_kib"};
        static constexpr QLatin1StringView kBlobMib{"render/blob_mib"};
        static constexpr QLatin1StringView kWarnCongestion{"render/warn_congestion"};   // live
        static constexpr QLatin1StringView kChannelBindings{"render/bindings"};         // live
        static constexpr QLatin1StringView kNothingFreePolicy{"render/nothing_free"};   // live
        static constexpr QLatin1StringView kChannelWaitSeconds{"render/wait_seconds"};  // live
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
        static constexpr QLatin1StringView kAgentPort{"guest/agent_port"};  // restart
        static constexpr QLatin1StringView kAutostart{"guest/autostart"};   // live
        // Host (live)
        static constexpr QLatin1StringView kCloseToTray{"host/close_to_tray"};
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

    // Writes immediately. Use this for live keys, and for restart keys
    // only once the user has agreed to the restart.
    void set_value(const QString& key, const QVariant& value);

    // Typed helpers for the two settings that are lists rather than
    // scalars. Both round-trip through the string form QSettings can
    // store, so nothing else has to know how they are encoded.
    [[nodiscard]] QVector<HostShare> host_shares() const;
    void set_host_shares(const QVector<HostShare>& shares);

    [[nodiscard]] QVector<ChannelBinding> channel_bindings() const;
    void set_channel_bindings(const QVector<ChannelBinding>& bindings);

    // The string form ``kHostShares`` is stored in. Public for the
    // settings page, which stages an edited list as that string before
    // anything is written, and for the apply bar, which counts it.
    [[nodiscard]] static QString encode_host_shares(const QVector<HostShare>& shares);
    [[nodiscard]] static QVector<HostShare> decode_host_shares(const QString& blob);

    // Drive letters not already taken by a share, in the order a
    // person would pick them.
    [[nodiscard]] QStringList free_drive_letters() const;

    [[nodiscard]] static QString policy_to_string(ChannelBusyPolicy policy);
    [[nodiscard]] static ChannelBusyPolicy policy_from_string(const QString& text);
    [[nodiscard]] static QString policy_label(ChannelBusyPolicy policy);

Q_SIGNALS:
    void changed(const QString& key);

private:
    // Both list settings are records of fields, joined with two ASCII
    // control characters no path or app id contains: 0x1F between
    // records and 0x1E between fields — the reverse of their names, and
    // kept that way for what is already stored. Written once so the two
    // settings cannot disagree about which is which.
    [[nodiscard]] static QString encode_records(const QVector<QStringList>& records);
    [[nodiscard]] static QVector<QStringList> decode_records(const QString& blob);

    QSettings store_;
};

}  // namespace lsw::host::config
