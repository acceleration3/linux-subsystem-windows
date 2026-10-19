#include "lsw/host/apps/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

#include <QFileInfo>
#include <iterator>

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

    // remotewin matches the launched program's windows by its image, which
    // a shortcut is not.
    const bool executable =
        app.aumid.isEmpty() && app.path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
    QString target = app.target();
    const QString log_name =
        app.id.isEmpty() ? QFileInfo(target.replace(QLatin1Char('\\'), QLatin1Char('/'))).completeBaseName()
                         : app.id;

    // One Launcher per launch, gone once the agent has answered, so each
    // answer is matched to the identity worked out for it here.
    auto* launcher = new apps::Launcher(rpc_, this);
    connect(
        launcher, &apps::Launcher::launched, this,
        [this, launcher, desktop_id, identity, existing, app_path = app.path, app_aumid = app.aumid,
         executable, log_name](const QString& path, quint32 pid, const QString& /*job*/) {
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
            const mirror::WindowMirror::Application application{
                pid, executable ? app_path : QString(), app_aumid, desktop_id, log_name};
            auto* mirror = mirror::WindowMirror::of_application(rpc_, application, this);
            mirrors_.insert(identity, mirror);
            pids_.insert(identity, pid);
            connect(mirror, &mirror::WindowMirror::app_identified, this,
                    [this, identity, path](const QString& aumid) { identified(identity, aumid, path); });
            connect(mirror, &mirror::WindowMirror::ready, this, [this, identity, path, pid] {
                pending_.remove(identity);
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
            });
            connect(mirror, &mirror::WindowMirror::failed, this,
                    [this, identity, path](const QString& error) {
                        pending_.remove(identity);
                        forget(identity);
                        Q_EMIT changed();
                        Q_EMIT failed(path, error);
                    });
            connect(mirror, &mirror::WindowMirror::finished, this, [this, identity, mirror] {
                if (mirrors_.value(identity) == mirror) {
                    forget(identity);
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

void GuestAppLauncher::identified(const QString& identity, const QString& aumid, const QString& path) {
    const QString shown_by = shown_by_.value(aumid);
    const auto other = mirrors_.value(shown_by);
    if (shown_by.isEmpty() || shown_by == identity || other.isNull()) {
        shown_by_.insert(aumid, identity);
        return;
    }
    LSW_INFO("launch {}: its windows are {}'s, which another mirror shows already; that one is kept",
             path.toStdString(), aumid.toStdString());
    const auto duplicate = mirrors_.value(identity);
    forget(identity);
    pending_.remove(identity);
    other->raise();
    Q_EMIT changed();
    Q_EMIT launched(path, pids_.value(shown_by));
    if (!duplicate.isNull()) duplicate->deleteLater();
}

void GuestAppLauncher::forget(const QString& identity) {
    mirrors_.remove(identity);
    pids_.remove(identity);
    for (auto it = shown_by_.begin(); it != shown_by_.end();) {
        it = it.value() == identity ? shown_by_.erase(it) : std::next(it);
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
