#include "lsw/host/manager/guest_app_launch_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/guest_app_launcher.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTimer>

namespace lsw::host::manager {

namespace {

// How long a guest may take to answer before the splash stops
// pretending this is normal. A cold boot is around forty seconds; three
// minutes is not a slow boot, it is something wrong.
constexpr int kStallMs = 180000;

}  // namespace

GuestAppLaunchController::GuestAppLaunchController(services::HostServices& services,
                                                   QObject* parent)
    : QObject(parent), services_(services),
      launcher_(new GuestAppLauncher(services.rpc(), services.manager_settings(), this)) {
    connect(launcher_, &GuestAppLauncher::launched, this,
            &GuestAppLaunchController::application_running);
}

void GuestAppLaunchController::launch(const QString& app_id) {
    const config::VmRecord* vm = services_.vms().primary();
    if (vm == nullptr) {
        LSW_WARN("launch_app({}): no VM defined", app_id.toStdString());
        return;
    }
    // Resolve AppConfig id → guest Windows path. The .desktop entries
    // ship the stable id (sha1-prefix of path) in their ``Exec=``
    // because that is robust to spaces/backslashes and survives the app
    // moving on the guest side. The agent's launch RPC needs the path.
    const auto* cfg = services_.app_configs().find(app_id);
    if (cfg == nullptr || cfg->path.isEmpty()) {
        LSW_WARN("launch_app({}): no matching config (was the manager "
                 "ever run with this app installed in the guest?)",
                 app_id.toStdString());
        return;
    }
    const QString guest_path = cfg->path;
    const QUuid uuid = vm->uuid;

    // Intentionally do NOT show the main window — the user clicked an
    // XDG launcher to open a guest app, not to bring up the manager.
    ensure_channel_target(uuid);

    if (services_.monitor().latest(uuid).agent_reachable) {
        LSW_INFO("launch_app({}): firing immediately path={}",
                 app_id.toStdString(), guest_path.toStdString());
        launcher_->launch(*cfg);
        return;
    }
    start_vm_and_wait(uuid, app_id, guest_path);
}

void GuestAppLaunchController::fire_when_ready(const QString& app_id, const QString& guest_path) {
    // Looked up again rather than carried over from the request: the
    // guest has been booting for a while, and the config may have been
    // edited in the meantime. One removed since launches its path bare.
    if (const auto* cfg = services_.app_configs().find(app_id); cfg != nullptr) {
        launcher_->launch(*cfg);
        return;
    }
    apps::AppConfig bare;
    bare.path = guest_path;
    launcher_->launch(bare);
}

void GuestAppLaunchController::ensure_channel_target(const QUuid& uuid) {
    if (!services_.rpc().has_target()) {
        (void)services_.bind_agent(uuid);
    }
}

void GuestAppLaunchController::start_vm_and_wait(const QUuid& uuid, const QString& app_id,
                                                 const QString& guest_path) {
    pending_app_id_ = app_id;
    auto& lifecycle = services_.lifecycle();
    if (!lifecycle.is_running(uuid)) {
        if (const int rc = services_.start_vm(uuid); rc != 0) {
            LSW_WARN("launch_app({}): lifecycle.start rc={}", app_id.toStdString(), rc);
        }
    }
    services_.monitor().watch(uuid);

    // Parentless top-level so the splash is not tied to the (possibly
    // hidden) main window, and so it gets its own taskbar slot. Centred
    // on whichever screen the cursor is on at the moment of the click.
    QString display_name = app_id;
    QIcon icon;
    if (const auto* cfg = services_.app_configs().find(app_id); cfg != nullptr) {
        if (!cfg->name.isEmpty()) {
            display_name = cfg->name;
        }
        const QImage image = cfg->icon_image();
        if (!image.isNull()) {
            icon = QIcon{QPixmap::fromImage(image)};
        }
    }

    auto* splash = new LaunchSplash(display_name, icon);
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        screen != nullptr || (screen = QGuiApplication::primaryScreen()) != nullptr) {
        splash->adjustSize();
        splash->move(screen->availableGeometry().center() - splash->rect().center());
    }
    QPointer<LaunchSplash> splash_ptr(splash);
    if (lifecycle.is_running(uuid)) {
        splash->reach(LaunchSplash::Phase::MachineStarted);
    }

    // A guest that has not answered in this long is not merely slow.
    // Rather than spin forever, say so and offer the two things that
    // help: the log, and a restart.
    auto* patience = new QTimer(this);
    patience->setSingleShot(true);
    patience->setInterval(kStallMs);
    connect(patience, &QTimer::timeout, this, [splash_ptr] {
        if (splash_ptr) {
            splash_ptr->stall(QStringLiteral(
                "The machine is running but the agent has not answered. Usually a guest "
                "still finishing an update; the log says so if the agent crashed."));
        }
    });
    patience->start();

    // The monitor emits stats_updated periodically. Wait for the first
    // reachable flip (or a cancel), then fire the launch.
    auto* wait = new QObject(this);
    connect(&services_.monitor(), &vm::Monitor::stats_updated, wait,
            [this, wait, uuid, app_id, guest_path, splash_ptr, patience]
            (const QUuid& u, const vm::VmStats& s) {
        if (u != uuid || !s.agent_reachable) return;
        if (pending_app_id_ != app_id) return;  // cancelled / superseded
        pending_app_id_.clear();
        patience->stop();
        if (splash_ptr) splash_ptr->reach(LaunchSplash::Phase::GuestAnswering);
        fire_when_ready(app_id, guest_path);
        // The splash closes itself once the application is actually
        // running, which the launcher reports; until then it stays up
        // saying what it is waiting for.
        if (splash_ptr) {
            connect(this, &GuestAppLaunchController::application_running, splash_ptr,
                    [splash_ptr] {
                        if (splash_ptr) {
                            splash_ptr->reach(LaunchSplash::Phase::ApplicationRunning);
                        }
                    });
        }
        wait->deleteLater();
    });
    connect(splash, &LaunchSplash::cancelled, wait, [this, wait, app_id] {
        if (pending_app_id_ == app_id) pending_app_id_.clear();
        wait->deleteLater();
    });
    connect(splash, &LaunchSplash::restart_requested, this, [this, uuid, splash_ptr] {
        (void)services_.lifecycle().shutdown(uuid);
        (void)services_.start_vm(uuid);
        if (splash_ptr) {
            splash_ptr->reach(LaunchSplash::Phase::MachineStarted);
        }
    });
    connect(splash, &LaunchSplash::log_requested, this,
            &GuestAppLaunchController::log_requested);
    splash->show();
}

}  // namespace lsw::host::manager
