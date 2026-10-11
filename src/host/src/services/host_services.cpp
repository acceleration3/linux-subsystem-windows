#include "lsw/host/services/host_services.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/platform/dolphin_places.hpp"
#include "lsw/host/platform/gtk_bookmarks.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/linkproto/envelope.hpp"

#include <QDir>
#include <QSet>

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;

// What apply_integrations reads. Keep in step with it.
const QSet<QString>& integration_keys() {
    static const QSet<QString> keys = {
        K::kMenuCategory,  K::kPublishEntries, K::kFileAssociations, K::kSmbEnabled,  K::kGuestUser,
        K::kGuestPassword, K::kShareScope,     K::kBookmarkDolphin,  K::kBookmarkGtk,
    };
    return keys;
}

}  // namespace

HostServices::HostServices(QObject* parent)
    : QObject(parent), settings_(this), manager_settings_(this),
      pending_changes_(manager_settings_, this), vms_(this), libvirt_(this),
      lifecycle_(libvirt_, this), rpc_(this), guest_probe_(rpc_),
      status_(libvirt_, lifecycle_, rpc_, guest_probe_, settings_, manager_settings_, this),
      shells_(rpc_, status_, this),
      upkeep_(status_, rpc_, this),
      app_configs_(QDir(settings_.data_dir()).filePath("apps"), this), log_store_(this),
      app_launcher_(rpc_, this) {
    // Wire the two services that consume the agent channel. Done here,
    // not by the UI, so records and stats are collected from the moment
    // the channel connects — the manager window may never be shown at
    // all (``--launch`` opens a guest app directly).
    log_store_.attach(rpc_);

    apply_integrations();
    // Filtered by key: apply_integrations rewrites every desktop entry
    // and both bookmark files, which is a lot of disk to touch because a
    // slider on another page moved.
    connect(&manager_settings_, &config::ManagerSettings::changed, this,
            [this](const QString& key) {
                if (integration_keys().contains(key)) {
                    apply_integrations();
                }
            });
}

void HostServices::apply_integrations() {
    apps::DesktopLauncher::set_category(manager_settings_.string_value(K::kMenuCategory));
    apps::DesktopLauncher::set_publishing(manager_settings_.bool_value(K::kPublishEntries));
    apps::DesktopLauncher::set_file_associations(
        manager_settings_.bool_value(K::kFileAssociations));
    apps::DesktopLauncher::sync(app_configs_.configs());

    // The bookmark is the same URL for both file managers, and both can
    // be on at once: a KDE user and a GNOME user are not exclusive, and
    // somebody with Dolphin and Thunar installed wants it in both.
    // A bookmark pointing at a guest that does not exist is worse than
    // no bookmark: it is a folder in somebody's sidebar that fails when
    // clicked. So the share is only advertised once there is something
    // behind it.
    const bool serving = manager_settings_.bool_value(K::kSmbEnabled) && vms_.primary() != nullptr;
    const QString user = manager_settings_.string_value(K::kGuestUser);
    const QString password = manager_settings_.string_value(K::kGuestPassword);
    const QString share =
        platform::GuestSmbShare::share_for_scope(manager_settings_.string_value(K::kShareScope));

    if (serving && manager_settings_.bool_value(K::kBookmarkDolphin)) {
        (void)platform::DolphinPlaces::register_windows_bookmark(user, password, share);
    } else {
        platform::DolphinPlaces::remove_windows_bookmark();
    }
    if (serving && manager_settings_.bool_value(K::kBookmarkGtk)) {
        (void)platform::GtkBookmarks::register_windows_bookmark(user, password, share);
    } else {
        platform::GtkBookmarks::remove_windows_bookmark();
    }
}

bool HostServices::bind_agent(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    if (record == nullptr) {
        return false;
    }
    rpc_.set_target(record->vsock_cid, linkproto::kAgentTcpPort);
    return true;
}

int HostServices::start_vm(const QUuid& uuid) {
    const auto* record = vms_.find(uuid);
    // A start of a machine already up is a no-op, not a boot to time.
    const bool cold = !lifecycle_.is_running(uuid);
    const int rc = lifecycle_.start(uuid, record != nullptr && record->first_boot_done);
    if (rc == 0 && cold) {
        status_.boot_started(uuid);
    }
    return rc;
}

HostServices::~HostServices() = default;

bool HostServices::open() {
    if (!settings_.ensure_directories()) {
        LSW_ERROR("could not create LSW data directories");
        return false;
    }
    if (libvirt_.open() != 0) {
        LSW_WARN("libvirt unavailable; VM controls disabled");
    }
    return true;
}

}  // namespace lsw::host::services
