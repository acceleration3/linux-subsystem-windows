#include "lsw/host/manager/log_file_tail.hpp"

#include <QFile>

#include <sys/stat.h>

namespace lsw::host::manager {

namespace {

bool stat_of(const QString& path, struct stat& out) {
    return !path.isEmpty() && ::stat(QFile::encodeName(path).constData(), &out) == 0;
}

QStringList split_lines(QByteArray data, QByteArray& partial) {
    data.prepend(partial);
    const qsizetype end = data.lastIndexOf('\n');
    if (end < 0) {
        partial = data;
        return {};
    }
    partial = data.mid(end + 1);
    QStringList lines = QString::fromUtf8(data.left(end)).split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    return lines;
}

}

LogFileTail::LogFileTail(QObject* parent) : QObject(parent) {
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, &LogFileTail::poll);
}

void LogFileTail::forget() {
    lines_.clear();
    partial_.clear();
    pos_ = 0;
    exists_ = false;
    loaded_ = false;
    device_ = 0;
    inode_ = 0;
}

void LogFileTail::follow(const QString& path) {
    path_ = path;
    forget();
    if (active_) {
        reload();
    }
}

void LogFileTail::set_active(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (!active_) {
        timer_.stop();
        return;
    }
    timer_.start();
    poll();
}

void LogFileTail::reload() {
    forget();
    struct stat info{};
    if (!stat_of(path_, info)) {
        return;
    }
    QFile file{path_};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    exists_ = true;
    loaded_ = true;
    device_ = info.st_dev;
    inode_ = info.st_ino;
    const qint64 size = file.size();
    const qint64 start = qMax<qint64>(0, size - kTailBytes);
    file.seek(start);
    QStringList lines = split_lines(file.read(size - start), partial_);
    if (start > 0 && !lines.isEmpty()) {
        lines.removeFirst();
    }
    while (lines.size() > kTailLines) {
        lines.removeFirst();
    }
    lines_ = lines;
    pos_ = size;
}

void LogFileTail::poll() {
    if (!active_) {
        return;
    }
    struct stat info{};
    if (!stat_of(path_, info)) {
        if (exists_) {
            forget();
            Q_EMIT replaced();
        }
        return;
    }
    if (!loaded_ || info.st_dev != device_ || info.st_ino != inode_ || info.st_size < pos_ ||
        info.st_size - pos_ > kCatchUpBytes) {
        reload();
        Q_EMIT replaced();
        return;
    }
    if (info.st_size == pos_) {
        return;
    }
    QFile file{path_};
    if (!file.open(QIODevice::ReadOnly) || !file.seek(pos_)) {
        return;
    }
    const QByteArray data = file.read(info.st_size - pos_);
    pos_ += data.size();
    const QStringList added = split_lines(data, partial_);
    if (added.isEmpty()) {
        return;
    }
    lines_ += added;
    while (lines_.size() > kMaxLines) {
        lines_.removeFirst();
    }
    Q_EMIT appended(added);
}

}
