#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/kwin_activation.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/vm/export_socket.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QProcessEnvironment>
#include <nlohmann/json.hpp>
#include <utility>

namespace lsw::host::mirror {

namespace {
constexpr int kExitWireMismatch = 3;

constexpr const char* kManagerDesktopId = "lsw-manager";

}

WindowMirror* WindowMirror::of_application(link::AgentRpcClient& rpc, const Application& application,
                                           QObject* parent) {
    QStringList arguments{QStringLiteral("--pid"), QString::number(application.pid)};
    if (application.pid_created != 0) {
        arguments << QStringLiteral("--pid-created") << QString::number(application.pid_created);
    }
    if (!application.image.isEmpty()) {
        arguments << QStringLiteral("--image") << application.image;
    }
    if (!application.aumid.isEmpty()) {
        arguments << QStringLiteral("--aumid") << application.aumid;
    }
    if (!application.desktop_id.isEmpty()) {
        arguments << QStringLiteral("--app-id") << application.desktop_id;
    }
    if (!application.output.isEmpty()) {
        arguments << QStringLiteral("--output") << application.output;
    }
    return new WindowMirror(rpc,
                            QStringLiteral("pid %1 '%2'")
                                .arg(application.pid)
                                .arg(application.aumid.isEmpty() ? application.image : application.aumid),
                            std::move(arguments), application.pid, false, application.log_name,
                            application.activation_token, parent);
}

WindowMirror* WindowMirror::of_desktop(link::AgentRpcClient& rpc, const QString& title, const QString& output,
                                       QObject* parent) {
    QStringList arguments{QStringLiteral("--desktop"), QStringLiteral("--title"), title,
                          QStringLiteral("--app-id"), QLatin1String(kManagerDesktopId)};
    if (!output.isEmpty()) arguments << QStringLiteral("--output") << output;
    return new WindowMirror(rpc, QStringLiteral("the desktop"), std::move(arguments), 0, true, QString(),
                            QString(), parent);
}

WindowMirror::WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                           quint32 guest_pid, bool desktop, QString log_name, QString activation_token,
                           QObject* parent)
    : QObject(parent), rpc_(rpc), what_(std::move(what)),
      host_arguments_(std::move(host_arguments)), guest_pid_(guest_pid), desktop_(desktop),
      log_name_(std::move(log_name)), activation_token_(std::move(activation_token)),
      host_end_(new QProcess(this)),
      log_(desktop ? QStringLiteral("desktop")
                   : (log_name_.isEmpty() ? QStringLiteral("app-%1").arg(guest_pid_) : log_name_)),
      deadline_(this) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("No window appeared within 60 seconds. The application may still "
                            "be running in Windows; open the guest desktop or try again."));
    });
    connect(host_end_, &QProcess::readyReadStandardOutput, this, &WindowMirror::read_status);
    connect(host_end_, &QProcess::readyReadStandardError, this, &WindowMirror::read_log);
    connect(host_end_, &QProcess::finished, this, &WindowMirror::on_host_end_finished);
    connect(host_end_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart || error == QProcess::Crashed) {
            fail(QStringLiteral("The window mirror failed: %1").arg(host_end_->errorString()));
            if (error == QProcess::FailedToStart)
                Q_EMIT finished();
        }
    });
}

WindowMirror::~WindowMirror() {
    disconnect(host_end_, nullptr, this, nullptr);
    if (host_end_->state() == QProcess::NotRunning)
        return;
    host_end_->closeWriteChannel();
    if (!host_end_->waitForFinished(250)) {
        host_end_->kill();
        host_end_->waitForFinished(1000);
    }
}

void WindowMirror::read_log() {
    log_.consume(host_end_->readAllStandardError());
}

void WindowMirror::on_host_end_finished(int code, QProcess::ExitStatus status) {
    read_status();
    read_log();
    log_.finish();
    LSW_INFO("mirror: {} exited with code {}{} after {:.1f} s", what_.toStdString(), code,
             status == QProcess::CrashExit ? " (crashed)" : "", static_cast<double>(running_.elapsed()) / 1000.0);
    deadline_.stop();
    if (status == QProcess::NormalExit && code == kExitWireMismatch) {
        LSW_WARN("mirror: {} refused the guest's remotewin-guest.exe: it is from another "
                 "build than this manager's remotewin. Redeploy it (scripts/redeploy-agent.sh, "
                 "or the redeploy_agent MCP tool)",
                 what_.toStdString());
        fail(QStringLiteral("RemoteWin's host and guest versions do not match. Redeploy the "
                            "guest agent and RemoteWin, then open the application again."));
    } else if (!failed_ && (status == QProcess::CrashExit || code != 0 || !ready_)) {
        fail(!ready_ ? QStringLiteral("The application or its mirror exited before a window "
                                      "appeared. Open the guest desktop to check the application.")
                     : QStringLiteral("The window mirror stopped unexpectedly (exit %1). "
                                      "Your application may still be running in Windows.")
                           .arg(code));
    } else {
        LSW_INFO("mirror: {} finished (exit {})", what_.toStdString(), code);
    }
    Q_EMIT finished();
}

bool WindowMirror::start() {
    if (started_)
        return !failed_;
    const QString exe = platform::MirrorExePaths::host();
    if (exe.isEmpty()) {
        LSW_WARN("mirror: no remotewin binary; {} will not be shown. "
                 "Build it or set LSW_REMOTEWIN",
                 what_.toStdString());
        fail(QStringLiteral("RemoteWin is not installed. Build it or set LSW_REMOTEWIN."));
        return false;
    }
    started_ = true;
    QStringList arguments{QStringLiteral("--managed"), QStringLiteral("--port"),
                          QStringLiteral("0")};
    if (const QString socket = vm::ExportSocket::path(); !socket.isEmpty()) {
        arguments << QStringLiteral("--export-socket") << socket;
    }
    arguments << host_arguments_;
    host_end_->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    if (!activation_token_.isEmpty()) {
        environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), std::exchange(activation_token_, QString()));
    }
    host_end_->setProcessEnvironment(environment);
    deadline_.start(60000);
    log_.roll_for_start();
    running_.start();
    LSW_INFO("mirror: starting {} (log mirrors/{}.log)", what_.toStdString(),
             log_.name().toStdString());
    LSW_DEBUG("mirror: {} {}", exe.toStdString(), arguments.join(QLatin1Char(' ')).toStdString());
    host_end_->start(exe, arguments);
    return true;
}

void WindowMirror::read_status() {
    status_buffer_ += host_end_->readAllStandardOutput();
    while (status_buffer_.contains('\n')) {
        const auto line = status_buffer_.first(status_buffer_.indexOf('\n'));
        status_buffer_.remove(0, line.size() + 1);
        if (failed_)
            continue;
        if (line.startsWith("LISTENING ") && !guest_started_) {
            bool valid = false;
            const quint16 port = line.mid(10).toUShort(&valid);
            if (!valid || port == 0) {
                fail(QStringLiteral("RemoteWin reported an invalid listening port."));
                continue;
            }
            guest_started_ = true;
            start_guest(port);
        } else if (line == "READY" && guest_started_) {
            first_frame_ = true;
            announce_ready();
        } else if (line.startsWith("APP ")) {
            const QString aumid = QString::fromUtf8(line.mid(4));
            if (!aumid.isEmpty() && !app_ids_.contains(aumid)) {
                app_ids_ << aumid;
                LSW_INFO("mirror: {} shows the windows of {}", what_.toStdString(), aumid.toStdString());
                Q_EMIT app_identified(aumid);
            }
        }
    }
    if (status_buffer_.size() > 4096) {
        fail(QStringLiteral("RemoteWin returned an invalid startup response."));
        status_buffer_.clear();
    }
}

void WindowMirror::start_guest(quint16 port) {
    LSW_INFO("mirror: listening on {} for {}", port, what_.toStdString());
    nlohmann::json params{{"port", port}};
    if (guest_pid_ != 0) {
        params["pid"] = guest_pid_;
    }
    if (desktop_) {
        params["desktop"] = true;
    }
    if (!log_name_.isEmpty()) {
        params["name"] = log_name_.toStdString();
    }
    rpc_.request_async(linkproto::cmd::kStartWindowMirror, std::move(params), this,
                       [this](const link::AgentReply& reply) {
                           if (!reply.ok && !failed_) {
                               fail(QStringLiteral("The guest window mirror could not start: %1")
                                        .arg(QString::fromStdString(reply.error)));
                           } else if (reply.ok && !failed_) {
                               LSW_DEBUG("mirror: the guest half of {} started", what_.toStdString());
                               guest_confirmed_ = true;
                               announce_ready();
                           }
                       });
}

void WindowMirror::announce_ready() {
    if (failed_ || ready_ || !guest_confirmed_ || !first_frame_)
        return;
    ready_ = true;
    LSW_INFO("mirror: {} is ready after {:.1f} s", what_.toStdString(),
             static_cast<double>(running_.elapsed()) / 1000.0);
    deadline_.stop();
    Q_EMIT ready();
}

void WindowMirror::fail(const QString& reason) {
    if (failed_)
        return;
    failed_ = true;
    deadline_.stop();
    LSW_WARN("mirror: {}: {}", what_.toStdString(), reason.toStdString());
    host_end_->kill();
    Q_EMIT failed(reason);
}

void WindowMirror::raise(const QString& activation_token) const {
    if (host_end_->state() != QProcess::Running) {
        return;
    }
    if (!activation_token.isEmpty()) {
        host_end_->write("RAISE " + activation_token.toUtf8() + "\n");
        return;
    }
    host_end_->write("RAISE\n");
    if (!platform::KWinActivation::activate(host_end_->processId())) {
        LSW_INFO("mirror: {} is open; this desktop gave no way to bring it forward",
                 what_.toStdString());
    }
}

}
