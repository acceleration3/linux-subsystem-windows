#include "lsw/host/apps/launcher.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/linkproto/app_package.hpp"
#include "lsw/linkproto/commands.hpp"

#include <QtConcurrent>
#include <nlohmann/json.hpp>

namespace lsw::host::apps {

namespace {
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

nlohmann::json documents_array(const QStringList& documents) {
    auto out = nlohmann::json::array();
    for (const QString& document : documents) {
        out.push_back(document.toStdString());
    }
    return out;
}

}

Launcher::Launcher(link::AgentRpcClient& rpc, QObject* parent) : QObject(parent), rpc_(rpc) {}

void Launcher::launch(const QString& app_id, const QString& args, const QString& env,
                      const QStringList& documents) {
    const bool package = linkproto::is_app_package(app_id.toStdString());
    if (package && (!args.trimmed().isEmpty() || !env.trimmed().isEmpty())) {
        Q_EMIT failed(
            app_id,
            QStringLiteral(
                "App Installer packages do not accept launch arguments or environment overrides"));
        return;
    }
    if (package && !documents.isEmpty()) {
        Q_EMIT failed(app_id, QStringLiteral("App Installer packages cannot open documents"));
        return;
    }
    nlohmann::json params =
        package ? nlohmann::json{{"path", app_id.toStdString()}}
                : nlohmann::json{{"app_id", app_id.toStdString()}, {"reuse_existing", true}};
    if (!args.trimmed().isEmpty()) {
        params["args"] = args.toStdString();
    }
    if (auto vars = env_object(env); !vars.empty()) {
        params["env"] = std::move(vars);
    }
    if (!documents.isEmpty() && !package) {
        params["documents"] = documents_array(documents);
    }
    QtConcurrent::run([&rpc = rpc_, package, params = std::move(params)] {
        return rpc.request(package ? linkproto::cmd::kOpenPackage : linkproto::cmd::kLaunch,
                           params);
    }).then(this, [this, app_id, package](const link::AgentReply& reply) {
        if (!reply.ok) {
            Q_EMIT failed(app_id, QString::fromStdString("agent: " + reply.error));
            return;
        }
        const auto pid = static_cast<quint32>(reply.body.value("pid", std::uint64_t{0}));
        if (pid == 0) {
            Q_EMIT failed(app_id, package ? QStringLiteral("agent: App Installer opened but the agent "
                                                           "named no window of it; update the guest agent")
                                          : QStringLiteral("agent: the launch reply carries no pid"));
            return;
        }
        Q_EMIT launched(app_id, pid, QString::fromStdString(reply.body.value("job", std::string{})),
                        reply.body.value("pid_created", std::uint64_t{0}),
                        QString::fromStdString(reply.body.value("image", std::string{})));
    });
}

void Launcher::activate(const QString& aumid, const QString& args, const QStringList& documents) {
    nlohmann::json params{{"aumid", aumid.toStdString()}};
    if (!args.trimmed().isEmpty()) {
        params["args"] = args.toStdString();
    }
    if (!documents.isEmpty()) {
        params["documents"] = documents_array(documents);
    }
    QtConcurrent::run([&rpc = rpc_, params = std::move(params)] {
        return rpc.request(linkproto::cmd::kLaunchStoreApp, params);
    }).then(this, [this, aumid](const link::AgentReply& reply) {
        if (!reply.ok) {
            Q_EMIT failed(aumid, QString::fromStdString("agent: " + reply.error));
            return;
        }
        const auto pid = static_cast<quint32>(reply.body.value("pid", std::uint64_t{0}));
        if (pid == 0) {
            Q_EMIT failed(aumid, QStringLiteral("agent: Windows started it but named no process"));
            return;
        }
        Q_EMIT launched(aumid, pid, QString{}, reply.body.value("pid_created", std::uint64_t{0}), QString{});
    });
}

}
