#pragma once

#include "lsw/host/platform/av_state_dump.hpp"

#include <QHash>
#include <QString>
#include <QVector>
#include <optional>

namespace lsw::host::services {

struct HostWindowStatus {
    struct Sample {
        qint64 at_ms = 0;
        quint64 write_back = 0;
        quint64 write_combined = 0;
    };

    struct Wait {
        double per_s = 0;
        std::optional<double> avg_ms;
    };

    struct App {
        quint32 device = 0;
        quint32 context = 0;
        QString name;
        quint64 held_bytes = 0;
        int blobs = 0;
        std::optional<double> submits_per_s;
        bool has_channel = false;
        double splits_per_s = 0;
        double extra_pieces_per_s = 0;
        Wait blob_room_wait;
        Wait ring_wait;
        Wait fence_wait;
        quint64 inline_bulk = 0;
        quint64 copied_mappings = 0;
        quint64 slice_waits = 0;
        quint64 refusals = 0;
        std::optional<quint64> arena_bytes;
        std::optional<quint64> arena_peak_ring_bytes;
        std::optional<quint64> arena_segments;
        std::optional<quint64> parked_blobs;
        std::optional<quint64> arena_grows;
        std::optional<double> arena_grows_per_s;
        std::optional<qint64> arena_full_at_ms;
        qint64 hot_split_ms = 0;
        std::optional<qint64> refused_at_ms;
        std::optional<qint64> copied_at_ms;

        [[nodiscard]] QString label() const;
        [[nodiscard]] std::optional<int> arena_peak_percent() const;
    };

    static constexpr qint64 kHistoryMs = 10 * 60 * 1000;
    static constexpr quint64 kPageBytes = 4096;
    static constexpr quint32 kNoDevice = 0xffffffffU;

    [[nodiscard]] static constexpr quint64 key(quint32 device, quint32 context) noexcept {
        return (static_cast<quint64>(device) << 32) | context;
    }

    bool read = false;
    bool channels_known = false;
    qint64 at_ms = 0;
    std::optional<quint64> window_bytes;
    quint64 used_bytes = 0;
    std::optional<quint64> peak_bytes;
    quint64 window_refusals = 0;
    std::optional<qint64> window_refused_at_ms;
    std::optional<qint64> full_since_ms;
    QVector<Sample> history;
    QVector<App> apps;
};

class HostWindowTracker {
public:
    void add(const platform::AvStateDump::Snapshot& snapshot, qint64 at_ms);

    void clear();

    [[nodiscard]] const HostWindowStatus& status() const noexcept { return status_; }

private:
    struct Memory {
        platform::AvStateDump::Channel counters;
        bool has_counters = false;
        std::optional<quint64> batches;
        std::optional<qint64> hot_since_ms;
        std::optional<qint64> refused_at_ms;
        std::optional<qint64> copied_at_ms;
        std::optional<qint64> arena_full_at_ms;
    };

    static constexpr double kJumpRatio = 1.2;
    static constexpr double kJumpPerSecond = 1.0;

    void order();

    HostWindowStatus status_;
    QHash<quint64, Memory> memory_;
    QVector<quint64> order_;
    std::optional<qint64> previous_at_ms_;
    std::optional<quint64> previous_refusals_;
};

}
