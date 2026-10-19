#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/kwin_activation.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/vm/export_socket.hpp"
#include "lsw/linkproto/commands.hpp"

#include <nlohmann/json.hpp>
#include <utility>

namespace lsw::host::mirror {

namespace {

// remotewin's exit status for a guest half from another build: the two
// halves say which features they speak first, and refuse a peer without
// what a mirror needs rather than misread it (remotewin's Hello).
constexpr int kExitWireMismatch = 3;

// The launcher entry the desktop's window presents as: the manager's
// own, since it is the manager's window onto the guest.
constexpr const char* kManagerDesktopId = "lsw-manager";

}  // namespace

WindowMirror* WindowMirror::of_application(link::AgentRpcClient& rpc, const Application& application,
                                           QObject* parent) {
    QStringList arguments{QStringLiteral("--pid"), QString::number(application.pid)};
    if (!application.image.isEmpty()) {
        arguments << QStringLiteral("--image") << application.image;
    }
    if (!application.aumid.isEmpty()) {
        arguments << QStringLiteral("--aumid") << application.aumid;
    }
    if (!application.desktop_id.isEmpty()) {
        arguments << QStringLiteral("--app-id") << application.desktop_id;
    }
    return new WindowMirror(rpc,
                            QStringLiteral("pid %1 '%2'")
                                .arg(application.pid)
                                .arg(application.aumid.isEmpty() ? application.image : application.aumid),
                            std::move(arguments), application.pid, false, application.log_name, parent);
}

WindowMirror* WindowMirror::of_desktop(link::AgentRpcClient& rpc, const QString& title,
                                       QObject* parent) {
    QStringList arguments{QStringLiteral("--desktop"), QStringLiteral("--title"), title,
                          QStringLiteral("--app-id"), QLatin1String(kManagerDesktopId)};
    return new WindowMirror(rpc, QStringLiteral("the desktop"), std::move(arguments), 0, true, QString(),
                            parent);
}

WindowMirror::WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                           quint32 guest_pid, bool desktop, QString log_name, QObject* parent)
    : QObject(parent), rpc_(rpc), what_(std::move(what)),
      host_arguments_(std::move(host_arguments)), guest_pid_(guest_pid), desktop_(desktop),
      log_name_(std::move(log_name)),
      host_end_(new QProcess(this)), deadline_(this) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("No window appeared within 60 seconds. The application may still "
                            "be running in Windows; open the guest desktop or try again."));
    });
    connect(host_end_, &QProcess::readyReadStandardOutput, this, &WindowMirror::read_status);
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

void WindowMirror::on_host_end_finished(int code, QProcess::ExitStatus status) {
    read_status();
    deadline_.stop();
    if (status == QProcess::NormalExit && code == kExitWireMismatch) {
        // remotewin has said what the guest half lacks on its own stderr, just above.
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
    // The GPU device's export socket: with it, and on the GPU the guest's
    // surfaces are on, the guest's frames reach the mirror as host GPU
    // memory instead of pixels over the guest's network. A VM defined
    // without one, or a QEMU that is not listening, is a mirror that takes
    // pixels, which remotewin settles for itself.
    if (const QString socket = vm::ExportSocket::path(); !socket.isEmpty()) {
        arguments << QStringLiteral("--export-socket") << socket;
    }
    arguments << host_arguments_;
    // The mirror's own diagnostics belong beside the manager's.
    host_end_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    deadline_.start(60000);
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
                               guest_confirmed_ = true;
                               announce_ready();
                           }
                       });
}

void WindowMirror::announce_ready() {
    if (failed_ || ready_ || !guest_confirmed_ || !first_frame_)
        return;
    ready_ = true;
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

void WindowMirror::raise() const {
    if (host_end_->state() != QProcess::Running) {
        return;
    }
    host_end_->write("RAISE\n");
    if (!platform::KWinActivation::activate(host_end_->processId())) {
        LSW_INFO("mirror: {} is open; this desktop gave no way to bring it forward",
                 what_.toStdString());
    }
}

}  // namespace lsw::host::mirror
