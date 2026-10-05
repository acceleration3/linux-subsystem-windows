#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/kwin_activation.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/vm/export_socket.hpp"

#include "lsw/base/log.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QTcpServer>

#include <nlohmann/json.hpp>

#include <utility>

namespace lsw::host::mirror {

namespace {

// remotewin's exit status for a guest half from another build: the two
// halves say their wire version to each other first, and refuse a
// mismatch rather than misread it (remotewin's WireHello).
constexpr int kExitWireMismatch = 3;

// The launcher entry the desktop's window presents as: the manager's
// own, since it is the manager's window onto the guest.
constexpr const char* kManagerDesktopId = "lsw-manager";

}  // namespace

WindowMirror* WindowMirror::of_application(link::AgentRpcClient& rpc, quint32 pid,
                                           const QString& job, const QString& desktop_id,
                                           QObject* parent) {
    QStringList arguments{QStringLiteral("--pid"), QString::number(pid)};
    if (!job.isEmpty()) {
        arguments << QStringLiteral("--job") << job;
    }
    if (!desktop_id.isEmpty()) {
        arguments << QStringLiteral("--app-id") << desktop_id;
    }
    return new WindowMirror(rpc,
                            QStringLiteral("pid %1 job '%2'").arg(pid).arg(job),
                            std::move(arguments), pid, false, parent);
}

WindowMirror* WindowMirror::of_desktop(link::AgentRpcClient& rpc, const QString& title,
                                       QObject* parent) {
    QStringList arguments{QStringLiteral("--desktop"), QStringLiteral("--title"), title,
                          QStringLiteral("--app-id"), QLatin1String(kManagerDesktopId)};
    return new WindowMirror(rpc, QStringLiteral("the desktop"), std::move(arguments), 0, true,
                            parent);
}

WindowMirror::WindowMirror(link::AgentRpcClient& rpc, QString what, QStringList host_arguments,
                           quint32 guest_pid, bool desktop, QObject* parent)
    : QObject(parent),
      rpc_(rpc),
      what_(std::move(what)),
      host_arguments_(std::move(host_arguments)),
      guest_pid_(guest_pid),
      desktop_(desktop),
      host_end_(new QProcess(this)) {}

quint16 WindowMirror::free_port() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

void WindowMirror::on_host_end_finished(int code, QProcess::ExitStatus status) {
    if (status == QProcess::NormalExit && code == kExitWireMismatch) {
        // remotewin has said which versions on its own stderr, just above.
        LSW_WARN("mirror: {} refused the guest's remotewin-guest.exe: it is from another "
                 "build than this manager's remotewin. Redeploy it (scripts/redeploy-agent.sh, "
                 "or the redeploy_agent MCP tool)",
                 what_.toStdString());
    } else {
        LSW_INFO("mirror: {} finished (exit {})", what_.toStdString(), code);
    }
    Q_EMIT finished();
}

bool WindowMirror::start() {
    const QString exe = platform::MirrorExePaths::host();
    if (exe.isEmpty()) {
        LSW_WARN("mirror: no remotewin binary; {} will not be shown. "
                 "Build it or set LSW_REMOTEWIN", what_.toStdString());
        return false;
    }
    const quint16 port = free_port();
    if (port == 0) {
        LSW_WARN("mirror: no free port for {}", what_.toStdString());
        return false;
    }

    QStringList arguments{QStringLiteral("--port"), QString::number(port)};
    // The GPU device's export socket: with it, and on the GPU the guest's
    // surfaces are on, the guest's frames reach the mirror as host GPU
    // memory instead of pixels over the guest's network. A VM defined
    // without one, or a QEMU that is not listening, is a mirror that takes
    // pixels, which remotewin settles for itself.
    if (const QString socket = vm::ExportSocket::path(); !socket.isEmpty()) {
        arguments << QStringLiteral("--export-socket") << socket;
    }
    arguments << host_arguments_;
    connect(host_end_, &QProcess::finished, this, &WindowMirror::on_host_end_finished);
    // The mirror's own diagnostics belong beside the manager's.
    host_end_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    host_end_->start(exe, arguments);
    // Waited for, because the guest half is told to dial this port next
    // and a connection refused is a mirror that never appears. Short:
    // this is a process starting on the same machine, not the guest.
    if (!host_end_->waitForStarted(2000)) {
        LSW_WARN("mirror: {} did not start for {}", exe.toStdString(), what_.toStdString());
        return false;
    }
    LSW_INFO("mirror: {} listening on {} for {}", exe.toStdString(), port, what_.toStdString());

    nlohmann::json params{{"port", port}};
    if (guest_pid_ != 0) {
        params["pid"] = guest_pid_;
    }
    if (desktop_) {
        params["desktop"] = true;
    }
    const auto reply = rpc_.request(linkproto::cmd::kStartWindowMirror, params);
    if (!reply.ok) {
        LSW_WARN("mirror: the guest half did not start for {}: {}", what_.toStdString(),
                 reply.error);
        host_end_->kill();
        return false;
    }
    return true;
}

void WindowMirror::raise() const {
    if (host_end_->state() != QProcess::Running) {
        return;
    }
    if (!platform::KWinActivation::activate(host_end_->processId())) {
        LSW_INFO("mirror: {} is open; this desktop gave no way to bring it forward",
                 what_.toStdString());
    }
}

}  // namespace lsw::host::mirror
