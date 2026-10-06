#include "lsw/host/apps/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

namespace lsw::host::apps {

GuestAppLauncher::GuestAppLauncher(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void GuestAppLauncher::launch(const apps::AppConfig& app) {
    const QString identity = key(app);
    if (pending_.contains(identity))
        return;
    const auto existing = mirrors_.value(identity);
    if (existing && (!app.cmdline.trimmed().isEmpty() || !app.env.trimmed().isEmpty())) {
        existing->raise();
        Q_EMIT launched(app.target(), pids_.value(identity));
        return;
    }
    pending_.insert(identity);
    Q_EMIT changed();
    // The identity this launch's windows present to the task manager:
    // the launcher entry already written for this application, so they
    // group under its own name and icon, and against the launcher the
    // user may have pinned, rather than under the manager.
    const QString desktop_id =
        app.id.isEmpty() ? QString{} : apps::DesktopLauncher::entry_id(app.id);

    // One Launcher per launch, gone once the agent has answered, so each
    // answer is matched to the identity worked out for it here.
    auto* launcher = new apps::Launcher(rpc_, this);
    connect(
        launcher, &apps::Launcher::launched, this,
        [this, launcher, desktop_id, identity, existing](const QString& path, quint32 pid,
                                                         const QString& job) {
            launcher->deleteLater();
            LSW_INFO("launch {}: opened, pid {}", path.toStdString(), pid);
            // Reopening a tray app must reach Windows too, so its normal
            // activation handoff runs. It still uses the same host mirror.
            if (existing && mirrors_.value(identity) == existing && pids_.value(identity) == pid) {
                pending_.remove(identity);
                existing->raise();
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
                return;
            }
            if (existing)
                existing->deleteLater();
            // Without this the agent dutifully starts the application
            // inside the guest and nothing of it appears here: the
            // mirror is what carries its windows onto this desktop.
            auto* mirror = mirror::WindowMirror::of_application(rpc_, pid, job, desktop_id, this);
            mirrors_.insert(identity, mirror);
            pids_.insert(identity, pid);
            connect(mirror, &mirror::WindowMirror::ready, this, [this, identity, path, pid] {
                pending_.remove(identity);
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
            });
            connect(mirror, &mirror::WindowMirror::failed, this,
                    [this, identity, path](const QString& error) {
                        pending_.remove(identity);
                        mirrors_.remove(identity);
                        pids_.remove(identity);
                        Q_EMIT changed();
                        Q_EMIT failed(path, error);
                    });
            connect(mirror, &mirror::WindowMirror::finished, this, [this, identity, mirror] {
                if (mirrors_.value(identity) == mirror) {
                    mirrors_.remove(identity);
                    pids_.remove(identity);
                    pending_.remove(identity);
                    Q_EMIT changed();
                }
            });
            connect(mirror, &mirror::WindowMirror::finished, mirror, &QObject::deleteLater);
            if (!mirror->start()) {
                mirror->deleteLater();
            }
        });
    connect(launcher, &apps::Launcher::failed, this,
            [this, launcher, identity](const QString& path, const QString& error) {
                launcher->deleteLater();
                pending_.remove(identity);
                Q_EMIT changed();
                LSW_WARN("launch {} failed: {}", path.toStdString(), error.toStdString());
                Q_EMIT failed(path, error);
            });
    LSW_INFO("launch {}", app.target().toStdString());
    if (app.aumid.isEmpty()) {
        launcher->launch(app.path, app.cmdline, app.env);
    } else if (!app.env.trimmed().isEmpty()) {
        // Activation starts the process from Windows' broker, which takes
        // no environment from whoever asked.
        launcher->deleteLater();
        pending_.remove(identity);
        Q_EMIT changed();
        Q_EMIT failed(app.aumid, QStringLiteral("Store applications cannot take environment "
                                                "overrides; remove them in Configure"));
    } else {
        launcher->activate(app.aumid, app.cmdline);
    }
}

QString GuestAppLauncher::key(const apps::AppConfig& app) const {
    return QString::number(rpc_.port()) + QLatin1Char(':') +
           (app.id.isEmpty() ? app.target().toCaseFolded() : app.id);
}

bool GuestAppLauncher::opening(const apps::AppConfig& app) const {
    return pending_.contains(key(app));
}

bool GuestAppLauncher::running(const apps::AppConfig& app) const {
    return !opening(app) && !mirrors_.value(key(app)).isNull();
}

}  // namespace lsw::host::apps
