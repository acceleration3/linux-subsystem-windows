#include "lsw/host/apps/launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::apps {

namespace {

// Carry both the error string and the spawned PID across the
// QtConcurrent boundary. Empty error + non-zero pid = success.
struct LaunchResult {
    QString error;
    quint32 pid = 0;
};

}  // namespace

Launcher::Launcher(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void Launcher::launch(const QString& app_id, bool vulkan) {
    auto* watcher = new QFutureWatcher<LaunchResult>(this);
    connect(watcher, &QFutureWatcher<LaunchResult>::finished, this, [this, watcher, app_id]() {
        const LaunchResult r = watcher->result();
        if (r.error.isEmpty()) {
            Q_EMIT launched(app_id, r.pid);
        } else {
            Q_EMIT failed(app_id, r.error);
        }
        watcher->deleteLater();
    });
    auto fut = QtConcurrent::run([this, app_id, vulkan]() -> LaunchResult {
        LaunchResult out;
        nlohmann::json req = {
            {"cmd", linkproto::cmd::kLaunch},
            {"params", {{"app_id", app_id.toStdString()}, {"vulkan", vulkan}}},
        };
        const auto reply = rpc_.call(req);
        if (reply.isEmpty()) {
            out.error = QStringLiteral("agent unreachable / no reply");
            return out;
        }
        try {
            const auto j = nlohmann::json::parse(reply.constData(),
                                                  reply.constData() + reply.size());
            if (!j.value("ok", false)) {
                const auto detail = j.value("error", std::string{});
                out.error = detail.empty()
                                 ? QStringLiteral("agent reported launch failure (no detail)")
                                 : QString::fromStdString("agent: " + detail);
                return out;
            }
            out.pid = static_cast<quint32>(j.value("pid", std::uint64_t{0}));
        } catch (const nlohmann::json::exception& e) {
            out.error = QString::fromStdString(e.what());
        }
        return out;
    });
    watcher->setFuture(fut);
}

}  // namespace lsw::host::apps
