#include "lsw/host/vm/render_stats.hpp"

#include "lsw/base/log.hpp"

#include <QFile>
#include <QRegularExpression>

#include <csignal>

namespace lsw::host::vm {

namespace {

// The three areas, in the order they appear on the page.
constexpr int kRing = 0;
constexpr int kReply = 1;
constexpr int kBlob = 2;

// Where "a handful" stops and "repeatedly" starts. The server logs its
// own warnings on the first occurrence and then on every power of two,
// so a workload that splits constantly reaches double digits quickly
// and one that split once at startup never does.
constexpr quint64 kCongestedEvents = 8;

// The server names a session in its own logs only when there is more
// than one, so an unqualified warning belongs to the channel currently
// doing the work. With one channel that is unambiguous; with several,
// this is what the prefix is for.
const QRegularExpression& session_re() {
    static const QRegularExpression re{QStringLiteral(R"(session\s+(\d+))")};
    return re;
}

const QRegularExpression& ring_re() {
    // "command 0x… is 3571712 bytes and arrived in pieces … 214 such
    //  commands so far — raise --ring-kib to 4096 to stop paying"
    static const QRegularExpression re{
        QStringLiteral(R"(is (\d+) bytes and arrived in pieces.*?(\d+) such commands.*?--ring-kib to (\d+))")};
    return re;
}

const QRegularExpression& ring_client_re() {
    // The same event seen from the guest: "… is 3571712 bytes and the
    // ring holds 1048576: sending it in 4 passes … start the server
    // with --ring-kib 4096".
    static const QRegularExpression re{
        QStringLiteral(R"(is (\d+) bytes and the ring holds \d+: sending it in (\d+) passes.*?(\d+) commands have taken this path.*?--ring-kib (\d+))")};
    return re;
}

const QRegularExpression& reply_re() {
    // "an answer of 11744000 bytes does not fit the 8384512 byte reply
    //  slot: the client will fetch it in 3 pieces … 6 such answers so
    //  far — raise --reply-kib to 16384"
    static const QRegularExpression re{
        QStringLiteral(R"(an answer of (\d+) bytes does not fit.*?in (\d+) pieces.*?(\d+) such answers.*?--reply-kib to (\d+))")};
    return re;
}

const QRegularExpression& reply_client_re() {
    static const QRegularExpression re{
        QStringLiteral(R"(answered with (\d+) bytes and the reply slot holds \d+: collected in (\d+) extra round trips.*?(\d+) such answers.*?--reply-kib (\d+))")};
    return re;
}

const QRegularExpression& blob_re() {
    // "blob arena has no run of 67108864 bytes (110 of 256 MiB in use,
    //  peak 168 MiB, 312 blocks, 4 failed allocations) — …,
    //  --blob-mib 384 or more"
    static const QRegularExpression re{
        QStringLiteral(R"(blob arena has no run of (\d+) bytes.*?(\d+) failed allocations.*?--blob-mib (\d+))")};
    return re;
}

AreaPressure::State state_for(quint64 events) {
    if (events == 0) {
        return AreaPressure::State::Healthy;
    }
    return events >= kCongestedEvents ? AreaPressure::State::Congested
                                      : AreaPressure::State::Tight;
}

QString size_label(quint64 kib) {
    if (kib >= 1024 && (kib % 1024) == 0) {
        return QStringLiteral("%1 MiB").arg(kib / 1024);
    }
    return QStringLiteral("%1 KiB").arg(kib);
}

// The first fields of the region header the render server writes, in
// its order. Only the part this reads is mirrored, and it is checked
// against the magic before anything else is believed, so a header that
// has moved on is refused rather than misread.
//
// See rvk/wire/region_layout.hpp in the render server for the whole of
// it.
struct RegionHeaderPrefix {
    std::uint32_t magic;
    std::uint32_t header_bytes;
    std::uint32_t ready;
    std::uint32_t reserved_0;
    std::uint64_t region_bytes;
    std::uint32_t owner_pid;
    std::uint32_t reserved_1;
    std::uint64_t ring_offset;
    std::uint64_t ring_buffer_bytes;
    std::uint64_t ring_extra_bytes;
    std::uint64_t reply_offset;
    std::uint64_t reply_bytes;
    std::uint64_t blob_offset;
    std::uint64_t blob_bytes;
};

constexpr std::uint32_t kRegionMagic = 0x524B5247;  // "RKRG"

bool alive(std::uint32_t pid) {
    return pid != 0 && ::kill(static_cast<int>(pid), 0) == 0;
}

}  // namespace

QString AreaPressure::advice() const {
    if (state == State::Healthy || suggested_kib == 0) {
        return {};
    }
    if (name.startsWith(QLatin1String("Command"))) {
        return QStringLiteral(
                   "%1 commands were sent in several passes, each waiting for the server "
                   "to drain. The server's own suggestion is %2.")
            .arg(events)
            .arg(size_label(suggested_kib));
    }
    if (name.startsWith(QLatin1String("Reply"))) {
        return QStringLiteral(
                   "%1 answers did not fit and were collected in %2 extra round trips. "
                   "%3 would have taken every one of them whole.")
            .arg(events)
            .arg(extra_trips)
            .arg(size_label(suggested_kib));
    }
    return QStringLiteral(
               "%1 allocations found no run large enough. The server asks for %2 or more.")
        .arg(events)
        .arg(size_label(suggested_kib));
}

AreaPressure::State ChannelPressure::worst() const {
    auto worst = AreaPressure::State::Healthy;
    for (const auto& a : areas) {
        if (a.state > worst) {
            worst = a.state;
        }
    }
    return worst;
}

RenderStats::RenderStats(QObject* parent) : QObject(parent) {
    set_channel_count(1);
}

void RenderStats::set_channel_count(int channels) {
    const int wanted = qMax(1, channels);
    if (channels_.size() == wanted) {
        return;
    }
    const int before = static_cast<int>(channels_.size());
    channels_.resize(wanted);
    for (int i = before; i < wanted; ++i) {
        ChannelPressure channel;
        channel.index = i;
        channel.areas = {
            AreaPressure{QStringLiteral("Command ring"), 0, 0, 0, 0, 0,
                         AreaPressure::State::Healthy},
            AreaPressure{QStringLiteral("Reply slot"), 0, 0, 0, 0, 0,
                         AreaPressure::State::Healthy},
            AreaPressure{QStringLiteral("Blob arena"), 0, 0, 0, 0, 0,
                         AreaPressure::State::Healthy},
        };
        channels_[i] = channel;
    }
    Q_EMIT updated();
}

void RenderStats::set_area_sizes(int ring_kib, int reply_kib, int blob_mib) {
    for (auto& channel : channels_) {
        channel.areas[kRing].configured_kib = static_cast<quint64>(ring_kib);
        channel.areas[kReply].configured_kib = static_cast<quint64>(reply_kib);
        channel.areas[kBlob].configured_kib =
            static_cast<quint64>(blob_mib) * 1024ULL;
    }
    Q_EMIT updated();
}

AreaPressure* RenderStats::area(int channel, int index) {
    if (channel < 0 || channel >= channels_.size()) {
        channel = 0;
    }
    if (channels_.isEmpty()) {
        return nullptr;
    }
    return &channels_[channel].areas[index];
}

void RenderStats::ingest(const QString& line) {
    int channel = 0;
    const auto session = session_re().match(line);
    if (session.hasMatch()) {
        channel = session.captured(1).toInt();
    }

    bool touched = false;

    auto ring = ring_re().match(line);
    if (!ring.hasMatch()) {
        ring = ring_client_re().match(line);
    }
    if (ring.hasMatch()) {
        if (auto* a = area(channel, kRing)) {
            a->largest_bytes = qMax(a->largest_bytes, ring.captured(1).toULongLong());
            // The server's form counts commands; the client's counts
            // passes first and commands second. Both end with the
            // recommendation, so the last capture is always the size.
            const int last = ring.lastCapturedIndex();
            a->events = ring.captured(last - 1).toULongLong();
            a->suggested_kib = ring.captured(last).toULongLong();
            a->state = state_for(a->events);
            touched = true;
        }
    }

    auto reply = reply_re().match(line);
    if (!reply.hasMatch()) {
        reply = reply_client_re().match(line);
    }
    if (reply.hasMatch()) {
        if (auto* a = area(channel, kReply)) {
            a->largest_bytes = qMax(a->largest_bytes, reply.captured(1).toULongLong());
            a->extra_trips += reply.captured(2).toULongLong();
            a->events = reply.captured(3).toULongLong();
            a->suggested_kib = reply.captured(4).toULongLong();
            a->state = state_for(a->events);
            touched = true;
        }
    }

    const auto blob = blob_re().match(line);
    if (blob.hasMatch()) {
        if (auto* a = area(channel, kBlob)) {
            a->largest_bytes = qMax(a->largest_bytes, blob.captured(1).toULongLong());
            a->events = blob.captured(2).toULongLong();
            // The flag is in MiB; everything here is KiB.
            a->suggested_kib = blob.captured(3).toULongLong() * 1024ULL;
            a->state = state_for(a->events);
            touched = true;
        }
    }

    if (touched) {
        Q_EMIT updated();
    }
}

void RenderStats::poll_regions(const QString& region_prefix) {
    bool touched = false;
    for (int i = 0; i < channels_.size(); ++i) {
        auto& channel = channels_[i];
        const bool was_claimed = channel.claimed;
        const quint32 was_owner = channel.owner_pid;

        channel.claimed = false;
        channel.owner_pid = 0;

        QFile file{QStringLiteral("/dev/shm/%1-%2").arg(region_prefix).arg(i)};
        if (file.open(QIODevice::ReadOnly)) {
            RegionHeaderPrefix header{};
            const auto read = file.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (read == static_cast<qint64>(sizeof(header)) && header.magic == kRegionMagic
                && header.ready != 0) {
                channel.owner_pid = header.owner_pid;
                channel.claimed = alive(header.owner_pid);
                // The sizes come from the region rather than from the
                // settings, so a channel still running on what it was
                // started with reports that and not what has since been
                // asked for.
                channel.areas[kRing].configured_kib = header.ring_buffer_bytes >> 10;
                channel.areas[kReply].configured_kib = header.reply_bytes >> 10;
                channel.areas[kBlob].configured_kib = header.blob_bytes >> 10;
            }
        }
        touched = touched || channel.claimed != was_claimed
                  || channel.owner_pid != was_owner;
    }
    if (touched) {
        Q_EMIT updated();
    }
}

bool RenderStats::release_channel(const QString& region_prefix, int index) {
    if (index < 0 || index >= channels_.size()) {
        return false;
    }
    const auto& channel = channels_.at(index);
    if (alive(channel.owner_pid)) {
        LSW_WARN("render: refusing to release channel {}; pid {} is still running",
                 index, channel.owner_pid);
        return false;
    }
    QFile file{QStringLiteral("/dev/shm/%1-%2").arg(region_prefix).arg(index)};
    if (!file.open(QIODevice::ReadWrite)) {
        return false;
    }
    RegionHeaderPrefix header{};
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header))
            != static_cast<qint64>(sizeof(header))
        || header.magic != kRegionMagic) {
        return false;
    }
    header.owner_pid = 0;
    file.seek(0);
    const bool ok = file.write(reinterpret_cast<const char*>(&header), sizeof(header))
                    == static_cast<qint64>(sizeof(header));
    channels_[index].claimed = false;
    channels_[index].owner_pid = 0;
    Q_EMIT updated();
    return ok;
}

AreaPressure::State RenderStats::worst() const {
    auto worst = AreaPressure::State::Healthy;
    for (const auto& channel : channels_) {
        const auto s = channel.worst();
        if (s > worst) {
            worst = s;
        }
    }
    return worst;
}

QString RenderStats::headline() const {
    for (const auto& channel : channels_) {
        for (const auto& a : channel.areas) {
            if (a.state == AreaPressure::State::Congested) {
                return QStringLiteral("%1 on channel %2 is congested. %3")
                    .arg(a.name)
                    .arg(channel.index)
                    .arg(a.advice());
            }
        }
    }
    for (const auto& channel : channels_) {
        for (const auto& a : channel.areas) {
            if (a.state == AreaPressure::State::Tight) {
                return QStringLiteral("%1 on channel %2 is tight. %3")
                    .arg(a.name)
                    .arg(channel.index)
                    .arg(a.advice());
            }
        }
    }
    return {};
}

void RenderStats::reset() {
    for (auto& channel : channels_) {
        for (auto& a : channel.areas) {
            a.events = 0;
            a.extra_trips = 0;
            a.largest_bytes = 0;
            a.suggested_kib = 0;
            a.state = AreaPressure::State::Healthy;
        }
    }
    Q_EMIT updated();
}

}  // namespace lsw::host::vm
