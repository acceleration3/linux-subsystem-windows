#include "fake_agent.hpp"
#include "zip_fixture.hpp"

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/zip_extractor.hpp"
#include "lsw/host/services/log_collector.hpp"
#include "lsw/host/services/settings_report.hpp"
#include "lsw/host/services/system_report.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "lsw/linkproto/commands.hpp"

using lsw::host::config::ManagerSettings;
using lsw::host::config::Settings;
using lsw::host::link::AgentRpcClient;
using lsw::host::services::LogCollector;
using lsw::host::services::SettingsReport;
using lsw::host::services::SystemReport;

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

QByteArray tar_member(const QString& archive, const QString& member) {
    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-xzOf"), archive, member});
    tar.waitForFinished(10000);
    return tar.readAllStandardOutput();
}

QStringList tar_list(const QString& archive) {
    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-tzf"), archive});
    tar.waitForFinished(10000);
    QStringList names = QString::fromUtf8(tar.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    names.sort();
    return names;
}

struct Run {
    bool ok = false;
    QString archive;
    QString error;
    QVector<int> started;
    QVector<QPair<int, QString>> skipped;
};

Run collect(LogCollector& collector) {
    Run run;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&collector, &LogCollector::step_started, [&](int step) { run.started.push_back(step); });
    QObject::connect(&collector, &LogCollector::step_skipped,
                     [&](int step, const QString& why) { run.skipped.push_back({step, why}); });
    QObject::connect(&collector, &LogCollector::finished, &loop, [&](const QString& path) {
        run.ok = true;
        run.archive = path;
        loop.quit();
    });
    QObject::connect(&collector, &LogCollector::failed, &loop, [&](const QString& error) {
        run.error = error;
        loop.quit();
    });
    timeout.start(60000);
    collector.start();
    loop.exec();
    QCoreApplication::processEvents();
    QObject::disconnect(&collector, nullptr, nullptr, nullptr);
    return run;
}

void settings_test() {
    for (const char* key : {"files/guest_password", "files/guest_Password", "account/token", "api_key",
                            "licence/product_key", "vm/secret", "net.passphrase", "key"}) {
        check(SettingsReport::secret(QString::fromLatin1(key)), key);
    }
    for (const char* key : {"files/guest_user", "machine/cpu_cores", "graphics/host_gpu",
                            "files/host_shares", "logs/manager", "display/modes", "usb/always",
                            "monkey/business", "keyboard/layout"}) {
        check(!SettingsReport::secret(QString::fromLatin1(key)), key);
    }

    Settings settings;
    ManagerSettings manager;
    manager.set_value(QStringLiteral("files/guest_password"), QStringLiteral("hunter2"));
    manager.set_value(QStringLiteral("files/guest_user"), QStringLiteral("someone"));
    manager.set_value(QStringLiteral("licence/product_key"), QStringLiteral("AAAAA-BBBBB"));
    manager.set_value(QStringLiteral("machine/cpu_cores"), 6);
    const QString report = SettingsReport::render(manager, settings);
    check(!report.contains(QStringLiteral("hunter2")) && !report.contains(QStringLiteral("AAAAA-BBBBB")),
          "settings.txt holds neither the password nor the product key");
    check(report.contains(QStringLiteral("files/guest_password = (removed)")),
          "a removed value is said to be removed");
    check(report.contains(QStringLiteral("files/guest_user = someone")) &&
              report.contains(QStringLiteral("machine/cpu_cores = 6")),
          "other values are kept");
    check(report.contains(QStringLiteral("logs/manager = info")), "defaults are listed as well");
    check(report.contains(settings.logs_dir()), "the folders are listed");
}

void zip_test() {
    QTemporaryDir dir;
    for (bool deflate : {true, false}) {
        const QString out = dir.filePath(deflate ? QStringLiteral("deflated") : QStringLiteral("stored"));
        const QByteArray zip = ZipFixture::build(
            {{QStringLiteral("logs/agent.log"), QByteArray(5000, 'a')},
             {QStringLiteral("release.json"), QByteArray("{}")},
             {QStringLiteral("empty.txt"), QByteArray()}},
            deflate);
        write(dir.filePath(QStringLiteral("x.zip")), zip);
        QString error;
        check(lsw::host::platform::ZipExtractor::extract(dir.filePath(QStringLiteral("x.zip")), out, error),
              deflate ? "a deflated zip unpacks" : "a stored zip unpacks");
        QFile agent{out + QStringLiteral("/logs/agent.log")};
        check(agent.open(QIODevice::ReadOnly) && agent.readAll() == QByteArray(5000, 'a') &&
                  QFile::exists(out + QStringLiteral("/release.json")) &&
                  QFile::exists(out + QStringLiteral("/empty.txt")),
              "its folders and files are made");
    }
    QString error;
    const QByteArray slip = ZipFixture::build({{QStringLiteral("../escape.txt"), QByteArray("x")}});
    write(dir.filePath(QStringLiteral("slip.zip")), slip);
    check(!lsw::host::platform::ZipExtractor::extract(dir.filePath(QStringLiteral("slip.zip")),
                                                     dir.filePath(QStringLiteral("slipped/inner")), error) &&
              !QFile::exists(dir.filePath(QStringLiteral("slipped/escape.txt"))),
          "a path that climbs out of the folder is refused");
    QByteArray damaged = ZipFixture::build({{QStringLiteral("a.txt"), QByteArray(100, 'b')}}, false);
    damaged[35] = 'X';
    write(dir.filePath(QStringLiteral("bad.zip")), damaged);
    check(!lsw::host::platform::ZipExtractor::extract(dir.filePath(QStringLiteral("bad.zip")),
                                                     dir.filePath(QStringLiteral("bad")), error),
          "a damaged entry is refused");
    write(dir.filePath(QStringLiteral("junk.zip")), QByteArray("not a zip at all"));
    check(!lsw::host::platform::ZipExtractor::extract(dir.filePath(QStringLiteral("junk.zip")),
                                                     dir.filePath(QStringLiteral("junk")), error),
          "a file that is not a zip is refused");
}

void report_test() {
    SystemReport::Inputs inputs;
    inputs.domain_xml = QStringLiteral("<domain type='kvm'/>");
    inputs.shipped_release = QStringLiteral("abc123");
    const QString without = SystemReport::render(inputs);
    check(without.contains(QStringLiteral("Release shipped: abc123")) &&
              without.contains(QStringLiteral("Release in Windows: -")),
          "system.txt names the releases, and a dash for the unknown one");
    check(without.contains(QStringLiteral("Not collected: Windows did not answer")),
          "system.txt says Windows was not asked");
    check(without.contains(QStringLiteral("<domain type='kvm'/>")) &&
              without.contains(QStringLiteral("Qt: ")) && without.contains(QStringLiteral("libvirt: ")) &&
              without.contains(QStringLiteral("Session type: ")) &&
              without.contains(QStringLiteral("Distribution: ")),
          "system.txt has the definition, versions and session");
    inputs.gpus_known = true;
    lsw::host::platform::HostGpu gpu;
    gpu.name = QStringLiteral("Test GPU");
    gpu.vendor_id = 0x1002;
    gpu.device_id = 0x744c;
    gpu.vram_bytes = 16ULL << 30;
    gpu.driver = QStringLiteral("radv");
    gpu.driver_info = QStringLiteral("Mesa 25.2.3");
    inputs.gpus = {gpu};
    inputs.agent_online = true;
    inputs.agent_system_info = QStringLiteral("{\"os\": \"Windows 11\"}");
    inputs.agent_gpu_status = QStringLiteral("{\"ok\": true}");
    const QString with = SystemReport::render(inputs);
    check(with.contains(QStringLiteral("Test GPU")) && with.contains(QStringLiteral("1002")) &&
              with.contains(QStringLiteral("16384 MiB")) && with.contains(QStringLiteral("radv Mesa 25.2.3")),
          "system.txt lists the host GPUs with their drivers");
    check(with.contains(QStringLiteral("Windows 11")) && with.contains(QStringLiteral("\"ok\": true")),
          "and the agent's system_info and gpu_status");
}

void collector_test() {
    Settings settings;
    ManagerSettings manager;
    manager.set_value(QStringLiteral("files/guest_password"), QStringLiteral("hunter2"));
    (void)settings.ensure_directories();
    const QString logs = settings.logs_dir();
    QTemporaryDir libvirt;

    write(logs + QStringLiteral("/manager.log"), "manager line\n");
    write(logs + QStringLiteral("/manager.1.log"), "older manager line\n");
    write(logs + QStringLiteral("/mirrors/notepad.log"), "mirror line\n");
    write(logs + QStringLiteral("/vm/avgpu.log"), "avgpu line\n");
    write(libvirt.filePath(QStringLiteral("lsw.log")), "qemu line\n");
    QFile::link(libvirt.filePath(QStringLiteral("lsw.log")), logs + QStringLiteral("/vm/qemu.log"));
    QFile::link(libvirt.filePath(QStringLiteral("gone.log")), logs + QStringLiteral("/vm/virtiofsd.log"));
    write(logs + QStringLiteral("/crash-20260101-000000.log"), "crash\n");
    write(logs + QStringLiteral("/lsw-logs-20250101-000000.tar.gz"), "earlier archive");

    const QByteArray zip = ZipFixture::build({{QStringLiteral("logs/agent.log"), QByteArray("agent line\n")},
                                              {QStringLiteral("release.json"), QByteArray("{}")}});
    FakeAgent agent;
    agent.on(lsw::linkproto::cmd::kPackLogs, [&](const nlohmann::json&) {
        return nlohmann::json{{"path", "C:\\ProgramData\\LSW\\logs-1.zip"}, {"size", zip.size()}};
    });
    agent.on(lsw::linkproto::cmd::kReadFile, [&](const nlohmann::json& params) {
        const auto offset = static_cast<qsizetype>(params.value("offset", std::uint64_t{0}));
        const QByteArray slice = zip.mid(offset, 1 << 20);
        return nlohmann::json{{"data", slice.toBase64().toStdString()},
                              {"eof", offset + slice.size() >= zip.size()}};
    });
    agent.on(lsw::linkproto::cmd::kSystemInfo, [](const nlohmann::json&) {
        return nlohmann::json{{"os", "Windows 11 test"}};
    });
    agent.on(lsw::linkproto::cmd::kGpuStatus, [](const nlohmann::json&) {
        return nlohmann::json{{"adapters", nlohmann::json::array()}, {"marker", "gpu-status-ok"}};
    });

    AgentRpcClient rpc;
    rpc.set_target(1, agent.port());
    bool online = true;
    LogCollector collector{settings, manager, rpc, [&] {
                               SystemReport::Inputs inputs;
                               inputs.agent_online = online;
                               inputs.domain_xml = QStringLiteral("<domain name='lsw'/>");
                               inputs.shipped_release = QStringLiteral("ship1");
                               inputs.guest_release = QStringLiteral("guest1");
                               return inputs;
                           }};

    Run run = collect(collector);
    check(run.ok && QFile::exists(run.archive), "an archive is written");
    check(QFileInfo{run.archive}.dir().path() == logs &&
              QFileInfo{run.archive}.fileName().startsWith(QStringLiteral("lsw-logs-")) &&
              run.archive.endsWith(QStringLiteral(".tar.gz")),
          "it is lsw-logs-<time>.tar.gz in the logs folder");
    check(run.started == QVector<int>({LogCollector::WindowsLogs, LogCollector::HostLogs, LogCollector::SystemDetails, LogCollector::Archive}) && run.skipped.isEmpty(), "the four steps ran, none skipped");

    const QStringList names = tar_list(run.archive);
    for (const char* wanted : {"manager.log", "manager.1.log", "mirrors/notepad.log", "vm/avgpu.log",
                               "vm/qemu.log", "crash-20260101-000000.log", "guest/logs/agent.log",
                               "guest/release.json", "guest-logs.zip", "system.txt", "settings.txt",
                               "README.txt"}) {
        check(names.contains(QString::fromLatin1(wanted)), wanted);
    }
    check(tar_member(run.archive, QStringLiteral("vm/qemu.log")) == QByteArray("qemu line\n"),
          "a linked file holds what it points to");
    check(!names.contains(QStringLiteral("vm/virtiofsd.log")), "a link to nothing is left out");
    check(!names.contains(QStringLiteral("lsw-logs-20250101-000000.tar.gz")), "earlier archives are not nested");
    check(tar_member(run.archive, QStringLiteral("guest-logs.zip")) == zip, "the zip is in the archive as pulled");
    check(QFile::exists(logs + QStringLiteral("/guest/logs/agent.log")),
          "Windows' logs are unpacked in the logs folder for the Log page");

    const QString system = QString::fromUtf8(tar_member(run.archive, QStringLiteral("system.txt")));
    check(system.contains(QStringLiteral("Windows 11 test")) && system.contains(QStringLiteral("gpu-status-ok")) &&
              system.contains(QStringLiteral("<domain name='lsw'/>")) &&
              system.contains(QStringLiteral("Release in Windows: guest1")),
          "system.txt has the agent's answers and the definition");
    const QString settings_txt = QString::fromUtf8(tar_member(run.archive, QStringLiteral("settings.txt")));
    check(!settings_txt.contains(QStringLiteral("hunter2")) && settings_txt.contains(QStringLiteral("(removed)")),
          "settings.txt in the archive has no password");
    const QString readme = QString::fromUtf8(tar_member(run.archive, QStringLiteral("README.txt")));
    check(readme.contains(QStringLiteral("window titles"), Qt::CaseInsensitive) &&
              readme.contains(QStringLiteral("read it before you share"), Qt::CaseInsensitive),
          "README.txt warns about titles and file names");
    bool deleted = false;
    for (const auto& request : agent.requests()) {
        deleted = deleted || (request.first == "read_file" && request.second.value("delete_at_eof", false));
    }
    check(deleted, "Windows is told to delete its zip once read");
    check(!collector.running() && collector.archive() == run.archive, "the collector is idle and remembers the archive");

    QFile::remove(logs + QStringLiteral("/guest/logs/agent.log"));
    online = false;
    QFile::remove(run.archive);
    QThread::sleep(std::chrono::seconds(1));
    run = collect(collector);
    check(run.ok, "without an agent an archive is still written");
    check(run.skipped.size() == 1 && run.skipped[0].first == LogCollector::WindowsLogs,
          "the Windows step is skipped");
    const QStringList offline_names = tar_list(run.archive);
    check(!offline_names.contains(QStringLiteral("guest-logs.zip")), "and no zip is in the archive");
    check(QString::fromUtf8(tar_member(run.archive, QStringLiteral("system.txt")))
              .contains(QStringLiteral("Not collected: Windows did not answer")),
          "system.txt says why");
    check(collector.windows_note() == QStringLiteral("Windows did not answer"), "the collector keeps the note");

    online = true;
    agent.on(lsw::linkproto::cmd::kPackLogs, [](const nlohmann::json&) {
        return nlohmann::json{{"ok", false}, {"error", "disk full"}};
    });
    QFile::remove(run.archive);
    QThread::sleep(std::chrono::seconds(1));
    run = collect(collector);
    check(run.ok && run.skipped.size() == 1 && run.skipped[0].second.contains(QStringLiteral("disk full")),
          "an agent that cannot pack skips the step with its reason");
    check(QString::fromUtf8(tar_member(run.archive, QStringLiteral("system.txt"))).contains(QStringLiteral("disk full")),
          "and system.txt carries it");
}

}

int main(int argc, char** argv) {
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("XDG_CACHE_HOME", home.path().toUtf8());
    QCoreApplication app(argc, argv);

    settings_test();
    zip_test();
    report_test();
    collector_test();
    return g_failures == 0 ? 0 : 1;
}
