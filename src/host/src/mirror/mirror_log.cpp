#include "lsw/host/mirror/mirror_log.hpp"

#include "lsw/base/log_format.hpp"
#include "lsw/host/apps/log_store.hpp"

#include <QDateTime>
#include <QDir>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace lsw::host::mirror {

namespace {

std::mutex g_directory_mutex;
QString g_directory;
std::mutex g_live_mutex;
std::vector<MirrorLog*> g_live;

QString file_name_of(const QString& name) {
    QString out;
    out.reserve(name.size());
    for (const QChar c : name.left(80)) {
        out += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')
                || c == QLatin1Char('.') || c == QLatin1Char(' '))
                   ? c
                   : QLatin1Char('_');
    }
    out = out.trimmed();
    while (out.startsWith(QLatin1Char('.'))) {
        out.remove(0, 1);
    }
    return out.isEmpty() ? QStringLiteral("mirror") : out;
}

bool has_wall_clock(std::string_view line) {
    return line.size() > 24 && line[4] == '-' && line[7] == '-' && line[10] == ' '
           && line[13] == ':' && line[16] == ':' && line[19] == '.' && line[23] == ' ';
}

std::string_view trim_left(std::string_view s) {
    while (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1);
    }
    return s;
}

std::string_view next_token(std::string_view& s) {
    s = trim_left(s);
    const auto end = s.find(' ');
    const std::string_view token = s.substr(0, end);
    s.remove_prefix(end == std::string_view::npos ? s.size() : end);
    return token;
}

}

MirrorLog::MirrorLog(const QString& name)
    : name_(file_name_of(name)), source_(QStringLiteral("mirror ") + name_) {
    const std::lock_guard lock{g_live_mutex};
    g_live.push_back(this);
}

MirrorLog::~MirrorLog() {
    const std::lock_guard lock{g_live_mutex};
    g_live.erase(std::remove(g_live.begin(), g_live.end(), this), g_live.end());
}

QStringList MirrorLog::truncate_all() {
    QStringList emptied;
    const std::lock_guard lock{g_live_mutex};
    for (MirrorLog* log : g_live) {
        if (log->file_ && log->file_->truncate()) {
            emptied << QString::fromStdString(log->file_->path().string());
        }
    }
    return emptied;
}

void MirrorLog::set_directory(const QString& directory) {
    const std::lock_guard lock{g_directory_mutex};
    g_directory = directory;
}

MirrorLog::Parsed MirrorLog::parse(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    Parsed whole;
    whole.message = line;

    std::string_view rest = line;
    if (has_wall_clock(rest)) {
        rest.remove_prefix(24);
    }
    if (rest.empty() || rest.front() != '[') {
        return whole;
    }
    const auto close = rest.find(']');
    if (close == std::string_view::npos) {
        return whole;
    }
    rest.remove_prefix(close + 1);
    const std::string_view level_word = next_token(rest);
    constexpr int kUnknown = -1;
    const int level = lsw::base::LogFormat::level_from_name(level_word, kUnknown);
    if (level == kUnknown) {
        return whole;
    }
    Parsed parsed;
    parsed.level = level;
    parsed.message = trim_left(rest);
    const auto bar = rest.find(" | ");
    if (bar != std::string_view::npos) {
        std::string_view head = rest.substr(0, bar);
        parsed.tag = next_token(head);
        parsed.message = rest.substr(bar + 3);
    }
    return parsed;
}

void MirrorLog::roll_for_start() {
    QString directory;
    {
        const std::lock_guard lock{g_directory_mutex};
        directory = g_directory;
    }
    if (directory.isEmpty()) {
        return;
    }
    if (!file_) {
        file_ = std::make_unique<lsw::base::RollingFile>(
            QDir(directory).filePath(name_ + QStringLiteral(".log")).toStdString(),
            lsw::base::RollingFile::kDefaultLimits);
    }
    file_->roll_now();
}

void MirrorLog::consume(const QByteArray& bytes) {
    partial_ += bytes;
    qsizetype start = 0;
    for (qsizetype end; (end = partial_.indexOf('\n', start)) >= 0; start = end + 1) {
        handle_line(std::string_view{partial_.constData() + start,
                                     static_cast<std::size_t>(end - start + 1)});
    }
    partial_.remove(0, start);
}

void MirrorLog::finish() {
    if (!partial_.isEmpty()) {
        partial_ += '\n';
        handle_line(std::string_view{partial_.constData(), static_cast<std::size_t>(partial_.size())});
        partial_.clear();
    }
}

void MirrorLog::handle_line(std::string_view line) {
    if (file_) {
        file_->append(line);
    } else {
        std::fwrite(line.data(), 1, line.size(), stderr);
    }
    if (auto* store = apps::LogStore::process_store()) {
        const Parsed parsed = parse(line);
        apps::LogRecord record;
        record.ts_ms = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        record.level = QString::fromLatin1(lsw::base::LogFormat::level_name(parsed.level)).toUpper();
        record.source = source_;
        record.msg = parsed.tag.empty()
                         ? QString::fromUtf8(parsed.message.data(), static_cast<qsizetype>(parsed.message.size()))
                         : QString::fromUtf8(parsed.tag.data(), static_cast<qsizetype>(parsed.tag.size()))
                               + QStringLiteral(": ")
                               + QString::fromUtf8(parsed.message.data(), static_cast<qsizetype>(parsed.message.size()));
        store->post(std::move(record));
    }
}

}
