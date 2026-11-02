#include "fake_agent.hpp"

#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_guest_files.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/agent_log_levels.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/vm/guest_stats_probe.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"
#include "lsw/host/vm/lifecycle.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#include "lsw/linkproto/commands.hpp"

using lsw::host::config::LogVerbosity;
using lsw::host::config::ManagerSettings;
using lsw::host::link::AgentGuestFiles;
using lsw::host::link::AgentRpcClient;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

class NoProbe : public lsw::host::vm::IGuestStatsProbe {
public:
    bool available() const override { return false; }
    lsw::host::vm::GuestStatsSample sample() override { return {}; }
};

QByteArray pattern(qsizetype size) {
    QByteArray bytes(size, '\0');
    for (qsizetype i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>((i * 31 + i / 251) & 0xff);
    }
    return bytes;
}

void serve_file(FakeAgent& agent, const QByteArray& content) {
    agent.on(lsw::linkproto::cmd::kReadFile, [content](const nlohmann::json& params) {
        const auto offset = params.value("offset", std::uint64_t{0});
        const auto length = params.value("length", std::uint64_t{0});
        const auto start = std::min<std::uint64_t>(offset, static_cast<std::uint64_t>(content.size()));
        const auto count = std::min<std::uint64_t>(length, static_cast<std::uint64_t>(content.size()) - start);
        const QByteArray slice = content.mid(static_cast<qsizetype>(start), static_cast<qsizetype>(count));
        return nlohmann::json{{"data", slice.toBase64().toStdString()},
                              {"eof", start + count >= static_cast<std::uint64_t>(content.size())}};
    });
}

bool wait_for(const std::function<bool()>& done, int ms = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

void files_test(QTemporaryDir& dir) {
    FakeAgent agent;
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());
    AgentGuestFiles files{rpc};

    const QByteArray content = pattern(2 * 1024 * 1024 + 12345);
    serve_file(agent, content);

    QByteArray got;
    std::string error;
    check(files.read("C:\\big.bin", got, error) && got == content, "read joins the chunks into the file");
    check(agent.count(lsw::linkproto::cmd::kReadFile) == 3, "a file of 2 MiB and a bit takes three chunks");
    bool delete_sent = false;
    for (const auto& request : agent.requests()) {
        delete_sent = delete_sent || request.second.value("delete_at_eof", false);
    }
    check(!delete_sent, "the file is kept in Windows unless asked");

    const QString local = dir.filePath(QStringLiteral("pulled.bin"));
    check(files.read_to_file("C:\\big.bin", local, error, true), "read_to_file succeeds");
    QFile file{local};
    check(file.open(QIODevice::ReadOnly) && file.readAll() == content, "read_to_file writes what was read");
    int deletes = 0;
    for (const auto& request : agent.requests()) {
        deletes += request.second.value("delete_at_eof", false) ? 1 : 0;
    }
    check(deletes == 3, "delete_at_eof goes with every chunk when asked");

    agent.on(lsw::linkproto::cmd::kReadFile, [](const nlohmann::json&) {
        return nlohmann::json{{"ok", false}, {"error", "no such file"}};
    });
    const QString failed = dir.filePath(QStringLiteral("failed.bin"));
    error.clear();
    check(!files.read_to_file("C:\\none", failed, error) && error == "no such file",
          "an error from the agent is returned as it came");
    check(!QFile::exists(failed), "a failed pull leaves no file behind");

    agent.on(lsw::linkproto::cmd::kReadFile, [](const nlohmann::json&) {
        return nlohmann::json{{"data", ""}, {"eof", false}};
    });
    error.clear();
    check(!files.read("C:\\stuck", got, error) && !error.empty(),
          "a reply with no data and no end is an error, not a loop");

    error.clear();
    check(!files.read_to_file("C:\\x", dir.filePath(QStringLiteral("missing/dir/x")), error) && !error.empty(),
          "an unwritable local path is an error");

    agent.on(lsw::linkproto::cmd::kPackLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"path", "C:\\ProgramData\\LSW\\logs-1.zip"}, {"size", 4242}};
    });
    auto packed = files.pack_logs();
    check(packed.ok && packed.path == "C:\\ProgramData\\LSW\\logs-1.zip" && packed.size == 4242,
          "pack_logs returns the path and size");
    agent.on(lsw::linkproto::cmd::kPackLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"ok", false}, {"error", "disk full"}};
    });
    packed = files.pack_logs();
    check(!packed.ok && packed.error == "disk full", "pack_logs returns the agent's error");

    AgentRpcClient nobody;
    AgentGuestFiles lonely{nobody};
    error.clear();
    check(!lonely.read("C:\\x", got, error) && !error.empty(), "with no agent, a read fails with a reason");
    check(!lonely.pack_logs().ok, "with no agent, pack_logs fails");
}

void levels_test() {
    FakeAgent agent;
    agent.on(lsw::linkproto::cmd::kSetLogLevel, [](const nlohmann::json&) { return nlohmann::json::object(); });
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());
    lsw::host::config::Settings settings;
    ManagerSettings manager_settings;
    lsw::host::vm::LibvirtConnection libvirt;
    lsw::host::vm::Lifecycle lifecycle{libvirt};
    NoProbe probe;
    lsw::host::services::StatusHub hub{libvirt, lifecycle, rpc, probe, settings, manager_settings};
    LogVerbosity verbosity{manager_settings};
    lsw::host::services::AgentLogLevels levels{hub, rpc, manager_settings, verbosity};

    const auto sets = [&] { return agent.count(lsw::linkproto::cmd::kSetLogLevel); };
    const auto last = [&] {
        nlohmann::json params;
        for (const auto& request : agent.requests()) {
            if (request.first == lsw::linkproto::cmd::kSetLogLevel) {
                params = request.second;
            }
        }
        return params;
    };

    verbosity.set_level(LogVerbosity::Part::Agent, QStringLiteral("trace"));
    QCoreApplication::processEvents();
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(sets() == 0, "nothing is sent while the agent is offline");

    lsw::host::services::VmStatus online;
    online.stats.running = true;
    online.stats.agent_reachable = true;
    hub.set_vm(online);
    check(wait_for([&] { return sets() == 1; }), "the levels are sent when the agent comes online");
    check(last().value("agent", "") == "trace" && last().value("guest_mirrors", "") == "info",
          "they are the agent's and the Windows mirrors' effective levels");

    hub.set_vm(online);
    settle.restart();
    while (settle.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(sets() == 1, "a status that changes nothing about the agent sends nothing");

    verbosity.set_level(LogVerbosity::Part::Agent, QStringLiteral("warn"));
    check(wait_for([&] { return sets() == 2; }) && last().value("agent", "") == "warn",
          "a changed level goes to the online agent at once");

    verbosity.start_debug();
    check(wait_for([&] { return sets() >= 3; }) && last().value("agent", "") == "debug" &&
              last().value("guest_mirrors", "") == "debug",
          "debug logging raises both to debug");
    verbosity.stop_debug();
    check(wait_for([&] { return last().value("agent", "") == "warn"; }),
          "and stopping it puts them back");

    lsw::host::services::VmStatus offline;
    hub.set_vm(offline);
    const int before = sets();
    verbosity.set_level(LogVerbosity::Part::Agent, QStringLiteral("error"));
    settle.restart();
    while (settle.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(sets() == before, "an agent that went offline is not sent to");
    hub.set_vm(online);
    check(wait_for([&] { return sets() == before + 1; }) && last().value("agent", "") == "error",
          "and gets the current levels when it returns");
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;

    files_test(dir);
    levels_test();
    return g_failures == 0 ? 0 : 1;
}
