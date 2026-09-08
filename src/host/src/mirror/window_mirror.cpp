#include "lsw/host/mirror/window_mirror.hpp"

#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"

#include "lsw/base/log.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QTcpServer>

#include <nlohmann/json.hpp>

#include <utility>

namespace lsw::host::mirror {

WindowMirror::WindowMirror(link::AgentRpcClient& rpc, quint32 pid, QString job,
                           QString desktop_id, QObject* parent)
    : QObject(parent),
      rpc_(rpc),
      pid_(pid),
      job_(std::move(job)),
      desktop_id_(std::move(desktop_id)),
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

bool WindowMirror::start() {
    const QString exe = platform::MirrorExePaths::host();
    if (exe.isEmpty()) {
        LSW_WARN("mirror: no remotewin binary; pid {} will run with no windows shown. "
                 "Build it or set LSW_REMOTEWIN", pid_);
        return false;
    }
    const quint16 port = free_port();
    if (port == 0) {
        LSW_WARN("mirror: no free port for pid {}", pid_);
        return false;
    }

    QStringList arguments{QStringLiteral("--port"), QString::number(port),
                          QStringLiteral("--pid"), QString::number(pid_)};
    if (!job_.isEmpty()) {
        arguments << QStringLiteral("--job") << job_;
    }
    if (!desktop_id_.isEmpty()) {
        arguments << QStringLiteral("--app-id") << desktop_id_;
    }

    connect(host_end_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        LSW_INFO("mirror: pid {} finished (exit {})", pid_, code);
        Q_EMIT finished(pid_);
    });
    // The mirror's own diagnostics belong beside the manager's.
    host_end_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    host_end_->start(exe, arguments);
    // Waited for, because the guest half is told to dial this port next
    // and a connection refused is a mirror that never appears. Short:
    // this is a process starting on the same machine, not the guest.
    if (!host_end_->waitForStarted(2000)) {
        LSW_WARN("mirror: {} did not start for pid {}", exe.toStdString(), pid_);
        return false;
    }
    LSW_INFO("mirror: {} listening on {} for pid {} job '{}'", exe.toStdString(), port,
             pid_, job_.toStdString());

    const nlohmann::json request = {
        {"cmd", linkproto::cmd::kStartWindowMirror},
        {"params", {{"port", port}}},
    };
    const QByteArray reply = rpc_.call(request);
    if (reply.isEmpty()) {
        LSW_WARN("mirror: the agent did not answer; killing the mirror for pid {}", pid_);
        host_end_->kill();
        return false;
    }
    try {
        const auto answer = nlohmann::json::parse(reply.constData(),
                                                  reply.constData() + reply.size());
        if (!answer.value("ok", false)) {
            LSW_WARN("mirror: the guest half did not start: {}",
                     answer.value("error", std::string{"no detail"}));
            host_end_->kill();
            return false;
        }
    } catch (const nlohmann::json::exception& e) {
        LSW_WARN("mirror: unreadable reply from the agent: {}", e.what());
        host_end_->kill();
        return false;
    }
    return true;
}

}  // namespace lsw::host::mirror
