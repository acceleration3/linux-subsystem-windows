#include "lsw/host/manager/guest_app_launch_controller.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config_store.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/link/guest_window_model.hpp"
#include "lsw/host/manager/app_window_watcher.hpp"
#include "lsw/host/services/host_services.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace lsw::host::manager {

namespace {

// The agent's RPC port inside the guest. Fixed by the agent build.
constexpr quint32 kAgentPort = 5102;

// How long to keep looking for the window of a Vulkan-capture launch
// before concluding it is not coming. Generous: a cold app on a cold
// guest can take a while to put anything up.
constexpr int kVulkanBindTimeoutMs = 60000;

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
    // Same two prerequisites as the in-manager list: a replay server on
    // this side, and an agent told to let go of the region. The config
    // is looked up by path because that is what this path carries.
    bool vulkan = false;
    for (const auto& cfg : services_.app_configs().configs()) {
        if (cfg.path == guest_path) {
            vulkan = cfg.capture == apps::CaptureMethod::Vulkan;
            break;
        }
    }

    auto* launcher = new apps::Launcher(services_.rpc(), this);
    connect(launcher, &apps::Launcher::launched, this,
            [this, launcher, vulkan](const QString& reported_path, quint32 pid) {
        launcher->deleteLater();
        if (pid == 0) {
            LSW_WARN("launch_app: agent returned pid=0 — old agent?");
            return;
        }
        if (vulkan) {
            bind_vulkan_input(pid);
            return;
        }
        // Without the watcher the agent dutifully CreateProcessW-es the
        // app inside the guest but no host-side mirror appears: the
        // watcher is what turns up WindowViews as the PID's windows show.
        auto* w = new AppWindowWatcher(services_.rpc(), services_.windows(), pid,
                                       exe_basename_of(reported_path), this);
        // Self-destructs when its idle timer expires (no live mirrors AND
        // no matching guest windows).
        connect(w, &AppWindowWatcher::exhausted, w, &QObject::deleteLater);
        w->start();
    });
    connect(launcher, &apps::Launcher::failed, launcher,
            [launcher](const QString& id, const QString& err) {
                LSW_WARN("launch_app({}) failed: {}", id.toStdString(), err.toStdString());
                launcher->deleteLater();
            });
    if (vulkan) {
        // Same single-VM resolution this controller already uses to
        // pick a channel target.
        const auto& records = services_.vms().records();
        if (!records.isEmpty()) {
            (void)services_.vulkan_capture().ensure_running(records.first().uuid);
        }
        services_.vulkan_capture().stop_forwarding();
    }
    launcher->launch(guest_path, vulkan);
}

void GuestAppLaunchController::bind_vulkan_input(quint32 pid) {
    // No AppWindowWatcher for a Vulkan-capture launch. Its windows would
    // each become a mirror fed by the frame ring, which this application
    // never writes to — a black window next to the replay's window that
    // has the actual picture. What the replay needs instead is the HWND
    // to send its window's input to, and that takes waiting for the
    // PID's window to turn up in the enumeration.
    auto* binder = new QObject(this);
    // The subscription is released exactly once, by whichever of the two
    // paths below gets there first: ``deleteLater`` only takes effect on
    // the next loop turn, so the timer can still fire after a successful
    // bind has asked for the binder's death.
    auto done = std::make_shared<bool>(false);
    auto release = [this, binder, done] {
        if (*done) {
            return;
        }
        *done = true;
        services_.windows().unsubscribe();
        binder->deleteLater();
    };

    connect(&services_.windows(), &link::GuestWindowModel::updated, binder,
            [this, pid, release](const QList<link::GuestWindow>& wins) {
        for (const auto& w : wins) {
            if (w.pid != pid || w.hwnd == 0) {
                continue;
            }
            services_.vulkan_capture().forward_input_to(w.hwnd);
            release();
            return;
        }
    });
    services_.windows().subscribe();

    // The window may never come (the app died, or it draws without one),
    // and an unmatched subscription would keep the enumeration polling
    // for the rest of the session. Give up after a while.
    QTimer::singleShot(kVulkanBindTimeoutMs, binder, [release] {
        LSW_WARN("launch_app: no guest window for the Vulkan-capture app; "
                 "its replay window will have no input");
        release();
    });
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
    auto* splash = new QMessageBox(nullptr);
    splash->setWindowTitle(QStringLiteral("LSW"));
    splash->setIcon(QMessageBox::Information);
    splash->setText(QStringLiteral("Starting Windows…"));
    splash->setInformativeText(
        QStringLiteral("Waiting for the LSW VM to become ready before launching\n"
                       "%1.").arg(app_id));
    splash->setStandardButtons(QMessageBox::Cancel);
    splash->setModal(false);
    splash->setAttribute(Qt::WA_DeleteOnClose);
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        screen != nullptr || (screen = QGuiApplication::primaryScreen()) != nullptr) {
        // QMessageBox sizes itself when first shown; adjustSize() forces
        // the recompute pre-show so the first frame lands centred.
        splash->adjustSize();
        splash->move(screen->availableGeometry().center() - splash->rect().center());
    }
    QPointer<QMessageBox> splash_ptr(splash);

    // The monitor emits stats_updated periodically. Wait for the first
    // reachable flip (or splash Cancel), then fire the launch.
    auto* wait = new QObject(this);
    connect(&services_.monitor(), &vm::Monitor::stats_updated, wait,
            [this, wait, uuid, app_id, guest_path, splash_ptr]
            (const QUuid& u, const vm::VmStats& s) {
        if (u != uuid || !s.agent_reachable) return;
        if (pending_app_id_ != app_id) return;  // cancelled / superseded
        pending_app_id_.clear();
        if (splash_ptr) splash_ptr->close();
        fire_launch(guest_path);
        wait->deleteLater();
    });
    connect(splash, &QMessageBox::rejected, wait, [this, wait, app_id] {
        if (pending_app_id_ == app_id) pending_app_id_.clear();
        wait->deleteLater();
    });
    splash->show();
}

}  // namespace lsw::host::manager
