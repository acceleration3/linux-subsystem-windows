#include "lsw/host/manager/guest_app_launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/apps/app_config.hpp"
#include "lsw/host/apps/desktop_launcher.hpp"
#include "lsw/host/apps/launcher.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/mirror/window_mirror.hpp"

#include <QStringList>

namespace lsw::host::manager {

GuestAppLauncher::GuestAppLauncher(link::AgentRpcClient& rpc,
                                   const config::ManagerSettings& settings, QObject* parent)
    : QObject(parent), rpc_(rpc), settings_(settings) {}

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
                if (pid == 0) {
                    LSW_WARN("launch {}: agent returned pid=0 — old agent without "
                             "PID-return support? rebuild + reinstall the agent",
                             path.toStdString());
                    return;
                }
                LSW_INFO("launch {}: running as pid {}", path.toStdString(), pid);
                // Without this the agent dutifully starts the application
                // inside the guest and nothing of it appears here: the
                // mirror is what carries its windows onto this desktop.
                auto* mirror = new mirror::WindowMirror(rpc_, pid, job, desktop_id, this);
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
    launcher->launch(app.path, app.cmdline, with_channel(app.env, app.channel));
}

QString GuestAppLauncher::with_channel(const QString& env, int channel) const {
    if (channel < 0) {
        return env;
    }
    // Pinning an application to a render channel is done here, in the
    // environment the guest process is started with, because the only
    // thing that can act on it is the driver inside that process. A
    // channel carries one command stream and cannot be shared, so the
    // policy says what to do when the pinned one is busy rather than
    // pretending it can be joined.
    using K = config::ManagerSettings::Key;
    auto policy = config::ChannelBusyPolicy::TakeAnyFree;
    for (const auto& binding : settings_.channel_bindings()) {
        if (binding.channel == channel) {
            policy = binding.busy;
            break;
        }
    }
    QStringList lines =
        env.isEmpty() ? QStringList{} : env.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines << QStringLiteral("RVK_CHANNEL=%1").arg(channel);
    lines << QStringLiteral("RVK_CHANNEL_BUSY=%1")
                 .arg(config::ManagerSettings::policy_to_string(policy));
    lines << QStringLiteral("RVK_CHANNEL_WAIT_MS=%1")
                 .arg(settings_.int_value(K::kChannelWaitSeconds) * 1000);
    return lines.join(QLatin1Char('\n'));
}

}  // namespace lsw::host::manager
