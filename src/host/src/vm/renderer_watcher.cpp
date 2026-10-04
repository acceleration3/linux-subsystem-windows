#include "lsw/host/vm/renderer_watcher.hpp"

#include "lsw/base/log.hpp"

#include <QDir>
#include <QFile>

namespace lsw::host::vm {

namespace {

// Every numeric entry under /proc.
QStringList process_ids() {
    QStringList out;
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        bool numeric = false;
        (void)entry.toLongLong(&numeric);
        if (numeric) {
            out << entry;
        }
    }
    return out;
}

QByteArray read_all(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// The command name and parent pid from /proc/<pid>/stat. The name is in
// parentheses and may hold spaces or parentheses itself, so the fields
// after it are found from the last ')'.
bool read_stat(const QString& pid, QByteArray* comm, qint64* ppid) {
    const QByteArray stat = read_all(QStringLiteral("/proc/%1/stat").arg(pid));
    const qsizetype open = stat.indexOf('(');
    const qsizetype close = stat.lastIndexOf(')');
    if (open < 0 || close <= open) {
        return false;
    }
    *comm = stat.mid(open + 1, close - open - 1);
    const QList<QByteArray> rest = stat.mid(close + 2).split(' ');
    if (rest.size() < 2) {
        return false;
    }
    *ppid = rest.at(1).toLongLong();
    return true;
}

}  // namespace

RendererWatcher::Sample RendererWatcher::scan(const QUuid& uuid, qint64 known_qemu) {
    Sample out;
    if (uuid.isNull()) {
        return out;
    }
    out.qemu_pid = known_qemu != 0 && QFile::exists(QStringLiteral("/proc/%1").arg(known_qemu))
                       ? known_qemu
                       : 0;
    if (out.qemu_pid == 0) {
        const QByteArray wanted = uuid.toString(QUuid::WithoutBraces).toLatin1();
        for (const QString& pid : process_ids()) {
            // Arguments are NUL-separated; "-uuid" then the VM's uuid.
            const QList<QByteArray> args =
                read_all(QStringLiteral("/proc/%1/cmdline").arg(pid)).split('\0');
            for (qsizetype i = 0; i + 1 < args.size() && out.qemu_pid == 0; ++i) {
                if (args.at(i) == "-uuid"
                    && args.at(i + 1).compare(wanted, Qt::CaseInsensitive) == 0) {
                    out.qemu_pid = pid.toLongLong();
                }
            }
            if (out.qemu_pid != 0) {
                break;
            }
        }
    }
    if (out.qemu_pid == 0) {
        return out;
    }
    for (const QString& pid : process_ids()) {
        QByteArray comm;
        qint64 ppid = 0;
        if (read_stat(pid, &comm, &ppid) && ppid == out.qemu_pid && comm == "av-renderer") {
            out.renderer_pid = pid.toLongLong();
            break;
        }
    }
    return out;
}

void RendererWatcher::reset() {
    *this = RendererWatcher{};
}

bool RendererWatcher::update(const Sample& sample) {
    if (sample.qemu_pid != qemu_pid_) {
        // A new QEMU is a new boot, not a crash: its first renderer is
        // not a restart.
        previous_renderer_ = 0;
        restarts_ = 0;
        last_restart_ = {};
    }
    qemu_pid_ = sample.qemu_pid;
    renderer_pid_ = sample.renderer_pid;
    bool restarted = false;
    if (renderer_pid_ != 0) {
        if (previous_renderer_ != 0 && renderer_pid_ != previous_renderer_) {
            ++restarts_;
            last_restart_ = QDateTime::currentDateTime();
            restarted = true;
            LSW_WARN("av-renderer {} replaced {}: the host graphics driver crashed",
                     renderer_pid_, previous_renderer_);
        }
        previous_renderer_ = renderer_pid_;
    }
    return restarted;
}

}  // namespace lsw::host::vm
