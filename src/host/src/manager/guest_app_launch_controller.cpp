#include "lsw/host/manager/guest_app_launch_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/guest_app_launcher.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/manager/launch_splash.hpp"
#include "lsw/host/services/host_services.hpp"
#include "lsw/host/vm/graceful_shutdown.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QTimer>

namespace lsw::host::manager {

namespace {

// How long a guest may take to answer before the splash stops
// pretending this is normal: three minutes, or three times the last
// measured boot if that is longer. A slow boot is not a stuck one.
constexpr int kStallMs = 180000;

int stall_ms(std::optional<qint64> last_boot_ms) {
    return static_cast<int>(qMax<qint64>(kStallMs, 3 * last_boot_ms.value_or(0)));
}

}  // namespace

GuestAppLaunchController::GuestAppLaunchController(services::HostServices& services,
                                                   QObject* parent)
    : QObject(parent), services_(services), launcher_(&services.app_launcher()) {
    connect(launcher_, &apps::GuestAppLauncher::launched, this,
            &GuestAppLaunchController::application_running);
    connect(launcher_, &apps::GuestAppLauncher::launched, this,
            [this](const QString& path, quint32) {
                requested_paths_.remove(path);
                if (splash_ && splash_path_ == path)
                    splash_->reach(LaunchSplash::Phase::ApplicationRunning);
            });
    connect(launcher_, &apps::GuestAppLauncher::failed, this,
            [this](const QString& path, const QString& error) {
                if (!requested_paths_.remove(path))
                    return;
                if (splash_ && splash_path_ == path) {
                    splash_->fail(error);
                    return;
                }
                auto* notice = new QMessageBox(QMessageBox::Warning,
                                               QStringLiteral("Could not open application"), error,
                                               QMessageBox::Close);
                notice->setTextFormat(Qt::PlainText);
                notice->setAttribute(Qt::WA_DeleteOnClose);
                notice->open();
            });
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
    requested_paths_.insert(guest_path);
    const QUuid uuid = vm->uuid;

    // Intentionally do NOT show the main window — the user clicked an
    // XDG launcher to open a guest app, not to bring up the manager.
    ensure_channel_target(uuid);

    // The hub watches one VM; with no manager window open nobody has
    // pointed it at this one yet.
    services_.status().set_target(uuid);
    if (services_.status().vm().agent_online()) {
        LSW_INFO("launch_app({}): firing immediately path={}", app_id.toStdString(),
                 guest_path.toStdString());
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
    if (splash_)
        splash_->close();
    pending_app_id_ = app_id;
    auto& lifecycle = services_.lifecycle();
    int start_error = 0;
    if (!lifecycle.is_running(uuid)) {
        if (const int rc = services_.start_vm(uuid); rc != 0) {
            start_error = rc;
            LSW_WARN("launch_app({}): lifecycle.start rc={}", app_id.toStdString(), rc);
        }
    }

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

    auto* splash = new LaunchSplash(display_name, icon, services_.status().last_boot_ms());
    splash_ = splash;
    splash_path_ = guest_path;
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
    auto* patience = new QTimer(splash);
    patience->setSingleShot(true);
    patience->setInterval(stall_ms(services_.status().last_boot_ms()));
    connect(patience, &QTimer::timeout, this, [splash_ptr] {
        if (splash_ptr) {
            splash_ptr->stall(QStringLiteral(
                "The machine is running but the agent has not answered. Usually a guest "
                "still finishing an update; the log says so if the agent crashed."));
        }
    });
    patience->start();

    // The status hub reads the machine every couple of seconds. Wait
    // for the agent to answer (or a cancel), then fire the launch.
    auto* wait = new QObject(splash);
    connect(&services_.status(), &services::StatusHub::changed, wait,
            [this, wait, uuid, app_id, guest_path, splash_ptr,
             patience](services::StatusHub::Area area) {
                const auto& vm = services_.status().vm();
                if (area != services::StatusHub::Area::Vm || vm.uuid != uuid)
                    return;
                if (pending_app_id_ != app_id)
                    return;  // cancelled / superseded
                // The phases follow the hub's readings: QEMU up, then the agent.
                if (vm.running() && splash_ptr)
                    splash_ptr->reach(LaunchSplash::Phase::MachineStarted);
                if (!vm.agent_online())
                    return;
                pending_app_id_.clear();
                patience->stop();
                if (splash_ptr)
                    splash_ptr->reach(LaunchSplash::Phase::GuestAnswering);
                fire_when_ready(app_id, guest_path);
                wait->deleteLater();
            });
    connect(splash, &LaunchSplash::cancelled, wait, [this, wait, app_id, guest_path, patience] {
        if (pending_app_id_ == app_id)
            pending_app_id_.clear();
        requested_paths_.remove(guest_path);
        patience->stop();
        wait->deleteLater();
    });
    connect(splash, &LaunchSplash::retry_requested, splash, [this, app_id, guest_path] {
        requested_paths_.insert(guest_path);
        if (services_.status().vm().agent_online())
            fire_when_ready(app_id, guest_path);
        else
            launch(app_id);
    });
    connect(splash, &LaunchSplash::restart_requested, splash, [this, uuid, splash_ptr, patience] {
        // A start while the old QEMU still runs does nothing, so the start
        // waits for the stop, as the manager's own restart does.
        if (splash_ptr) {
            splash_ptr->resume();
        }
        patience->start();
        auto* restart = new vm::GracefulShutdown(services_.lifecycle(), uuid,
                                                 vm::GracefulShutdown::kDefaultDeadlineMs, this);
        connect(restart, &vm::GracefulShutdown::finished, this, [this, restart, uuid] {
            restart->deleteLater();
            (void)services_.start_vm(uuid);
        });
        restart->start();
    });
    connect(splash, &LaunchSplash::log_requested, this, &GuestAppLaunchController::log_requested);
    splash->show();
    if (start_error != 0) {
        patience->stop();
        wait->deleteLater();
        pending_app_id_.clear();
        splash->fail(QStringLiteral("Windows could not start (error %1). Open the log for details, "
                                    "then try again.")
                         .arg(start_error));
    }
}

}  // namespace lsw::host::manager
