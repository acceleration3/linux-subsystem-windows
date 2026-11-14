#include "lsw/host/apps/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/guest_mirrors.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <nlohmann/json.hpp>

namespace lsw::host::apps {

namespace {
constexpr qsizetype kMostEnded = 64;
}

GuestAppLauncher::GuestAppLauncher(link::AgentRpcClient& rpc, mirror::GuestMirrors& mirrors, QObject* parent)
    : QObject(parent), rpc_(rpc), mirrors_(mirrors) {
    connect(&mirrors_, &mirror::GuestMirrors::shown, this, &GuestAppLauncher::shown);
    connect(&mirrors_, &mirror::GuestMirrors::failed, this, &GuestAppLauncher::mirror_failed);
    connect(&mirrors_, &mirror::GuestMirrors::closed, this, &GuestAppLauncher::mirror_closed);
    connect(&mirrors_, &mirror::GuestMirrors::merged, this, &GuestAppLauncher::merged);
    ended_sub_ = rpc_.subscribe(linkproto::topic::kLaunchEnded, [this](const nlohmann::json& f) {
        const auto pid = f.value("pid", quint32{0});
        const auto created = f.value("created", quint64{0});
        if (pid == 0) {
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, pid, created] { launch_ended(pid, created); }, Qt::QueuedConnection);
    });
}

void GuestAppLauncher::launch_ended(quint32 pid, quint64 created) {
    const QString identity = mirrors_.identity_of(pid, created);
    if (identity.isEmpty()) {
        ended_.append({pid, created});
        if (ended_.size() > kMostEnded) {
            ended_.removeFirst();
        }
        return;
    }
    if (!pending_.contains(identity)) {
        if (mirrors_.mirror(identity) == nullptr) {
            LSW_INFO("launch {}: every process ended while the desktop showed it", identity.toStdString());
            mirrors_.forget(identity);
            launched_.remove(identity);
            Q_EMIT changed();
        }
        return;
    }
    LSW_INFO("launch {}: every process ended before a window was shown", identity.toStdString());
    mirrors_.forget(identity);
    launched_.remove(identity);
    pending_.remove(identity);
    Q_EMIT changed();
    flush_queued(identity);
}

void GuestAppLauncher::shown(const QString& identity) {
    const auto launch = launched_.constFind(identity);
    if (launch == launched_.cend() || !pending_.remove(identity)) {
        return;
    }
    const QString path = launch->path;
    LSW_DEBUG("launch {}: its window is shown", path.toStdString());
    Q_EMIT changed();
    Q_EMIT launched(path, mirrors_.pid(identity));
    flush_queued(identity);
}

void GuestAppLauncher::mirror_failed(const QString& identity, const QString& error) {
    if (!launched_.contains(identity)) {
        return;
    }
    const Launched launch = launched_.take(identity);
    pending_.remove(identity);
    Q_EMIT changed();
    Q_EMIT failed(launch.path, error, launch.documents);
    flush_queued(identity);
}

void GuestAppLauncher::mirror_closed(const QString& identity) {
    if (!launched_.remove(identity)) {
        return;
    }
    pending_.remove(identity);
    Q_EMIT changed();
    flush_queued(identity);
}

void GuestAppLauncher::merged(const QString& identity, const QString& into) {
    if (!launched_.contains(identity)) {
        return;
    }
    const Launched launch = launched_.take(identity);
    LSW_INFO("launch {}: its windows are shown by another mirror already; that one is kept",
             launch.path.toStdString());
    pending_.remove(identity);
    Q_EMIT changed();
    Q_EMIT launched(launch.path, mirrors_.pid(into));
    flush_queued(identity);
}

void GuestAppLauncher::launch(const apps::AppConfig& app, const QString& activation_token,
                              const QStringList& documents) {
    const QString identity = key(app);
    if (pending_.contains(identity)) {
        LSW_DEBUG("launch {}: already opening{}", app.target().toStdString(),
                  documents.isEmpty() ? "" : "; the documents wait");
        if (!documents.isEmpty()) {
            Queued& queued = queued_[identity];
            queued.app = app;
            queued.activation_token = activation_token;
            queued.documents << documents;
        }
        return;
    }
    const QPointer<mirror::WindowMirror> existing = mirrors_.mirror(identity);
    if (documents.isEmpty() && mirrors_.has(identity) &&
        (!app.cmdline.trimmed().isEmpty() || !app.env.trimmed().isEmpty())) {
        LSW_DEBUG("launch {}: already open; raising its window", app.target().toStdString());
        mirrors_.raise(identity, activation_token);
        Q_EMIT launched(app.target(), mirrors_.pid(identity));
        return;
    }
    pending_.insert(identity);
    Q_EMIT changed();
    const QString desktop_id =
        app.id.isEmpty() ? QString{} : apps::DesktopLauncher::entry_id(app.id);

    const bool executable =
        app.aumid.isEmpty() && app.path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
    QString target = app.target();
    const QString log_name =
        app.id.isEmpty() ? QFileInfo(target.replace(QLatin1Char('\\'), QLatin1Char('/'))).completeBaseName()
                         : app.id;

    auto* launcher = new apps::Launcher(rpc_, this);
    connect(
        launcher, &apps::Launcher::launched, this,
        [this, launcher, desktop_id, identity, existing, app_path = app.path, app_aumid = app.aumid,
         executable, log_name, activation_token, documents](
            const QString& path, quint32 pid, const QString& , quint64 pid_created,
            const QString& image) {
            const bool has_documents = !documents.isEmpty();
            launcher->deleteLater();
            LSW_INFO("launch {}: opened, pid {}", path.toStdString(), pid);
            if (existing && mirrors_.mirror(identity) == existing &&
                (has_documents || mirrors_.pid(identity) == pid)) {
                pending_.remove(identity);
                mirrors_.raise(identity, activation_token);
                mirrors_.launch_settled();
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
                flush_queued(identity);
                return;
            }
            if (ended_.removeOne({pid, pid_created})) {
                LSW_INFO("launch {}: every process ended before a window was shown", path.toStdString());
                pending_.remove(identity);
                mirrors_.launch_settled();
                Q_EMIT changed();
                flush_queued(identity);
                return;
            }
            const mirror::WindowMirror::Application application{
                pid, !image.isEmpty() ? image : executable ? app_path : QString(), app_aumid, desktop_id, log_name,
                activation_token, QString(), pid_created};
            launched_.insert(identity, {path, documents});
            mirrors_.show(identity, application, has_documents);
            mirrors_.launch_settled();
        });
    connect(launcher, &apps::Launcher::failed, this,
            [this, launcher, identity, documents](const QString& path, const QString& error) {
                launcher->deleteLater();
                pending_.remove(identity);
                mirrors_.launch_settled();
                Q_EMIT changed();
                LSW_WARN("launch {} failed: {}", path.toStdString(), error.toStdString());
                Q_EMIT failed(path, error, documents);
                flush_queued(identity);
            });
    LSW_INFO("launch {}", app.target().toStdString());
    LSW_DEBUG("launch {}: {} documents, command line '{}'", app.target().toStdString(), documents.size(),
              app.cmdline.toStdString());
    if (app.aumid.isEmpty()) {
        mirrors_.expect_launch();
        launcher->launch(app.path, app.cmdline, app.env, documents);
    } else if (!app.env.trimmed().isEmpty()) {
        launcher->deleteLater();
        pending_.remove(identity);
        Q_EMIT changed();
        Q_EMIT failed(app.aumid, QStringLiteral("Store applications cannot take environment "
                                                "overrides; remove them in Configure"),
                      documents);
    } else {
        mirrors_.expect_launch();
        launcher->activate(app.aumid, app.cmdline, documents);
    }
}

void GuestAppLauncher::flush_queued(const QString& identity) {
    if (!queued_.contains(identity) || pending_.contains(identity)) {
        return;
    }
    const Queued queued = queued_.take(identity);
    launch(queued.app, queued.activation_token, queued.documents);
}

QString GuestAppLauncher::key(const apps::AppConfig& app) const {
    return QString::number(rpc_.port()) + QLatin1Char(':') +
           (app.id.isEmpty() ? app.target().toCaseFolded() : app.id);
}

bool GuestAppLauncher::opening(const apps::AppConfig& app) const {
    return pending_.contains(key(app));
}

bool GuestAppLauncher::running(const apps::AppConfig& app) const {
    return !opening(app) && mirrors_.has(key(app));
}

}
