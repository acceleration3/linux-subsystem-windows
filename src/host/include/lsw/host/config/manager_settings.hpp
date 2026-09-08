#pragma once

#include <cstdint>

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
        return host_path == other.host_path && drive == other.drive
               && read_only == other.read_only;
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
    // setting that silently reads its default forever.
    struct Key {
        // Machine (restart)
        static constexpr const char* kCpuCores = "machine/cpu_cores";
        static constexpr const char* kMemoryMib = "machine/memory_mib";
        static constexpr const char* kDiskGib = "machine/disk_gib";
        // Display
        static constexpr const char* kGuestWidth = "display/width";
        static constexpr const char* kGuestHeight = "display/height";
        static constexpr const char* kMatchHostOutput = "display/match_host_output";
        static constexpr const char* kScalingPercent = "display/scaling_percent";
        static constexpr const char* kRefreshHz = "display/refresh_hz";      // live
        static constexpr const char* kVramMib = "display/vram_mib";
        static constexpr const char* kLivePreview = "display/live_preview";  // live
        // Render channels (restart, except the warning toggle)
        static constexpr const char* kChannels = "render/channels";
        // Which GPU the render server draws on: an index into the
        // devices EGL enumerates, or -1 for whichever drives the
        // display. See RenderServer::gl_devices.
        static constexpr const char* kGlDevice = "render/gl_device";
        static constexpr const char* kRingKib = "render/ring_kib";
        static constexpr const char* kReplyKib = "render/reply_kib";
        static constexpr const char* kBlobMib = "render/blob_mib";
        static constexpr const char* kWarnCongestion = "render/warn_congestion";  // live
        static constexpr const char* kChannelBindings = "render/bindings";        // live
        static constexpr const char* kNothingFreePolicy = "render/nothing_free";  // live
        static constexpr const char* kChannelWaitSeconds = "render/wait_seconds"; // live
        // Files (live, except the shares)
        static constexpr const char* kSmbEnabled = "files/smb_enabled";
        static constexpr const char* kShareScope = "files/share_scope";
        static constexpr const char* kBookmarkDolphin = "files/bookmark_dolphin";
        static constexpr const char* kBookmarkGtk = "files/bookmark_gtk";
        static constexpr const char* kHostShares = "files/host_shares";  // restart
        // The account the bookmarks authenticate as. Written once, by
        // the pipeline, so the manager can rewrite a bookmark long
        // after the install. The password is stored in clear, which is
        // the same thing user-places.xbel already does with it and the
        // same thing the answer file did before that; it protects a
        // loopback share on a machine the user already owns.
        static constexpr const char* kGuestUser = "files/guest_user";
        static constexpr const char* kGuestPassword = "files/guest_password";
        // Desktop entries (live)
        static constexpr const char* kPublishEntries = "desktop/publish";
        static constexpr const char* kMenuCategory = "desktop/category";
        static constexpr const char* kFileAssociations = "desktop/mime";
        // Guest
        static constexpr const char* kAgentPort = "guest/agent_port";  // restart
        static constexpr const char* kAutostart = "guest/autostart";   // live
        // Host (live)
        static constexpr const char* kCloseToTray = "host/close_to_tray";
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

    // Drive letters not already taken by a share, in the order a
    // person would pick them.
    [[nodiscard]] QStringList free_drive_letters() const;

    [[nodiscard]] static QString policy_to_string(ChannelBusyPolicy policy);
    [[nodiscard]] static ChannelBusyPolicy policy_from_string(const QString& text);
    [[nodiscard]] static QString policy_label(ChannelBusyPolicy policy);

Q_SIGNALS:
    void changed(const QString& key);

private:
    QSettings store_;
};

}  // namespace lsw::host::config
