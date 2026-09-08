#include "lsw/host/manager/guest_app_launch_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/mirror/window_mirror.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QCursor>
#include <QIcon>
#include <QPixmap>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

#include <QTimer>

#include <algorithm>

namespace lsw::host::manager {

namespace {

// The agent's RPC port inside the guest. Fixed by the agent build.
constexpr quint32 kAgentPort = 5102;

// How long a guest may take to answer before the splash stops
// pretending this is normal. A cold boot is around forty seconds; three
// minutes is not a slow boot, it is something wrong.
constexpr int kStallMs = 180000;

// Derive an exe basename from a guest path (Windows backslashes) for the
// watcher's packaged/alias window matching.
[[nodiscard]] QString exe_basename_of(const QString& guest_path) {
    const qsizetype sep = std::max(guest_path.lastIndexOf(QLatin1Char('\\')),
                                   guest_path.lastIndexOf(QLatin1Char('/')));
    return sep < 0 ? guest_path : guest_path.mid(sep + 1);
}

}  // namespace

GuestAppLaunchController::GuestAppLaunchController(services::HostServices& services,
                                                   QObject* parent)
    : QObject(parent), services_(services) {}

void GuestAppLaunchController::launch(const QString& app_id) {
    auto& vms = services_.vms();
    if (vms.records().isEmpty()) {
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
    const QUuid uuid = vms.records().first().uuid;

    // Intentionally do NOT show the main window — the user clicked an
    // XDG launcher to open a guest app, not to bring up the manager.
    ensure_channel_target(uuid);

    if (services_.monitor().latest(uuid).agent_reachable) {
        LSW_INFO("launch_app({}): firing immediately path={}",
                 app_id.toStdString(), guest_path.toStdString());
        fire_launch(guest_path);
        return;
    }
    start_vm_and_wait(uuid, app_id, guest_path);
}

void GuestAppLaunchController::ensure_channel_target(const QUuid& uuid) {
    auto& rpc = services_.rpc();
    if (rpc.has_target()) {
        return;
    }
    auto* rec = services_.vms().find(uuid);
    if (rec == nullptr) {
        return;
    }
    rpc.attach_shmem(services_.lifecycle().ivshmem_server(uuid),
                     services_.lifecycle().shmem_region(uuid));
    rpc.set_target(rec->vsock_cid, kAgentPort);
}

void GuestAppLaunchController::fire_launch(const QString& guest_path) {
    // The config is looked up by path because that is what this path
    // carries — the .desktop entry launches by id, which was resolved
    // to a path before we got here.
    QString args;
    QString env;
    // And the identity its windows will present to the task manager.
    // Taken from the same config as the arguments, in the same pass:
    // this path was reached from an id that was already resolved to a
    // path, so the config is the only thing that still knows which
    // application this is.
    QString desktop_id;
    for (const auto& cfg : services_.app_configs().configs()) {
        if (cfg.path == guest_path) {
            args = cfg.cmdline;
            env = cfg.env;
            desktop_id = apps::DesktopLauncher::entry_id(cfg.id);
            break;
        }
    }

    auto* launcher = new apps::Launcher(services_.rpc(), this);
    connect(launcher, &apps::Launcher::launched, this,
            [this, launcher, desktop_id](const QString& /*reported_path*/, quint32 pid,
                                         const QString& job) {
        launcher->deleteLater();
        if (pid == 0) {
            LSW_WARN("launch_app: agent returned pid=0 — old agent?");
            return;
        }
        Q_EMIT application_running();
        // Without this the agent dutifully starts the application inside
        // the guest and nothing of it appears here: the mirror is what
        // carries its windows onto this desktop.
        auto* mirror = new mirror::WindowMirror(services_.rpc(), pid, job, desktop_id, this);
        connect(mirror, &mirror::WindowMirror::finished, mirror, &QObject::deleteLater);
        if (!mirror->start()) {
            mirror->deleteLater();
        }
    });
    connect(launcher, &apps::Launcher::failed, launcher,
            [launcher](const QString& id, const QString& err) {
                LSW_WARN("launch_app({}) failed: {}", id.toStdString(), err.toStdString());
                launcher->deleteLater();
            });
    launcher->launch(guest_path, args, env);
}

void GuestAppLaunchController::start_vm_and_wait(const QUuid& uuid, const QString& app_id,
                                                 const QString& guest_path) {
    pending_app_id_ = app_id;
    auto& lifecycle = services_.lifecycle();
    if (!lifecycle.is_running(uuid)) {
        const auto* record = services_.vms().find(uuid);
        if (const int rc =
                lifecycle.start(uuid, record != nullptr && record->first_boot_done);
            rc != 0) {
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
        fire_launch(guest_path);
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
        const auto* record = services_.vms().find(uuid);
        (void)services_.lifecycle().start(uuid,
                                          record != nullptr && record->first_boot_done);
        if (splash_ptr) {
            splash_ptr->reach(LaunchSplash::Phase::MachineStarted);
        }
    });
    connect(splash, &LaunchSplash::log_requested, this,
            &GuestAppLaunchController::log_requested);
    splash->show();
}

}  // namespace lsw::host::manager
