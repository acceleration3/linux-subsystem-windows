#include "lsw/host/platform/process_threads.hpp"

#include <QFile>
#include <QList>

#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <sys/resource.h>
#include <unistd.h>

namespace lsw::host::platform {

namespace {

constexpr qint64 kMaxFile = 16 * 1024;
constexpr int kNiceField = 16;
constexpr int kStartField = 19;
constexpr int kNiceCeiling = 20;

QByteArray slurp(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(kMaxFile) : QByteArray();
}

qint64 uptime_ms() {
    timespec now{};
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0) {
        return 0;
    }
    return static_cast<qint64>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

}

std::optional<ProcProcessThreads::Schedule> ProcProcessThreads::parse_stat(const QByteArray& stat) {
    const qsizetype name_end = stat.lastIndexOf(')');
    if (name_end < 0) {
        return std::nullopt;
    }
    const QList<QByteArray> fields = stat.mid(name_end + 1).simplified().split(' ');
    if (fields.size() <= kStartField) {
        return std::nullopt;
    }
    bool nice_ok = false;
    bool start_ok = false;
    Schedule schedule;
    schedule.nice = fields.at(kNiceField).toInt(&nice_ok);
    schedule.start_ticks = fields.at(kStartField).toLongLong(&start_ok);
    return nice_ok && start_ok ? std::optional<Schedule>{schedule} : std::nullopt;
}

int ProcProcessThreads::lowest_nice_of(const QByteArray& limits) {
    for (const QByteArray& line : limits.split('\n')) {
        if (!line.startsWith("Max nice priority")) {
            continue;
        }
        const QList<QByteArray> fields = line.mid(17).simplified().split(' ');
        bool ok = false;
        const int soft = fields.value(0).toInt(&ok);
        if (!ok) {
            return fields.value(0) == "unlimited" ? -kNiceCeiling : 0;
        }
        return std::clamp(kNiceCeiling - soft, -kNiceCeiling, 0);
    }
    return 0;
}

QVector<qint64> ProcProcessThreads::list(qint64 pid) {
    QVector<qint64> out;
    DIR* dir = opendir(QStringLiteral("/proc/%1/task").arg(pid).toLocal8Bit().constData());
    if (dir == nullptr) {
        return out;
    }
    while (const dirent* entry = readdir(dir)) {
        bool ok = false;
        const qint64 tid = QByteArray(entry->d_name).toLongLong(&ok);
        if (ok && tid > 0) {
            out.push_back(tid);
        }
    }
    closedir(dir);
    return out;
}

std::optional<IProcessThreads::Thread> ProcProcessThreads::read(qint64 pid, qint64 tid) {
    const QString base = QStringLiteral("/proc/%1/task/%2/").arg(pid).arg(tid);
    const QByteArray name = slurp(base + QStringLiteral("comm"));
    const auto schedule = parse_stat(slurp(base + QStringLiteral("stat")));
    const long ticks = sysconf(_SC_CLK_TCK);
    if (name.isEmpty() || !schedule || ticks <= 0) {
        return std::nullopt;
    }
    Thread thread;
    thread.name = QString::fromUtf8(name.endsWith('\n') ? name.chopped(1) : name);
    thread.nice = schedule->nice;
    thread.age_ms = std::max<qint64>(0, uptime_ms() - schedule->start_ticks * 1000 / ticks);
    return thread;
}

int ProcProcessThreads::lowest_nice(qint64 pid) {
    return lowest_nice_of(slurp(QStringLiteral("/proc/%1/limits").arg(pid)));
}

bool ProcProcessThreads::set_nice(qint64 tid, int nice) {
    return setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice) == 0;
}

}
