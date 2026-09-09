#include <algorithm>

#include <QDateTime>
#include "lsw/host/vm/render_stats.hpp"

#include "lsw/base/log.hpp"

#include <QFile>
#include <QRegularExpression>

#include <csignal>

#include "rvk/wire/region_layout.hpp"

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

// Take what the server published over what the log said. Both describe
// the same events, and the header is the one that cannot be behind: a
// warning is printed on the first occurrence and then on every power of
// two, so between two of them the log's count is stale by design.
void adopt(ChannelPressure& channel, const rvk::wire::RegionCounters& counters) {
    auto& ring = channel.areas[kRing];
    if (counters.chunked_commands > 0) {
        ring.events = counters.chunked_commands;
        ring.largest_bytes = counters.chunked_largest_bytes;
        ring.suggested_kib = counters.ring_suggest_kib;
        ring.state = state_for(ring.events);
    }
    auto& reply = channel.areas[kReply];
    if (counters.partial_replies > 0) {
        reply.events = counters.partial_replies;
        reply.extra_trips = counters.reply_extra_trips;
        reply.largest_bytes = counters.reply_largest_bytes;
        reply.suggested_kib = counters.reply_suggest_kib;
        reply.state = state_for(reply.events);
    }
}

// What the guest publishes about the two areas it owns.
//
// The server can only report what reached it, and two of the three
// areas on the page are not its to report at all: the blob arena is
// carved inside the guest, and how full the ring runs is known only to
// the process filling it. Both were therefore permanently blank —
// events at zero, no recommendation, and a meter with nothing behind
// it — on a channel that was working perfectly hard.
//
// ``published_ns`` of zero is a guest that predates this block. Its
// zeroes are not measurements and nothing here is taken from them.
void adopt_client(ChannelPressure& channel, const rvk::wire::RegionClientUsage& client) {
    if (client.published_ns == 0) {
        return;
    }

    auto& ring = channel.areas[kRing];
    // Believable for the same reason the throughput largests are
    // checked: a field written at a different offset by an older build
    // arrives as a plausible integer, and an occupancy larger than the
    // area it is an occupancy of is the tell.
    if (ring.configured_kib > 0
        && client.ring_peak_inflight <= ring.configured_kib * 1024ULL) {
        // Taken as published rather than maxed with what was here
        // before. The guest already keeps the high-water mark for its
        // own session; accumulating on top of that only carries one
        // application's peak onto the next one to claim the channel.
        ring.peak_bytes = client.ring_peak_inflight;
        ring.used_bytes = client.ring_inflight;
        ring.measured = true;
    }
    // The sending side's count of the same events the server counts.
    // Taken only where the server has nothing, so the two never
    // disagree on screen: the server's is what actually arrived.
    if (ring.events == 0 && client.chunked_commands > 0) {
        ring.events = client.chunked_commands;
        ring.largest_bytes = qMax(ring.largest_bytes,
                                  static_cast<quint64>(client.chunked_largest_bytes));
        ring.state = state_for(ring.events);
    }

    auto& reply = channel.areas[kReply];
    if (reply.events == 0 && client.partial_replies > 0) {
        reply.events = client.partial_replies;
        reply.extra_trips = client.reply_extra_trips;
        reply.largest_bytes = qMax(reply.largest_bytes,
                                   static_cast<quint64>(client.reply_largest_bytes));
        reply.state = state_for(reply.events);
    }

    // The blob arena, which has no other source at all.
    auto& blob = channel.areas[kBlob];
    if (blob.configured_kib > 0 && client.blob_in_use <= blob.configured_kib * 1024ULL) {
        blob.used_bytes = client.blob_in_use;
        blob.peak_bytes = client.blob_peak;
        blob.measured = true;
    }
    if (client.blob_failures > 0) {
        blob.events = client.blob_failures;
        blob.largest_bytes = client.blob_largest_failure;
        blob.suggested_kib = client.blob_suggest_mib * 1024ULL;
        blob.state = state_for(blob.events);
    }
}

// Whether whoever holds a claim is still running.
//
// The claim word is a process id from one side or the other, and the
// top bit says which: set means the guest took it. A guest id means
// nothing to kill(), and asking anyway reports every guest-held channel
// as abandoned — which is what the page showed. The transport draws the
// same distinction from the other side; see claim_holder_alive.
constexpr std::uint32_t kGuestClaim = 0x80000000U;

// Running totals into rates.
//
// The server publishes what it has carried since the session began and
// nothing about time, which is right: it has no idea how often anything
// is looking. Two samples and the clock between them are the reader's
// business.
void adopt_throughput(ChannelPressure& channel,
                      const rvk::wire::RegionThroughput& totals) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - channel.last_sampled_ms;
    // A first sample has nothing to subtract from, and a counter that
    // went backwards is a new session on the same region rather than
    // negative work.
    const bool usable = channel.last_sampled_ms != 0 && elapsed > 0
                        && totals.commands >= channel.last_commands
                        && totals.frames >= channel.last_frames;
    if (usable) {
        const double seconds = static_cast<double>(elapsed) / 1000.0;
        channel.commands_per_second =
            static_cast<double>(totals.commands - channel.last_commands) / seconds;
        channel.bytes_per_second =
            static_cast<double>((totals.command_bytes + totals.reply_bytes)
                                - channel.last_bytes) / seconds;
        channel.frames_per_second =
            static_cast<double>(totals.frames - channel.last_frames) / seconds;
    } else {
        channel.commands_per_second = 0.0;
        channel.bytes_per_second = 0.0;
        channel.frames_per_second = 0.0;
    }
    // The biggest thing each area has carried, congested or not. The
    // congestion counters only remember what overflowed, so without
    // this an area that is working reports nothing about the sizes it
    // handles — and that is what says how much headroom is left.
    // Nothing carried by an area can be larger than the area, so a
    // value that is says the writer laid this struct out differently —
    // an older server against a newer manager. Believing it once put a
    // nanosecond clock on screen as a payload of twenty-six million
    // mebibytes, so it is checked rather than trusted.
    const auto believable = [](const AreaPressure& area, std::uint64_t bytes) {
        return bytes > 0 && area.configured_kib > 0
               && bytes <= area.configured_kib * 1024ULL;
    };
    if (channel.areas.size() > kRing
        && believable(channel.areas[kRing], totals.largest_command_bytes)) {
        channel.areas[kRing].largest_bytes =
            std::max(channel.areas[kRing].largest_bytes,
                     static_cast<quint64>(totals.largest_command_bytes));
    }
    if (channel.areas.size() > kReply
        && believable(channel.areas[kReply], totals.largest_reply_bytes)) {
        channel.areas[kReply].largest_bytes =
            std::max(channel.areas[kReply].largest_bytes,
                     static_cast<quint64>(totals.largest_reply_bytes));
        // The slot holds one answer at a time, so the largest answer
        // written IS how full it has been — the only area where those
        // two questions have the same answer.
        // No live reading to have: the slot holds one answer, and
        // between two of them it holds nothing. The high-water mark is
        // the whole of what can be said, which is why this is the one
        // card whose number sits still on a busy channel.
        channel.areas[kReply].used_bytes = channel.areas[kReply].largest_bytes;
        channel.areas[kReply].peak_bytes = channel.areas[kReply].largest_bytes;
        channel.areas[kReply].measured = true;
    }

    channel.last_commands = totals.commands;
    channel.last_bytes = totals.command_bytes + totals.reply_bytes;
    channel.last_frames = totals.frames;
    channel.last_sampled_ms = now;
}

bool alive(std::uint32_t claim) {
    if (claim == 0) {
        return false;
    }
    if ((claim & kGuestClaim) != 0) {
        // Not ours to interpret. The guest reclaims its own dead
        // claims; from here the honest answer is "held".
        return true;
    }
    return ::kill(static_cast<int>(claim), 0) == 0;
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
               "%1 allocations found no run large enough. The arena asks for %2 or more.")
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
        // Counters move far more often than ownership does, and a page
        // that only redraws when a channel changes hands shows numbers
        // that were true the last time somebody claimed one.
        const double was_rate = channel.commands_per_second;
        QVector<quint64> was_events;
        QVector<quint64> was_used;
        was_events.reserve(channel.areas.size());
        was_used.reserve(channel.areas.size());
        for (const auto& area : channel.areas) {
            was_events.push_back(area.events);
            was_used.push_back(area.used_bytes);
        }

        channel.claimed = false;
        channel.owner_pid = 0;

        QFile file{QStringLiteral("/dev/shm/%1-%2").arg(region_prefix).arg(i)};
        if (file.open(QIODevice::ReadOnly)) {
            rvk::wire::RegionHeader header{};
            const auto read = file.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (read == static_cast<qint64>(sizeof(header))
                && header.magic == rvk::wire::RegionHeader{}.magic && header.ready != 0) {
                channel.owner_pid = header.owner_pid;
                channel.claimed = alive(header.owner_pid);
                // The sizes come from the region rather than from the
                // settings, so a channel still running on what it was
                // started with reports that and not what has since been
                // asked for.
                channel.areas[kRing].configured_kib = header.ring_buffer_bytes >> 10;
                channel.areas[kReply].configured_kib = header.reply_bytes >> 10;
                channel.areas[kBlob].configured_kib = header.blob_bytes >> 10;
                adopt(channel, header.counters);
                adopt_throughput(channel, header.throughput);
                adopt_client(channel, header.client);
            }
        }
        touched = touched || channel.claimed != was_claimed
                  || channel.owner_pid != was_owner;
        for (int a = 0; a < channel.areas.size() && a < was_events.size(); ++a) {
            touched = touched || channel.areas[a].events != was_events[a]
                      || channel.areas[a].used_bytes != was_used[a];
        }
        touched = touched || channel.commands_per_second != was_rate;
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
    rvk::wire::RegionHeader header{};
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header))
            != static_cast<qint64>(sizeof(header))
        || header.magic != rvk::wire::RegionHeader{}.magic) {
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
            a.used_bytes = 0;
            a.peak_bytes = 0;
            a.measured = false;
            a.state = AreaPressure::State::Healthy;
        }
    }
    Q_EMIT updated();
}

}  // namespace lsw::host::vm
