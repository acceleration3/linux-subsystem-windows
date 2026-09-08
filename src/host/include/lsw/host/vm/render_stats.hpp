#pragma once

#include <cstdint>

#include <QObject>
#include <QString>
#include <QVector>

namespace lsw::host::vm {

// How hard one area of a render channel is being pushed.
//
// The three areas run out separately and for different reasons, so
// there is a set of these per channel rather than one number for the
// region: a command ring that keeps splitting commands and a blob arena
// with room to spare are not the same problem and do not have the same
// fix.
struct AreaPressure {
    enum class State : std::uint8_t {
        Healthy,    // nothing has waited
        Tight,      // a handful of operations have split
        Congested,  // splitting repeatedly, which is round trips per frame
    };

    QString name;              // "Command ring", "Reply slot", "Blob arena"
    quint64 configured_kib = 0;
    quint64 events = 0;        // splits, partial answers, failed allocations
    quint64 extra_trips = 0;   // what those splits cost
    quint64 largest_bytes = 0; // the operation that did not fit
    quint64 suggested_kib = 0; // the server's own recommendation, not ours
    State state = State::Healthy;

    [[nodiscard]] QString advice() const;
};

// One channel's worth of pressure, plus who is holding it.
struct ChannelPressure {
    int index = 0;
    bool claimed = false;
    // The claim word as the region carries it. A claim taken inside the
    // guest has its top bit set, because the two sides number their
    // processes independently and a bare number would be ambiguous.
    quint32 owner_pid = 0;
    // The process id with that marking removed, which is what the
    // process calls itself on whichever side it runs.
    [[nodiscard]] quint32 owner() const noexcept { return owner_pid & 0x7fffffffU; }
    [[nodiscard]] bool owner_is_guest() const noexcept {
        return (owner_pid & 0x80000000U) != 0;
    }
    QVector<AreaPressure> areas;

    // What the channel is carrying, per second, worked out from two
    // samples of the running totals the server publishes. Zero for a
    // channel nobody has claimed, and zero for an idle one — which are
    // different situations that ``claimed`` tells apart.
    double commands_per_second = 0.0;
    double bytes_per_second = 0.0;
    double frames_per_second = 0.0;

    [[nodiscard]] AreaPressure::State worst() const;

    // The previous sample, kept so the next one has something to
    // subtract from.
    quint64 last_commands = 0;
    quint64 last_bytes = 0;
    quint64 last_frames = 0;
    qint64 last_sampled_ms = 0;
};

// What the render server is telling us about its own congestion.
//
// The server already counts every one of these and already knows what
// size would have avoided the stall: ``ring.space_waits``,
// ``wire.chunked_commands``, ``wire.partial_replies`` and the blob
// allocator's failure counter, and its warnings carry the recommended
// flag value verbatim. None of that was reaching anybody, because it
// went to a log the manager did not read.
//
// So this reads it. The preferred source is a counters block in the
// region header, which costs a load and no round trip; until the server
// publishes one, the same numbers are parsed out of the warnings it
// already prints, which is why the recommendation shown in the manager
// is the server's number rather than one invented here.
class RenderStats : public QObject {
    Q_OBJECT
public:
    explicit RenderStats(QObject* parent = nullptr);

    // Feed one line of the server's output. Lines that are not one of
    // its congestion warnings are ignored.
    void ingest(const QString& line);

    // Sized to the channel count in effect. Existing counters for
    // channels that survive the resize are kept.
    void set_channel_count(int channels);
    void set_area_sizes(int ring_kib, int reply_kib, int blob_mib);

    // Read each region's header for who holds it and how it is carved
    // up. The header is the first page of a file in /dev/shm that the
    // server wrote and both sides map, so this costs an open and a read
    // and asks the server nothing.
    void poll_regions(const QString& region_prefix);

    // Clear a claim a dead process left behind. Refuses while the
    // owning pid is still alive, because taking a channel from a
    // running process corrupts the stream it is in the middle of.
    [[nodiscard]] bool release_channel(const QString& region_prefix, int index);

    [[nodiscard]] const QVector<ChannelPressure>& channels() const noexcept {
        return channels_;
    }
    [[nodiscard]] AreaPressure::State worst() const;
    // The one sentence the overview's warning strip shows, or empty
    // when nothing is congested.
    [[nodiscard]] QString headline() const;

    void reset();

Q_SIGNALS:
    void updated();

private:
    AreaPressure* area(int channel, int index);

    QVector<ChannelPressure> channels_;
};

}  // namespace lsw::host::vm
