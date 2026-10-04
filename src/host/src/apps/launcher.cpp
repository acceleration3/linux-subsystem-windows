#include "lsw/host/apps/launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::apps {

namespace {

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
    nlohmann::json params = {{"app_id", app_id.toStdString()}};
    if (!args.trimmed().isEmpty()) {
        params["args"] = args.toStdString();
    }
    if (auto vars = env_object(env); !vars.empty()) {
        params["env"] = std::move(vars);
    }
    QtConcurrent::run([&rpc = rpc_, params = std::move(params)] {
        return rpc.request(linkproto::cmd::kLaunch, params);
    }).then(this, [this, app_id](const link::AgentReply& reply) {
        if (!reply.ok) {
            Q_EMIT failed(app_id, QString::fromStdString("agent: " + reply.error));
            return;
        }
        Q_EMIT launched(app_id, static_cast<quint32>(reply.body.value("pid", std::uint64_t{0})),
                        QString::fromStdString(reply.body.value("job", std::string{})));
    });
}

}  // namespace lsw::host::apps
