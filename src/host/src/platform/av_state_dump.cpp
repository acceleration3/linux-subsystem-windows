#include "lsw/host/platform/av_state_dump.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace lsw::host::platform {

namespace {

constexpr qsizetype kMaxDump = 8 * 1024 * 1024;
constexpr qsizetype kMaxValue = 512;
constexpr qsizetype kMaxLine = 16 * 1024;
constexpr int kTimeoutMs = 1500;
constexpr int kMaxContexts = 4096;
constexpr int kMaxChannels = 4096;
constexpr int kCachingWriteCombined = 2;

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

QString unescape(const QByteArray& raw) {
    QByteArray out;
    const qsizetype size = std::min<qsizetype>(raw.size(), kMaxValue * 4);
    for (qsizetype i = 0; i < size && out.size() < kMaxValue; ++i) {
        const char c = raw.at(i);
        if (c != '\\' || i + 1 >= size) {
            out.append(c);
            continue;
        }
        const char next = raw.at(++i);
        if (next == 'n') {
            out.append('\n');
        } else if (next == 'r') {
            out.append('\r');
        } else if (next == 'x' && i + 2 < size && hex_digit(raw.at(i + 1)) >= 0
                   && hex_digit(raw.at(i + 2)) >= 0) {
            out.append(static_cast<char>(hex_digit(raw.at(i + 1)) * 16 + hex_digit(raw.at(i + 2))));
            i += 2;
        } else {
            out.append(next);
        }
    }
    QString text = QString::fromUtf8(out);
    for (QChar& ch : text) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f) {
            ch = QLatin1Char(' ');
        }
    }
    return text.trimmed();
}

std::optional<quint64> number(const QHash<QString, QString>& values, const QString& key) {
    const auto it = values.constFind(key);
    if (it == values.constEnd() || it->isEmpty() || it->size() > 20) {
        return std::nullopt;
    }
    for (const QChar ch : *it) {
        if (ch.unicode() < '0' || ch.unicode() > '9') {
            return std::nullopt;
        }
    }
    bool ok = false;
    const quint64 value = it->toULongLong(&ok);
    return ok ? std::optional<quint64>{value} : std::nullopt;
}

void add_known(std::optional<quint64>& total, const std::optional<quint64>& value) {
    if (value) {
        total = total.value_or(0) + *value;
    }
}

quint64 number_or_zero(const QHash<QString, QString>& values, const QString& key) {
    return number(values, key).value_or(0);
}

std::optional<bool> flag(const QHash<QString, QString>& values, const QString& key) {
    const auto it = values.constFind(key);
    if (it == values.constEnd()) {
        return std::nullopt;
    }
    if (*it == QLatin1String("true")) {
        return true;
    }
    if (*it == QLatin1String("false")) {
        return false;
    }
    return std::nullopt;
}

std::optional<quint32> segment_id(const QString& text) {
    if (text.isEmpty() || text.size() > 10) {
        return std::nullopt;
    }
    bool ok = false;
    const quint32 value = text.toUInt(&ok);
    return ok ? std::optional<quint32>{value} : std::nullopt;
}

quint64 context_key(quint32 device, quint32 id) {
    return (static_cast<quint64>(device) << 32) | id;
}

AvStateDump::Context& context_for(AvStateDump::Snapshot& into, QHash<quint64, int>& index,
                                  quint32 device, quint32 id) {
    const quint64 key = context_key(device, id);
    const auto it = index.constFind(key);
    if (it != index.constEnd()) {
        return into.contexts[*it];
    }
    index.insert(key, static_cast<int>(into.contexts.size()));
    AvStateDump::Context context;
    context.device = device;
    context.id = id;
    into.contexts.push_back(context);
    return into.contexts.last();
}

AvStateDump::Channel& channel_for(AvStateDump::Snapshot& into, QHash<quint32, int>& index,
                                  quint32 id) {
    const auto it = index.constFind(id);
    if (it != index.constEnd()) {
        return into.channels[*it];
    }
    index.insert(id, static_cast<int>(into.channels.size()));
    AvStateDump::Channel channel;
    channel.context = id;
    into.channels.push_back(channel);
    return into.channels.last();
}

std::optional<qint64> parent_of(qint64 pid) {
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray stat = file.read(4096);
    const qsizetype close = stat.lastIndexOf(')');
    if (close < 0) {
        return std::nullopt;
    }
    const QList<QByteArray> rest = stat.mid(close + 2).split(' ');
    if (rest.size() < 2) {
        return std::nullopt;
    }
    bool ok = false;
    const qint64 parent = rest.at(1).toLongLong(&ok);
    return ok ? std::optional<qint64>{parent} : std::nullopt;
}

}

quint64 AvStateDump::Snapshot::used_bytes() const {
    quint64 total = 0;
    for (const auto& context : contexts) {
        total += context.bytes();
    }
    return total;
}

const AvStateDump::Context* AvStateDump::Snapshot::find_context(quint32 device, quint32 id) const {
    for (const auto& context : contexts) {
        if (context.device == device && context.id == id) {
            return &context;
        }
    }
    return nullptr;
}

int AvStateDump::Snapshot::contexts_with_id(quint32 id) const {
    int count = 0;
    for (const auto& context : contexts) {
        count += context.id == id ? 1 : 0;
    }
    return count;
}

const AvStateDump::Context* AvStateDump::Snapshot::find_context(quint32 id) const {
    for (const auto& context : contexts) {
        if (context.id == id) {
            return &context;
        }
    }
    return nullptr;
}

QHash<QString, QString> AvStateDump::fields(const QByteArray& line, QString* path) {
    QHash<QString, QString> out;
    const qsizetype space = line.indexOf(' ');
    if (path != nullptr) {
        *path = QString::fromLatin1(space < 0 ? line : line.left(space)).left(256);
    }
    if (space < 0) {
        return out;
    }
    for (const QByteArray& field : line.mid(space + 1).split(' ')) {
        qsizetype equals = -1;
        for (qsizetype i = 0; i < field.size(); ++i) {
            if (field.at(i) == '\\') {
                ++i;
            } else if (field.at(i) == '=') {
                equals = i;
                break;
            }
        }
        if (equals <= 0 || equals > 64) {
            continue;
        }
        out.insert(QString::fromLatin1(field.left(equals)), unescape(field.mid(equals + 1)));
    }
    return out;
}

void AvStateDump::parse(const QByteArray& dump, Snapshot& into) {
    QHash<quint64, int> context_index;
    QHash<quint32, int> channel_index;
    for (const Context& context : std::as_const(into.contexts)) {
        context_index.insert(context_key(context.device, context.id), static_cast<int>(context_index.size()));
    }
    for (const Channel& channel : std::as_const(into.channels)) {
        channel_index.insert(channel.context, static_cast<int>(channel_index.size()));
    }
    qsizetype from = 0;
    while (from < dump.size()) {
        qsizetype end = dump.indexOf('\n', from);
        if (end < 0) {
            end = dump.size();
        }
        const QByteArray line = dump.mid(from, std::min<qsizetype>(end - from, kMaxLine));
        from = end + 1;
        if (line.isEmpty()) {
            continue;
        }
        QString path;
        const auto values = fields(line, &path);
        const QStringList parts = path.split(QLatin1Char('/'));
        const qsizetype n = parts.size();
        if (n >= 3 && parts.at(0) == QLatin1String("module") && parts.at(2) == QLatin1String("device")) {
            if (n == 4) {
                into.device = true;
                if (const auto bytes = number(values, QStringLiteral("host_window_bytes"))) {
                    into.window_bytes = into.window_bytes.value_or(0) + *bytes;
                }
            } else if (n == 8 && parts.at(4) == QLatin1String("host_blobs")
                       && parts.at(6) == QLatin1String("host_blob")) {
                const quint32 device = segment_id(parts.at(3)).value_or(0);
                const auto id = number(values, QStringLiteral("context"));
                const auto size = number(values, QStringLiteral("size"));
                const auto caching = number(values, QStringLiteral("caching"));
                if (!id || *id > 0xffffffffULL || !size || flag(values, QStringLiteral("mapped")) != true
                    || (!context_index.contains(context_key(device, static_cast<quint32>(*id))) && context_index.size() >= kMaxContexts)) {
                    continue;
                }
                Context& context = context_for(into, context_index, device, static_cast<quint32>(*id));
                if (caching == kCachingWriteCombined) {
                    context.write_combined_bytes += *size;
                } else {
                    context.write_back_bytes += *size;
                }
                ++context.blobs;
            } else if (n == 8 && parts.at(4) == QLatin1String("contexts") && parts.at(6) == QLatin1String("context")) {
                const quint32 device = segment_id(parts.at(3)).value_or(0);
                const auto id = segment_id(parts.at(7));
                const auto name = values.constFind(QStringLiteral("name"));
                if (!id || name == values.constEnd()
                    || (!context_index.contains(context_key(device, *id)) && context_index.size() >= kMaxContexts)) {
                    continue;
                }
                Context& context = context_for(into, context_index, device, *id);
                context.name = *name;
                context.batches = number(values, QStringLiteral("batches"));
            } else if (n == 6 && parts.at(4) == QLatin1String("window_stats")) {
                WindowStats stats = into.window_stats.value_or(WindowStats{});
                stats.attached = stats.attached || flag(values, QStringLiteral("attached")).value_or(false);
                stats.stable = stats.stable || flag(values, QStringLiteral("stable")).value_or(false);
                stats.total_pages += number_or_zero(values, QStringLiteral("total_pages"));
                stats.pages_in_use += number_or_zero(values, QStringLiteral("pages_in_use"));
                stats.peak_pages_in_use += number_or_zero(values, QStringLiteral("peak_pages_in_use"));
                stats.refusals += number_or_zero(values, QStringLiteral("refusals"));
                stats.refused_bytes += number_or_zero(values, QStringLiteral("refused_bytes"));
                stats.largest_free_run_at_refusal =
                    std::max(stats.largest_free_run_at_refusal, number_or_zero(values, QStringLiteral("largest_free_run_at_refusal")));
                into.window_stats = stats;
            }
        } else if (n == 6 && parts.at(0) == QLatin1String("executor") && parts.at(2) == QLatin1String("session")
                   && parts.at(4) == QLatin1String("channel")) {
            const auto id = segment_id(parts.at(5));
            if (!id || (!channel_index.contains(*id) && channel_index.size() >= kMaxChannels)) {
                continue;
            }
            Channel& channel = channel_for(into, channel_index, *id);
            channel.reports += number_or_zero(values, QStringLiteral("reports"));
            channel.rejected += number_or_zero(values, QStringLiteral("rejected"));
            channel.split_transfers += number_or_zero(values, QStringLiteral("split_transfers"));
            channel.split_pieces += number_or_zero(values, QStringLiteral("split_pieces"));
            add_known(channel.blob_room_waits, number(values, QStringLiteral("blob_room_waits")));
            add_known(channel.blob_room_wait_us, number(values, QStringLiteral("blob_room_wait_us")));
            add_known(channel.ring_waits, number(values, QStringLiteral("ring_waits")));
            add_known(channel.ring_wait_us, number(values, QStringLiteral("ring_wait_us")));
            add_known(channel.fence_waits, number(values, QStringLiteral("fence_waits")));
            add_known(channel.fence_wait_us, number(values, QStringLiteral("fence_wait_us")));
            channel.inline_bulk += number_or_zero(values, QStringLiteral("inline_bulk"));
            channel.copied_mappings += number_or_zero(values, QStringLiteral("copied_mappings"));
            channel.slice_waits += number_or_zero(values, QStringLiteral("slice_waits"));
            channel.refused_no_space += number_or_zero(values, QStringLiteral("refused_no_space"));
            channel.refused_no_memory += number_or_zero(values, QStringLiteral("refused_no_memory"));
            channel.refused_other += number_or_zero(values, QStringLiteral("refused_other"));
            add_known(channel.arena_grows, number(values, QStringLiteral("arena_grows")));
            add_known(channel.arena_bytes, number(values, QStringLiteral("arena_bytes")));
            add_known(channel.arena_peak_ring_bytes, number(values, QStringLiteral("arena_peak_ring_bytes")));
            add_known(channel.arena_set_aside_bytes, number(values, QStringLiteral("arena_set_aside_bytes")));
            add_known(channel.arena_segments, number(values, QStringLiteral("arena_segments")));
            add_known(channel.parked_blobs, number(values, QStringLiteral("parked_blobs")));
            into.executor = true;
        } else if (n == 6 && parts.at(0) == QLatin1String("executor") && parts.at(2) == QLatin1String("gpu_shares")
                   && parts.at(4) == QLatin1String("memory_source")) {
            const auto device = segment_id(parts.at(3));
            const auto cap = number(values, QStringLiteral("vram_cap_bytes"));
            const auto reserve = number(values, QStringLiteral("vram_reserve_bytes"));
            if (device && (cap || reserve)) {
                GpuVram entry;
                entry.device = *device;
                entry.guest_cap_bytes = cap;
                entry.reserve_bytes = reserve;
                into.vram.push_back(entry);
            }
            into.executor = true;
        } else if (n >= 1 && parts.at(0) == QLatin1String("executor")) {
            into.executor = true;
        }
    }
}

std::optional<QByteArray> AvStateDump::fetch(const QString& socket_path) {
    const QByteArray path = QFile::encodeName(socket_path);
    sockaddr_un address = {};
    if (path.isEmpty() || path.size() >= static_cast<qsizetype>(sizeof address.sun_path)) {
        return std::nullopt;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return std::nullopt;
    }
    address.sun_family = AF_UNIX;
    std::copy(path.begin(), path.end(), address.sun_path);
    const auto fail = [fd]() -> std::optional<QByteArray> {
        ::close(fd);
        return std::nullopt;
    };
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        return fail();
    }
    static constexpr char kRequest[] = "state\n";
    if (::send(fd, kRequest, sizeof kRequest - 1, MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof kRequest - 1)) {
        return fail();
    }
    QByteArray out;
    char buffer[65536];
    for (;;) {
        pollfd waiting = {fd, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, kTimeoutMs);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            return fail();
        }
        const ssize_t got = ::recv(fd, buffer, sizeof buffer, 0);
        if (got == 0) {
            break;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail();
        }
        out.append(buffer, static_cast<qsizetype>(got));
        if (out.size() > kMaxDump) {
            return fail();
        }
    }
    ::close(fd);
    return out;
}

QVector<QString> AvStateDump::sockets(qint64 qemu_pid) {
    QVector<QString> out;
    if (qemu_pid <= 0) {
        return out;
    }
    static const QRegularExpression pattern(
        QStringLiteral("^av-state-(device|executor)-(\\d{1,10})\\.sock$"));
    const QDir shm(QStringLiteral("/dev/shm"));
    for (const QFileInfo& entry : shm.entryInfoList({QStringLiteral("av-state-*.sock")},
                                                    QDir::System | QDir::Files | QDir::NoDotAndDotDot)) {
        const auto match = pattern.match(entry.fileName());
        if (!match.hasMatch()) {
            continue;
        }
        const qint64 pid = match.captured(2).toLongLong();
        if (pid == qemu_pid || parent_of(pid) == qemu_pid) {
            out.push_back(entry.filePath());
        }
    }
    return out;
}

AvStateDump::Snapshot AvStateDump::read(qint64 qemu_pid) {
    Snapshot snapshot;
    for (const QString& socket : sockets(qemu_pid)) {
        if (const auto dump = fetch(socket)) {
            parse(*dump, snapshot);
        }
    }
    return snapshot;
}

}
