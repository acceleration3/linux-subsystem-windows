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

// ``NAME=value`` per line → a JSON object for the agent.
//
// Blank lines and ``#`` comments are skipped so a config can be
// annotated, and a line with no ``=`` is dropped rather than sent as a
// nameless variable. The value keeps its spaces — a path may have them
// — but the name is trimmed, because "NAME = value" is what everyone
// types at least once.
nlohmann::json env_object(const QString& text) {
    auto obj = nlohmann::json::object();
    const auto lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const qsizetype eq = trimmed.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString name = trimmed.left(eq).trimmed();
        if (name.isEmpty()) {
            continue;
        }
        obj[name.toStdString()] = trimmed.mid(eq + 1).toStdString();
    }
    return obj;
}

}  // namespace

Launcher::Launcher(link::AgentRpcClient& rpc, QObject* parent)
    : QObject(parent), rpc_(rpc) {}

void Launcher::launch(const QString& app_id, const QString& args,
                      const QString& env) {
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
    auto fut = QtConcurrent::run([this, app_id, args, env]() -> LaunchResult {
        LaunchResult out;
        nlohmann::json params = {{"app_id", app_id.toStdString()}};
        if (!args.trimmed().isEmpty()) {
            params["args"] = args.toStdString();
        }
        if (auto vars = env_object(env); !vars.empty()) {
            params["env"] = std::move(vars);
        }
        nlohmann::json req = {
            {"cmd", linkproto::cmd::kLaunch},
            {"params", std::move(params)},
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
