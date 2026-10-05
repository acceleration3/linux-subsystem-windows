#include "lsw/host/manager/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/mirror/window_mirror.hpp"


namespace lsw::host::manager {

GuestAppLauncher::GuestAppLauncher(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void GuestAppLauncher::launch(const apps::AppConfig& app) {
    // The identity this launch's windows present to the task manager:
    // the launcher entry already written for this application, so they
    // group under its own name and icon, and against the launcher the
    // user may have pinned, rather than under the manager.
    const QString desktop_id =
        app.id.isEmpty() ? QString{} : apps::DesktopLauncher::entry_id(app.id);

    // One Launcher per launch, gone once the agent has answered, so each
    // answer is matched to the identity worked out for it here.
    auto* launcher = new apps::Launcher(rpc_, this);
    connect(launcher, &apps::Launcher::launched, this,
            [this, launcher, desktop_id](const QString& path, quint32 pid, const QString& job) {
                launcher->deleteLater();
                LSW_INFO("launch {}: running as pid {}", path.toStdString(), pid);
                // Without this the agent dutifully starts the application
                // inside the guest and nothing of it appears here: the
                // mirror is what carries its windows onto this desktop.
                auto* mirror =
                    mirror::WindowMirror::of_application(rpc_, pid, job, desktop_id, this);
                connect(mirror, &mirror::WindowMirror::finished, mirror, &QObject::deleteLater);
                if (!mirror->start()) {
                    mirror->deleteLater();
                }
                Q_EMIT launched(path, pid);
            });
    connect(launcher, &apps::Launcher::failed, this,
            [this, launcher](const QString& path, const QString& error) {
                launcher->deleteLater();
                LSW_WARN("launch {} failed: {}", path.toStdString(), error.toStdString());
                Q_EMIT failed(path, error);
            });
    LSW_INFO("launch {}", app.path.toStdString());
    launcher->launch(app.path, app.cmdline, app.env);
}

}  // namespace lsw::host::manager
