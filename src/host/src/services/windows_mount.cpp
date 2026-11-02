#include "lsw/host/services/windows_mount.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/platform/guest_smb_share.hpp"
#include "lsw/host/services/mount_status.hpp"
#include "lsw/host/services/status_hub.hpp"

namespace lsw::host::services {

namespace {

using K = config::ManagerSettings::Key;
using State = MountStatus::State;

const QStringList& watched_keys() {
    static const QStringList keys = {K::kSmbEnabled, K::kShareScope, K::kGuestUser,
                                     K::kGuestPassword};
    return keys;
}

}  // namespace

WindowsMount::WindowsMount(StatusHub& status, config::ManagerSettings& settings,
                           platform::WindowsFsLauncher& helper, QString mount_point,
                           TableReader table, MountedHook mounted, QObject* parent)
    : QObject(parent), status_(status), settings_(settings), helper_(helper),
      mount_point_(std::move(mount_point)),
      table_(table ? std::move(table) : TableReader{[] { return platform::MountTable::read(); }}),
      mounted_(std::move(mounted)) {
    connect(&status_, &StatusHub::changed, this, [this](StatusHub::Area area) {
        if (area != StatusHub::Area::Vm) {
            return;
        }
        const bool running = status_.vm().running();
        const bool online = status_.vm().agent_online();
        if (running != was_running_ || online != was_online_) {
            was_running_ = running;
            was_online_ = online;
            blocked_ = false;
        }
        reconcile();
    });
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (watched_keys().contains(key)) {
            unblock();
        }
    });
}

void WindowsMount::start(bool guest_running) {
    started_ = true;
    start_running_ = guest_running;
    reconcile();
}

void WindowsMount::unblock() {
    blocked_ = false;
    reconcile();
}

void WindowsMount::publish(MountStatus::State state, const QString& path, const QString& detail) {
    MountStatus next;
    next.state = state;
    next.path = path;
    next.detail = detail;
    if (next == status_.mount()) {
        return;
    }
    status_.set_mount(next);
}

void WindowsMount::reconcile() {
    if (!started_ || closing_ || busy_) {
        return;
    }
    const bool known = !status_.vm().uuid.isNull();
    if (!known && start_running_) {
        return;
    }
    const bool enabled = settings_.bool_value(K::kSmbEnabled);
    const bool kept = enabled && known && status_.vm().running();
    const bool wanted = kept && status_.vm().agent_online();
    const QString share =
        platform::GuestSmbShare::share_for_scope(settings_.string_value(K::kShareScope));
    const QString& point = mount_point_;
    const QString source = platform::GuestSmbShare::mount_source(share);

    const platform::MountTable table = table_();
    const auto* entry = table.find(point);
    const bool guest_fs = entry != nullptr && entry->fstype == platform::MountTable::kFuseType &&
                          platform::GuestSmbShare::is_mount_source(entry->source);
    const bool present = guest_fs && helper_.owns_mount() && kept && entry->source == source;
    if (guest_fs && !present) {
        if (blocked_) {
            return;
        }
        if (const QString why = helper_.unavailable(); !why.isEmpty()) {
            publish(State::Unavailable, point, why);
            return;
        }
        unmount();
        return;
    }
    if (present) {
        publish(State::Mounted, point);
        return;
    }
    if (!wanted) {
        publish(!enabled ? State::Off : (kept ? State::Waiting : State::NoGuest), point);
        return;
    }
    if (blocked_) {
        return;
    }
    if (const QString why = helper_.unavailable(); !why.isEmpty()) {
        publish(State::Unavailable, point, why);
        return;
    }
    mount(share);
}

void WindowsMount::fail(const QString& detail) {
    blocked_ = true;
    LSW_WARN("the Windows files: {}", detail.toStdString());
    publish(State::Failed, mount_point_, detail);
}

void WindowsMount::unmount() {
    busy_ = true;
    publish(State::Unmounting, mount_point_);
    helper_.unmount([this](const platform::WindowsFsLauncher::Result& result) {
        busy_ = false;
        if (!result.ok) {
            fail(result.error);
            return;
        }
        LSW_INFO("unmounted {}", mount_point_.toStdString());
        reconcile();
    });
}

void WindowsMount::mount(const QString& share) {
    busy_ = true;
    publish(State::Mounting, mount_point_);
    helper_.mount(platform::GuestSmbShare::endpoint(share), settings_.string_value(K::kGuestUser),
                  settings_.string_value(K::kGuestPassword),
                  [this, share](const platform::WindowsFsLauncher::Result& result) {
                      busy_ = false;
                      if (!result.ok) {
                          fail(result.error);
                          return;
                      }
                      settle(share);
                  });
}

void WindowsMount::settle(const QString& share) {
    const platform::MountTable table = table_();
    const auto* entry = table.find(mount_point_);
    if (entry == nullptr || entry->source != platform::GuestSmbShare::mount_source(share)) {
        fail(QStringLiteral("the mount did not appear at %1").arg(mount_point_));
        return;
    }
    LSW_INFO("mounted {} at {}", share.toStdString(), mount_point_.toStdString());
    if (mounted_) {
        mounted_(mount_point_);
    }
    reconcile();
}

void WindowsMount::unmount_for_shutdown() {
    closing_ = true;
    const platform::MountTable table = table_();
    const auto* entry = table.find(mount_point_);
    if (entry != nullptr && entry->fstype == platform::MountTable::kFuseType &&
        platform::GuestSmbShare::is_mount_source(entry->source) &&
        helper_.unavailable().isEmpty()) {
        const auto result = helper_.unmount_now();
        if (!result.ok) {
            LSW_WARN("could not unmount {} before shutdown: {}", mount_point_.toStdString(),
                     result.error.toStdString());
        }
    }
    publish(State::NoGuest, mount_point_);
}

}  // namespace lsw::host::services
