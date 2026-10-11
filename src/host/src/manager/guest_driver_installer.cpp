#include "lsw/host/manager/guest_driver_installer.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QMetaObject>
#include <QPointer>

#include <utility>

#include <nlohmann/json.hpp>

namespace lsw::host::manager {

namespace {

constexpr int kRestartNeeded = 3010;

}  // namespace

GuestDriverInstaller::GuestDriverInstaller(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

QString GuestDriverInstaller::script(const QString& staging) {
    return QStringLiteral(
               "@echo off\r\n"
               "setlocal\r\n") +
           staging +
           QStringLiteral(
        "if not exist C:\\av\\pristine\\package (\r\n"
        "  echo C:\\av\\pristine has no package: this guest was set up before LSW kept one\r\n"
        "  exit /b 2\r\n"
        ")\r\n"
        "echo Copying the package as shipped\r\n"
        "robocopy C:\\av\\pristine C:\\av\\resign /MIR /NFL /NDL /NJH /NJS /NP >nul\r\n"
        "if %ERRORLEVEL% GEQ 8 exit /b %ERRORLEVEL%\r\n"
        "echo Signing it for the adapters present now\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\av\\provision.ps1 "
        "-Package C:\\av\\resign\\package -Stage sign\r\n"
        "set SIGNED=%ERRORLEVEL%\r\n"
        "if not \"%SIGNED%\"==\"0\" if not \"%SIGNED%\"==\"3010\" exit /b %SIGNED%\r\n"
        "echo Installing it\r\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\av\\provision.ps1 "
        "-Package C:\\av\\resign\\package -Stage install -NoVerifier\r\n"
        "set INSTALLED=%ERRORLEVEL%\r\n"
        "if \"%INSTALLED%\"==\"0\" if \"%SIGNED%\"==\"3010\" exit /b 3010\r\n"
        "exit /b %INSTALLED%\r\n");
}

bool GuestDriverInstaller::start(const QString& staging) {
    if (running()) {
        return true;
    }
    // Subscribed before the job exists, so an end that comes back fast
    // is not missed: until the reply names the job, what arrives is kept
    // (on_event) and replayed against the id (on_job_id). Handlers run on
    // the reader thread; everything is marshalled here before it is
    // looked at.
    QPointer<GuestDriverInstaller> self(this);
    const auto deliver = [self](const nlohmann::json& frame, bool done) {
        Event event;
        event.job = QString::fromStdString(frame.value("job_id", std::string{}));
        event.done = done;
        event.ok = frame.value("ok", false);
        event.exit_code = frame.value("exit_code", 0);
        event.message = QString::fromStdString(frame.value("message", std::string{}));
        QMetaObject::invokeMethod(self, [self, event] {
            if (self) {
                self->on_event(event);
            }
        }, Qt::QueuedConnection);
    };
    progress_sub_ = rpc_.subscribe(linkproto::topic::kSetupProgress,
                                   [deliver](const nlohmann::json& frame) { deliver(frame, false); });
    done_sub_ = rpc_.subscribe(linkproto::topic::kSetupDone,
                               [deliver](const nlohmann::json& frame) { deliver(frame, true); });

    job_.clear();
    early_.clear();
    awaiting_id_ = true;
    Q_EMIT progress(QStringLiteral("Starting"));
    rpc_.request_async(linkproto::cmd::kRunScript,
                       {{"name", staging.isEmpty() ? "install avgpu for new adapters"
                                                   : "update avgpu"},
                        {"script", script(staging).toStdString()}},
                       this, [this](const link::AgentReply& reply) {
                           if (!awaiting_id_) {
                               return;  // abandoned while the call was out
                           }
                           const std::string id = reply.body.value("job_id", std::string{});
                           if (!reply.ok || id.empty()) {
                               awaiting_id_ = false;
                               early_.clear();
                               progress_sub_.reset();
                               done_sub_.reset();
                               Q_EMIT failed(QStringLiteral("the agent would not run it: %1")
                                                 .arg(QString::fromStdString(reply.error)));
                               return;
                           }
                           LSW_INFO("graphics: installing avgpu, job {}", id);
                           on_job_id(QString::fromStdString(id));
                       });
    return true;
}

void GuestDriverInstaller::on_event(const Event& event) {
    if (awaiting_id_) {
        early_.push_back(event);
        return;
    }
    handle(event);
}

void GuestDriverInstaller::on_job_id(const QString& id) {
    awaiting_id_ = false;
    job_ = id;
    const QVector<Event> early = std::exchange(early_, {});
    for (const Event& event : early) {
        if (job_.isEmpty()) {
            break;  // finished by one of them
        }
        handle(event);
    }
}

void GuestDriverInstaller::handle(const Event& event) {
    if (job_.isEmpty() || event.job != job_) {
        return;  // another job's, or none is ours
    }
    if (event.done) {
        finish(event.ok, event.exit_code, event.message);
    } else if (!event.message.isEmpty()) {
        Q_EMIT progress(event.message);
    }
}

void GuestDriverInstaller::abandon(const QString& reason) {
    if (!running()) {
        return;
    }
    awaiting_id_ = false;
    early_.clear();
    job_.clear();
    progress_sub_.reset();
    done_sub_.reset();
    Q_EMIT failed(reason);
}

void GuestDriverInstaller::finish(bool ok, int exit_code, const QString& message) {
    job_.clear();
    progress_sub_.reset();
    done_sub_.reset();
    LSW_INFO("graphics: driver install finished ok={} exit={}", ok, exit_code);
    if (ok || exit_code == 0) {
        Q_EMIT succeeded(false);
    } else if (exit_code == kRestartNeeded) {
        Q_EMIT succeeded(true);
    } else {
        Q_EMIT failed(message.isEmpty() ? QStringLiteral("exit code %1").arg(exit_code) : message);
    }
}

}  // namespace lsw::host::manager
