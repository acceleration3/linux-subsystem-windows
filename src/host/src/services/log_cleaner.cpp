#include "lsw/host/services/log_cleaner.hpp"

#include "lsw/base/log.hpp"
#include "lsw/base/log_tail.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/link/agent_guest_files.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/mirror/mirror_log.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>

namespace lsw::host::services {

namespace {

bool is_rolled(const QString& name) {
    static const QRegularExpression rolled{QStringLiteral(R"(\.\d+\.log$)")};
    return rolled.match(name).hasMatch();
}

}

struct LogCleaner::Windows {
    bool reached = false;
    QString note;
    int removed = 0;
    int emptied = 0;
    QStringList skipped;
};

bool LogCleaner::Result::complete() const noexcept {
    return failed.isEmpty() && windows_cleared && windows_skipped.isEmpty();
}

QString LogCleaner::Result::headline() const {
    return complete() ? QStringLiteral("Logs cleared.") : QStringLiteral("Logs cleared, with exceptions.");
}

QString LogCleaner::Result::detail() const {
    QStringList parts;
    if (!failed.isEmpty()) {
        parts << QStringLiteral("%1 could not be cleared on this computer: %2.")
                     .arg(failed.size() == 1 ? QStringLiteral("One file")
                                             : QStringLiteral("%1 files").arg(failed.size()),
                          failed.join(QStringLiteral(", ")));
    }
    if (!windows_cleared) {
        parts << QStringLiteral("Windows' logs were not cleared: %1.").arg(windows_note);
    } else if (!windows_skipped.isEmpty()) {
        parts << QStringLiteral("%1 in Windows could not be cleared.")
                     .arg(windows_skipped.size() == 1 ? QStringLiteral("One file")
                                                      : QStringLiteral("%1 files").arg(windows_skipped.size()));
    }
    return parts.join(QLatin1Char(' '));
}

LogCleaner::LogCleaner(config::Settings& settings, link::AgentRpcClient& rpc, apps::LogStore& store,
                       Probe agent_online, Probe vm_running, QObject* parent)
    : QObject(parent), settings_(settings), rpc_(rpc), store_(store),
      agent_online_(std::move(agent_online)), vm_running_(std::move(vm_running)) {}

LogCleaner::~LogCleaner() {
    future_.waitForFinished();
}

void LogCleaner::start() {
    if (running_) {
        return;
    }
    running_ = true;
    result_ = Result{};
    Q_EMIT started();
    clear_host();

    if (!agent_online_()) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                Windows windows;
                windows.note = QStringLiteral("Windows did not answer");
                conclude(windows);
            },
            Qt::QueuedConnection);
        return;
    }
    future_ = QtConcurrent::run([this] {
        Windows windows;
        link::AgentGuestFiles guest{rpc_};
        const auto cleared = guest.clear_logs();
        if (!cleared.ok) {
            windows.note = QString::fromStdString(cleared.error);
            if (windows.note.isEmpty()) {
                windows.note = QStringLiteral("Windows did not answer");
            }
        } else {
            windows.reached = true;
            windows.removed = cleared.deleted;
            windows.emptied = cleared.emptied;
            for (const auto& entry : cleared.skipped) {
                windows.skipped << QString::fromStdString(entry);
            }
        }
        QMetaObject::invokeMethod(this, [this, windows] { conclude(windows); }, Qt::QueuedConnection);
    });
}

void LogCleaner::clear_host() {
    const QString logs = settings_.logs_dir();
    const QDir root{logs};

    QSet<QString> held;
    const QString manager_log = QFileInfo{settings_.manager_log_path()}.absoluteFilePath();
    if (base::Logger::truncate()) {
        held.insert(manager_log);
        ++result_.emptied;
    }
    for (const QString& path : mirror::MirrorLog::truncate_all()) {
        held.insert(QFileInfo{path}.absoluteFilePath());
        ++result_.emptied;
    }

    const QString avgpu = QFileInfo{settings_.vm_avgpu_log_path()}.absoluteFilePath();
    const bool vm_running = vm_running_();

    const auto remove = [this, &root](const QString& path) {
        if (QFile::remove(path)) {
            ++result_.removed;
        } else {
            result_.failed << root.relativeFilePath(path);
        }
    };
    const auto empty = [this, &root](const QString& path) {
        QFile file{path};
        if (file.resize(0)) {
            ++result_.emptied;
        } else {
            result_.failed << root.relativeFilePath(path);
        }
    };

    QDirIterator it{logs, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories};
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString relative = root.relativeFilePath(path);
        if (info.isSymLink() || relative.startsWith(QStringLiteral("guest/"))) {
            continue;
        }
        const bool top = !relative.contains(QLatin1Char('/'));
        const QString absolute = info.absoluteFilePath();
        if (is_rolled(info.fileName())) {
            remove(path);
        } else if (top && info.fileName().startsWith(QStringLiteral("crash-")) &&
                   info.fileName().endsWith(QStringLiteral(".log"))) {
            remove(path);
        } else if (absolute == manager_log) {
            if (!held.contains(absolute)) {
                remove(path);
            }
        } else if (absolute == avgpu) {
            if (vm_running) {
                empty(path);
            } else {
                remove(path);
            }
        } else if (relative.startsWith(QStringLiteral("mirrors/")) &&
                   info.fileName().endsWith(QStringLiteral(".log")) && !held.contains(absolute)) {
            remove(path);
        }
    }

    const QString guest_dir = root.filePath(QStringLiteral("guest"));
    if (QFileInfo::exists(guest_dir)) {
        QDirIterator files{guest_dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories};
        int count = 0;
        while (files.hasNext()) {
            files.next();
            ++count;
        }
        if (QDir{guest_dir}.removeRecursively()) {
            result_.removed += count;
        } else {
            result_.failed << QStringLiteral("guest");
        }
    }
}

void LogCleaner::conclude(const Windows& windows) {
    result_.windows_cleared = windows.reached;
    result_.windows_note = windows.note;
    result_.windows_removed = windows.removed;
    result_.windows_emptied = windows.emptied;
    result_.windows_skipped = windows.skipped;
    store_.clear();
    base::LogTail::instance().clear();
    running_ = false;
    LSW_INFO("clear logs: {} files removed, {} emptied; Windows: {}", result_.removed, result_.emptied,
             windows.reached ? "cleared" : windows.note.toStdString());
    Q_EMIT finished();
}

}
