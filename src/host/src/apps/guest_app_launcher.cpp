#include "lsw/host/apps/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QFileInfo>
#include <QMetaObject>

#include <nlohmann/json.hpp>
#include <iterator>

namespace lsw::host::apps {

namespace {
constexpr qsizetype kMostEnded = 64;
}

GuestAppLauncher::GuestAppLauncher(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {
    ended_sub_ = rpc_.subscribe(linkproto::topic::kLaunchEnded, [this](const nlohmann::json& f) {
        const auto pid = f.value("pid", quint32{0});
        if (pid == 0) {
            return;
        }
        QMetaObject::invokeMethod(this, [this, pid] { launch_ended(pid); }, Qt::QueuedConnection);
    });
}

void GuestAppLauncher::launch_ended(quint32 pid) {
    QString identity;
    for (auto it = pids_.cbegin(); it != pids_.cend(); ++it) {
        if (it.value() == pid) {
            identity = it.key();
            break;
        }
    }
    if (identity.isEmpty()) {
        ended_.append(pid);
        if (ended_.size() > kMostEnded) {
            ended_.removeFirst();
        }
        return;
    }
    if (!pending_.contains(identity)) {
        return;
    }
    LSW_INFO("launch {}: every process ended before a window was shown", identity.toStdString());
    const QPointer<mirror::WindowMirror> mirror = mirrors_.value(identity);
    forget(identity);
    pending_.remove(identity);
    if (mirror) {
        mirror->deleteLater();
    }
    Q_EMIT changed();
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
    const auto existing = mirrors_.value(identity);
    if (documents.isEmpty() && existing &&
        (!app.cmdline.trimmed().isEmpty() || !app.env.trimmed().isEmpty())) {
        LSW_DEBUG("launch {}: already open; raising its window", app.target().toStdString());
        existing->raise(activation_token);
        Q_EMIT launched(app.target(), pids_.value(identity));
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
            if (existing && mirrors_.value(identity) == existing &&
                (has_documents || pids_.value(identity) == pid)) {
                pending_.remove(identity);
                existing->raise(activation_token);
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
                flush_queued(identity);
                return;
            }
            if (ended_.removeOne(pid)) {
                LSW_INFO("launch {}: every process ended before a window was shown", path.toStdString());
                pending_.remove(identity);
                Q_EMIT changed();
                flush_queued(identity);
                return;
            }
            if (existing && !has_documents)
                existing->deleteLater();
            const mirror::WindowMirror::Application application{
                pid, !image.isEmpty() ? image : executable ? app_path : QString(), app_aumid, desktop_id, log_name, activation_token,
                output_source_ ? output_source_() : QString(), pid_created};
            auto* mirror = mirror::WindowMirror::of_application(rpc_, application, this);
            mirrors_.insert(identity, mirror);
            pids_.insert(identity, pid);
            connect(mirror, &mirror::WindowMirror::app_identified, this,
                    [this, identity, path](const QString& aumid) { identified(identity, aumid, path); });
            connect(mirror, &mirror::WindowMirror::ready, this, [this, identity, path, pid] {
                LSW_DEBUG("launch {}: its window is shown", path.toStdString());
                pending_.remove(identity);
                Q_EMIT changed();
                Q_EMIT launched(path, pid);
                flush_queued(identity);
            });
            connect(mirror, &mirror::WindowMirror::failed, this,
                    [this, identity, path, documents](const QString& error) {
                        pending_.remove(identity);
                        forget(identity);
                        Q_EMIT changed();
                        Q_EMIT failed(path, error, documents);
                        flush_queued(identity);
                    });
            connect(mirror, &mirror::WindowMirror::finished, this, [this, identity, mirror] {
                if (mirrors_.value(identity) == mirror) {
                    forget(identity);
                    pending_.remove(identity);
                    Q_EMIT changed();
                }
                flush_queued(identity);
            });
            connect(mirror, &mirror::WindowMirror::finished, mirror, &QObject::deleteLater);
            if (!mirror->start()) {
                mirror->deleteLater();
            }
        });
    connect(launcher, &apps::Launcher::failed, this,
            [this, launcher, identity, documents](const QString& path, const QString& error) {
                launcher->deleteLater();
                pending_.remove(identity);
                Q_EMIT changed();
                LSW_WARN("launch {} failed: {}", path.toStdString(), error.toStdString());
                Q_EMIT failed(path, error, documents);
                flush_queued(identity);
            });
    LSW_INFO("launch {}", app.target().toStdString());
    LSW_DEBUG("launch {}: {} documents, command line '{}'", app.target().toStdString(), documents.size(),
              app.cmdline.toStdString());
    if (app.aumid.isEmpty()) {
        launcher->launch(app.path, app.cmdline, app.env, documents);
    } else if (!app.env.trimmed().isEmpty()) {
        launcher->deleteLater();
        pending_.remove(identity);
        Q_EMIT changed();
        Q_EMIT failed(app.aumid, QStringLiteral("Store applications cannot take environment "
                                                "overrides; remove them in Configure"),
                      documents);
    } else {
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
    other->raise(QString());
    Q_EMIT changed();
    Q_EMIT launched(path, pids_.value(shown_by));
    if (!duplicate.isNull()) duplicate->deleteLater();
    flush_queued(identity);
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

}
