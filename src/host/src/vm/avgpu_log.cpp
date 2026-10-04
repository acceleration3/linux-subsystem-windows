#include "lsw/host/vm/avgpu_log.hpp"

#include <QFile>
#include <QRegularExpression>

namespace lsw::host::vm {

namespace {

// "<what> status 0x0000000N" as the driver's log_status writes it.
std::optional<quint32> status_of(const QString& line, QLatin1StringView what) {
    const QString prefix = QStringLiteral("avgpu: %1 status 0x").arg(what);
    if (!line.startsWith(prefix)) {
        return std::nullopt;
    }
    bool ok = false;
    const quint32 value = line.mid(prefix.size()).trimmed().toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

void AvgpuLog::reset() {
    offset_ = 0;
    partial_.clear();
    lines_.clear();
    line_count_ = 0;
    facts_ = Facts{};
}

void AvgpuLog::read_fact(const QString& line, Facts& facts) {
    if (const auto window = status_of(line, QLatin1StringView("host window"))) {
        facts.host_window_taken = *window == 0;
    } else if (const auto ums = status_of(line, QLatin1StringView("user-mode submission"))) {
        facts.user_mode_submission = *ums != 0;
    } else if (const auto fences = status_of(line, QLatin1StringView("native fences"))) {
        facts.native_fences = *fences != 0;
    } else if (line.startsWith(QLatin1StringView("avgpu: the host offers"))) {
        static const QRegularExpression offers(
            QStringLiteral("offers (\\d+) modes, (\\d+) taken, preferred (\\d+x\\d+)"));
        const auto match = offers.match(line);
        if (match.hasMatch()) {
            facts.modes_offered = match.captured(1).toInt();
            facts.modes_taken = match.captured(2).toInt();
            facts.preferred_mode = match.captured(3);
        }
    }
}

bool AvgpuLog::poll() {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
        if (offset_ != 0 || line_count_ != 0) {
            reset();
            return true;
        }
        return false;
    }
    const qint64 size = file.size();
    bool changed = false;
    if (size < offset_) {
        // Truncated: QEMU started again, and this is a new run.
        reset();
        changed = true;
    }
    if (size == offset_) {
        return changed;
    }
    file.seek(offset_);
    QByteArray chunk = partial_ + file.readAll();
    offset_ = size;
    const qsizetype last_newline = chunk.lastIndexOf('\n');
    partial_ = chunk.mid(last_newline + 1);
    chunk.truncate(last_newline + 1);
    for (const QByteArray& raw : chunk.split('\n')) {
        // The debug console carries bytes as the driver wrote them,
        // CRs included.
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        ++line_count_;
        read_fact(line, facts_);
        lines_ << line;
    }
    if (lines_.size() > kKeptLines) {
        lines_ = lines_.mid(lines_.size() - kKeptLines);
    }
    return true;
}

}  // namespace lsw::host::vm
