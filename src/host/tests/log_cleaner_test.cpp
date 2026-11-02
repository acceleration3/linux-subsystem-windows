#include "fake_agent.hpp"

#include "lsw/base/logger.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_guest_files.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/mirror_log.hpp"
#include "lsw/host/services/log_cleaner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "lsw/linkproto/commands.hpp"

using lsw::host::apps::LogRecord;
using lsw::host::apps::LogStore;
using lsw::host::config::Settings;
using lsw::host::link::AgentRpcClient;
using lsw::host::mirror::MirrorLog;
using lsw::host::services::LogCleaner;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void write(const QString& path, const QByteArray& content) {
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QFile file{path};
    (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(content);
}

QByteArray read(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void settle(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void run(LogCleaner& cleaner) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&cleaner, &LogCleaner::finished, &loop, &QEventLoop::quit);
    timeout.start(30000);
    cleaner.start();
    loop.exec();
}

void log_line(const char* text) {
    lsw::base::Logger::write(spdlog::level::info, "test", text);
}

void cleaner_test() {
    Settings settings;
    (void)settings.ensure_directories();
    const QString logs = settings.logs_dir();
    const auto at = [&logs](const char* relative) { return logs + QLatin1Char('/') + QLatin1String(relative); };

    MirrorLog::set_directory(settings.mirror_logs_dir());
    lsw::base::Logger::attach(settings.manager_log_path().toStdString(),
                              lsw::base::RollingFile::kManagerLimits, nullptr);
    log_line("before the clear");
    MirrorLog running{QStringLiteral("notepad")};
    running.roll_for_start();
    running.consume("mirror before\n");

    write(at("manager.1.log"), "older\n");
    write(at("crash-20260101-000000.log"), "crash\n");
    write(at("guest/logs/agent.log"), "guest\n");
    write(at("guest/release.json"), "{}");
    write(at("lsw-logs-20250101-000000.tar.gz"), "archive");
    write(at("mirrors/notepad.1.log"), "older mirror\n");
    write(at("mirrors/closed.log"), "closed mirror\n");
    write(at("vm/avgpu.log"), "gpu\n");
    write(at("vm/avgpu.1.log"), "older gpu\n");
    write(logs + QStringLiteral("/target.log"), "qemu\n");
    QFile::link(at("target.log"), at("vm/qemu.log"));

    LogStore store;
    LogRecord record;
    record.level = QStringLiteral("INFO");
    record.source = QStringLiteral("test");
    record.msg = QStringLiteral("shown");
    store.post(record);
    settle(150);
    check(store.records().size() == 1, "the store holds a record to clear");

    FakeAgent agent;
    agent.on(lsw::linkproto::cmd::kClearLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"deleted", 4}, {"emptied", 2}, {"skipped", {"C:\\locked.log: in use"}}};
    });
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());
    bool online = true;
    bool vm_running = true;
    LogCleaner cleaner{settings, rpc, store, [&] { return online; }, [&] { return vm_running; }};

    int cleared_signals = 0;
    QObject::connect(&store, &LogStore::cleared, [&] { ++cleared_signals; });
    run(cleaner);

    check(!cleaner.running(), "the cleaner is idle afterwards");
    check(!QFile::exists(at("manager.1.log")) && !QFile::exists(at("crash-20260101-000000.log")) &&
              !QFile::exists(at("mirrors/notepad.1.log")) && !QFile::exists(at("vm/avgpu.1.log")),
          "rolled files and crash logs are gone");
    check(!QFile::exists(at("guest")), "the unpacked Windows logs are gone");
    check(!QFile::exists(at("mirrors/closed.log")), "a mirror log nothing writes to is deleted");
    check(QFile::exists(at("lsw-logs-20250101-000000.tar.gz")), "archives are kept");
    check(QFileInfo{at("vm/qemu.log")}.isSymLink() && read(at("vm/qemu.log")) == "qemu\n",
          "the links in vm are kept");
    check(QFile::exists(at("vm/avgpu.log")) && read(at("vm/avgpu.log")).isEmpty(),
          "the GPU log is emptied in place while the VM runs");
    check(QFile::exists(at("manager.log")) && read(at("manager.log")).isEmpty(),
          "manager.log is emptied, not deleted");
    check(QFile::exists(at("mirrors/notepad.log")) && read(at("mirrors/notepad.log")).isEmpty(),
          "a running mirror's log is emptied");
    check(store.records().isEmpty() && cleared_signals == 1, "the store is emptied and says so");

    log_line("after the clear");
    running.consume("mirror after\n");
    settle(150);
    const QByteArray manager = read(at("manager.log"));
    check(manager.contains("after the clear") && !manager.contains("before the clear"),
          "manager.log is written again, from the start");
    check(read(at("mirrors/notepad.log")) == "mirror after\n", "the mirror log is written again, from the start");

    const auto& result = cleaner.result();
    check(result.windows_cleared && result.windows_removed == 4 && result.windows_emptied == 2 &&
              result.windows_skipped == QStringList{QStringLiteral("C:\\locked.log: in use")},
          "Windows' counts and skipped files come back");
    check(!result.complete() && result.detail().contains(QStringLiteral("One file in Windows")),
          "the result names what was left");

    write(at("crash-20260102-000000.log"), "crash\n");
    online = false;
    vm_running = false;
    run(cleaner);
    check(!QFile::exists(at("vm/avgpu.log")),
          "with the VM stopped the GPU log goes too");
    check(!QFile::exists(at("crash-20260102-000000.log")), "the host part runs without an agent");
    check(!cleaner.result().windows_cleared &&
              cleaner.result().windows_note == QStringLiteral("Windows did not answer") &&
              cleaner.result().detail().contains(QStringLiteral("Windows' logs were not cleared")),
          "the result says Windows' logs were not cleared");

    online = true;
    agent.on(lsw::linkproto::cmd::kClearLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"ok", false}, {"error", "disk on fire"}};
    });
    run(cleaner);
    check(!cleaner.result().windows_cleared && cleaner.result().windows_note == QStringLiteral("disk on fire"),
          "an agent that fails is reported with its reason");

    lsw::base::Logger::detach();
    MirrorLog::set_directory({});
}

void helper_test() {
    FakeAgent agent;
    agent.on(lsw::linkproto::cmd::kClearLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"deleted", 3}, {"emptied", 1}, {"skipped", {"a: b", "c: d"}}};
    });
    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());
    lsw::host::link::AgentGuestFiles files{rpc};
    auto cleared = files.clear_logs();
    check(cleared.ok && cleared.deleted == 3 && cleared.emptied == 1 && cleared.skipped.size() == 2 &&
              cleared.skipped[1] == "c: d",
          "clear_logs returns the counts and the skipped files");
    agent.on(lsw::linkproto::cmd::kClearLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"ok", false}, {"error", "no way"}};
    });
    cleared = files.clear_logs();
    check(!cleared.ok && cleared.error == "no way", "clear_logs returns the agent's error");
    AgentRpcClient nobody;
    check(!lsw::host::link::AgentGuestFiles{nobody}.clear_logs().ok, "with no agent, clear_logs fails");
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QCoreApplication app(argc, argv);

    cleaner_test();
    helper_test();
    return g_failures == 0 ? 0 : 1;
}
