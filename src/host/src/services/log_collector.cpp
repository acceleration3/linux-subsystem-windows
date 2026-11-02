#include "lsw/host/services/log_collector.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_guest_files.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/platform/host_gpus.hpp"
#include "lsw/host/platform/zip_extractor.hpp"
#include "lsw/host/services/settings_report.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

namespace {

constexpr int kTarTimeoutMs = 10 * 60 * 1000;
constexpr const char* kGuestZip = "guest-logs.zip";
constexpr const char* kGuestFolder = "guest";
constexpr const char* kDriverLogs = "gpu-drivers.log";

void merge_driver_logs(const QString& guest_folder) {
    const QDir av{QDir{guest_folder}.filePath(QStringLiteral("av"))};
    const QFileInfoList files = av.entryInfoList({QStringLiteral("*.log")}, QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        return;
    }
    QFile out{QDir{guest_folder}.filePath(QString::fromLatin1(kDriverLogs))};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LSW_WARN("collect logs: could not write {}: {}", out.fileName().toStdString(),
                 out.errorString().toStdString());
        return;
    }
    for (const QFileInfo& info : files) {
        QFile in{info.filePath()};
        if (!in.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray prefix = "[" + info.completeBaseName().toUtf8() + "] ";
        while (!in.atEnd()) {
            QByteArray line = in.readLine();
            if (!line.endsWith('\n')) {
                line.append('\n');
            }
            out.write(prefix + line);
        }
    }
}
constexpr int kCrashLogDays = 30;

constexpr const char* kReadme =
    "This archive was made by LSW's Collect logs.\n"
    "\n"
    "It holds the logs of the manager, the window mirrors, the GPU driver, QEMU and virtiofsd; "
    "Windows' logs (guest-logs.zip, unpacked in guest/); system.txt, with versions, the GPUs "
    "and the VM's definition; and settings.txt, LSW's settings without passwords and keys. "
    "Window titles and file names appear in logs, so read it before you share it.\n";

QStringList host_files(const QString& logs_dir) {
    QStringList files;
    const QDir root{logs_dir};
    const QDateTime crash_cutoff = QDateTime::currentDateTime().addDays(-kCrashLogDays);
    QDirIterator it{logs_dir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks};
    while (it.hasNext()) {
        const QString relative = root.relativeFilePath(it.next());
        const bool old_crash = !relative.contains(QLatin1Char('/')) &&
                               relative.startsWith(QLatin1String("crash-")) &&
                               relative.endsWith(QLatin1String(".log")) &&
                               it.fileInfo().lastModified() < crash_cutoff;
        if (old_crash) {
            continue;
        }
        const bool earlier_archive = !relative.contains(QLatin1Char('/')) &&
                                     relative.startsWith(QLatin1String("lsw-logs-")) &&
                                     relative.contains(QLatin1String(".tar.gz"));
        if (!earlier_archive) {
            files << relative;
        }
    }
    files.sort();
    return files;
}

bool write_text(const QString& path, const QString& text) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) >= 0;
}

}

struct LogCollector::Job {
    QString logs_dir;
    SystemReport::Inputs inputs;
    QString settings_text;
};

struct LogCollector::Outcome {
    bool ok = false;
    QString path;
    QString error;
    QString windows_note;
};

QString LogCollector::step_text(int step) {
    switch (step) {
    case HostLogs:
        return QStringLiteral("Copying the host logs");
    case WindowsLogs:
        return QStringLiteral("Collecting Windows' logs");
    case SystemDetails:
        return QStringLiteral("Writing the system details");
    case Archive:
        return QStringLiteral("Writing the archive");
    default:
        return {};
    }
}

LogCollector::LogCollector(config::Settings& settings, config::ManagerSettings& manager_settings,
                           link::AgentRpcClient& rpc, InputsFn inputs, QObject* parent)
    : QObject(parent), settings_(settings), manager_settings_(manager_settings), rpc_(rpc),
      inputs_(std::move(inputs)) {}

LogCollector::~LogCollector() {
    future_.waitForFinished();
}

void LogCollector::start() {
    if (running_) {
        return;
    }
    running_ = true;
    archive_.clear();
    error_.clear();
    windows_note_.clear();

    Job job;
    job.logs_dir = settings_.logs_dir();
    job.inputs = inputs_();
    job.settings_text = SettingsReport::render(manager_settings_, settings_);

    future_ = QtConcurrent::run([this, job] {
        const Outcome outcome = work(this, job);
        QMetaObject::invokeMethod(this, [this, outcome] { conclude(outcome); },
                                  Qt::QueuedConnection);
    });
}

void LogCollector::conclude(const Outcome& outcome) {
    running_ = false;
    windows_note_ = outcome.windows_note;
    if (outcome.ok) {
        archive_ = outcome.path;
        LSW_INFO("collect logs: wrote {}", outcome.path.toStdString());
        Q_EMIT finished(outcome.path);
        return;
    }
    error_ = outcome.error;
    LSW_WARN("collect logs: {}", outcome.error.toStdString());
    Q_EMIT failed(outcome.error);
}

LogCollector::Outcome LogCollector::work(LogCollector* self, const Job& job) {
    Outcome outcome;
    const auto started = [self](int step) {
        QMetaObject::invokeMethod(self, [self, step] { Q_EMIT self->step_started(step); },
                                  Qt::QueuedConnection);
    };
    const auto skipped = [self](int step, const QString& why) {
        QMetaObject::invokeMethod(self, [self, step, why] { Q_EMIT self->step_skipped(step, why); },
                                  Qt::QueuedConnection);
    };

    QDir{}.mkpath(job.logs_dir);
    QTemporaryDir staging;
    if (!staging.isValid()) {
        outcome.error = QStringLiteral("Cannot make a working folder: %1").arg(staging.errorString());
        return outcome;
    }

    started(WindowsLogs);
    const QString guest_folder = QDir{job.logs_dir}.filePath(QString::fromLatin1(kGuestFolder));
    QDir{guest_folder}.removeRecursively();
    SystemReport::Inputs inputs = job.inputs;
    bool have_zip = false;
    if (!inputs.agent_online) {
        inputs.agent_note = QStringLiteral("Windows did not answer");
    } else {
        link::AgentGuestFiles guest{self->rpc_};
        const auto packed = guest.pack_logs();
        std::string error;
        const QString zip = QDir{staging.path()}.filePath(QString::fromLatin1(kGuestZip));
        if (!packed.ok) {
            inputs.agent_note = QStringLiteral("Windows could not pack its logs: %1")
                                    .arg(QString::fromStdString(packed.error));
        } else if (!guest.read_to_file(packed.path, zip, error, true)) {
            inputs.agent_note = QStringLiteral("Windows' logs could not be read: %1")
                                    .arg(QString::fromStdString(error));
        } else {
            QString unzip_error;
            if (!platform::ZipExtractor::extract(zip, guest_folder, unzip_error)) {
                inputs.agent_note = QStringLiteral("Windows' logs could not be unpacked: %1")
                                        .arg(unzip_error);
                QDir{guest_folder}.removeRecursively();
            } else {
                merge_driver_logs(guest_folder);
            }
            have_zip = true;
        }
    }
    outcome.windows_note = inputs.agent_note;
    if (!inputs.agent_note.isEmpty()) {
        skipped(WindowsLogs, inputs.agent_note);
    }

    started(HostLogs);
    const QStringList files = host_files(job.logs_dir);
    LSW_DEBUG("collect logs: {} host files, Windows' logs {}", files.size(),
              have_zip ? "included" : "left out");

    started(SystemDetails);
    inputs.gpus = platform::HostGpus::enumerate();
    inputs.gpus_known = true;
    if (inputs.agent_online && inputs.agent_note.isEmpty()) {
        const auto info = self->rpc_.request(linkproto::cmd::kSystemInfo);
        const auto gpu = self->rpc_.request(linkproto::cmd::kGpuStatus);
        inputs.agent_system_info = info.ok ? QString::fromStdString(info.body.dump(2))
                                           : QStringLiteral("failed: %1").arg(QString::fromStdString(info.error));
        inputs.agent_gpu_status = gpu.ok ? QString::fromStdString(gpu.body.dump(2))
                                         : QStringLiteral("failed: %1").arg(QString::fromStdString(gpu.error));
    }
    const QDir stage{staging.path()};
    if (!write_text(stage.filePath(QStringLiteral("system.txt")), SystemReport::render(inputs)) ||
        !write_text(stage.filePath(QStringLiteral("settings.txt")), job.settings_text) ||
        !write_text(stage.filePath(QStringLiteral("README.txt")), QString::fromLatin1(kReadme))) {
        outcome.error = QStringLiteral("Cannot write in the working folder %1").arg(staging.path());
        return outcome;
    }

    started(Archive);
    const QString list_path = stage.filePath(QStringLiteral("files.list"));
    if (!write_text(list_path, files.join(QLatin1Char('\n')) + QLatin1Char('\n'))) {
        outcome.error = QStringLiteral("Cannot write in the working folder %1").arg(staging.path());
        return outcome;
    }
    const QString name = QStringLiteral("lsw-logs-%1.tar.gz")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString final_path = QDir{job.logs_dir}.filePath(name);
    const QString part_path = final_path + QStringLiteral(".part");

    QStringList args{QStringLiteral("-czh"), QStringLiteral("-f"), part_path,
                     QStringLiteral("-C"), job.logs_dir, QStringLiteral("-T"), list_path,
                     QStringLiteral("-C"), staging.path(), QStringLiteral("system.txt"),
                     QStringLiteral("settings.txt"), QStringLiteral("README.txt")};
    if (have_zip) {
        args << QString::fromLatin1(kGuestZip);
    }
    QProcess tar;
    tar.start(QStringLiteral("tar"), args);
    if (!tar.waitForStarted(5000)) {
        outcome.error = QStringLiteral("tar would not start: %1").arg(tar.errorString());
        return outcome;
    }
    if (!tar.waitForFinished(kTarTimeoutMs)) {
        tar.kill();
        outcome.error = QStringLiteral("tar did not finish");
        QFile::remove(part_path);
        return outcome;
    }
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() > 1) {
        const QString detail = QString::fromLocal8Bit(tar.readAllStandardError()).trimmed();
        outcome.error = QStringLiteral("tar failed: %1")
                            .arg(detail.isEmpty() ? QStringLiteral("exit code %1").arg(tar.exitCode()) : detail);
        QFile::remove(part_path);
        return outcome;
    }
    QFile::remove(final_path);
    if (!QFile::rename(part_path, final_path)) {
        outcome.error = QStringLiteral("Cannot write %1").arg(final_path);
        QFile::remove(part_path);
        return outcome;
    }
    outcome.ok = true;
    outcome.path = final_path;
    return outcome;
}

}
