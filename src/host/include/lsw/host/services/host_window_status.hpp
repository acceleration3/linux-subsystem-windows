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

    struct Holder {
        quint32 context = 0;
        QString name;
        quint64 bytes = 0;
        int blobs = 0;

        [[nodiscard]] QString label() const;
    };

    struct Transfer {
        quint32 context = 0;
        QString name;
        double splits_per_s = 0;
        double extra_pieces_per_s = 0;
        double waits_per_s = 0;
        std::optional<double> avg_wait_ms;
        quint64 inline_bulk = 0;
        quint64 copied_mappings = 0;
        quint64 slice_waits = 0;
        quint64 refusals = 0;
        std::optional<quint64> arena_bytes;
        std::optional<quint64> arena_peak_ring_bytes;
        std::optional<qint64> arena_full_at_ms;
        bool active = false;
        qint64 hot_split_ms = 0;
        std::optional<qint64> refused_at_ms;
        std::optional<qint64> copied_at_ms;

        [[nodiscard]] QString label() const;
        [[nodiscard]] std::optional<int> arena_peak_percent() const;
    };

    static constexpr qint64 kHistoryMs = 10 * 60 * 1000;
    static constexpr quint64 kPageBytes = 4096;

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
    QVector<Holder> holders;
    QVector<Transfer> transfers;
};

class HostWindowTracker {
public:
    void add(const platform::AvStateDump::Snapshot& snapshot, qint64 at_ms);

    void clear();

    [[nodiscard]] const HostWindowStatus& status() const noexcept { return status_; }

private:
    struct Memory {
        platform::AvStateDump::Channel counters;
        std::optional<qint64> hot_since_ms;
        std::optional<qint64> refused_at_ms;
        std::optional<qint64> copied_at_ms;
        std::optional<qint64> arena_full_at_ms;
        std::optional<qint64> active_at_ms;
    };

    HostWindowStatus status_;
    QHash<quint32, Memory> memory_;
    std::optional<qint64> previous_at_ms_;
    std::optional<quint64> previous_refusals_;
};

}
