#include "lsw/host/services/host_window_status.hpp"

#include <algorithm>

namespace lsw::host::services {

namespace {

constexpr double kHotSplitsPerSecond = 5.0;
constexpr qint64 kActiveMs = 30'000;

QString context_label(quint32 context, const QString& name) {
    return name.isEmpty() ? QStringLiteral("context %1").arg(context) : name;
}

}

QString HostWindowStatus::Holder::label() const {
    return context_label(context, name);
}

QString HostWindowStatus::Transfer::label() const {
    return context_label(context, name);
}

std::optional<int> HostWindowStatus::Transfer::arena_peak_percent() const {
    if (!arena_bytes || *arena_bytes == 0 || !arena_peak_ring_bytes) {
        return std::nullopt;
    }
    return static_cast<int>(qMin<quint64>(100, *arena_peak_ring_bytes * 100 / *arena_bytes));
}

void HostWindowTracker::clear() {
    status_ = HostWindowStatus{};
    memory_.clear();
    previous_at_ms_.reset();
    previous_refusals_.reset();
}

void HostWindowTracker::add(const platform::AvStateDump::Snapshot& snapshot, qint64 at_ms) {
    if (!snapshot.device && !snapshot.executor) {
        clear();
        return;
    }
    auto history = std::move(status_.history);
    auto window_refused_at = status_.window_refused_at_ms;
    auto full_since = status_.full_since_ms;
    status_ = HostWindowStatus{};
    status_.read = snapshot.device;
    status_.channels_known = snapshot.executor && !snapshot.channels.isEmpty();
    status_.at_ms = at_ms;

    const auto& stats = snapshot.window_stats;
    status_.window_bytes = snapshot.window_bytes;
    if (!status_.window_bytes && stats && stats->total_pages > 0) {
        status_.window_bytes = stats->total_pages * HostWindowStatus::kPageBytes;
    }
    if (snapshot.device) {
        status_.used_bytes = snapshot.used_bytes();
        quint64 write_back = 0;
        quint64 write_combined = 0;
        for (const auto& context : snapshot.contexts) {
            write_back += context.write_back_bytes;
            write_combined += context.write_combined_bytes;
            if (context.bytes() > 0) {
                status_.holders.push_back({context.id, context.name, context.bytes(), context.blobs});
            }
        }
        history.push_back({at_ms, write_back, write_combined});
    }
    while (!history.isEmpty() && at_ms - history.first().at_ms > HostWindowStatus::kHistoryMs) {
        history.removeFirst();
    }
    status_.history = std::move(history);
    std::sort(status_.holders.begin(), status_.holders.end(),
              [](const HostWindowStatus::Holder& a, const HostWindowStatus::Holder& b) {
                  return a.bytes != b.bytes ? a.bytes > b.bytes : a.context < b.context;
              });

    if (stats && stats->peak_pages_in_use > 0) {
        status_.peak_bytes = stats->peak_pages_in_use * HostWindowStatus::kPageBytes;
    } else if (snapshot.device) {
        quint64 peak = 0;
        for (const auto& sample : status_.history) {
            peak = std::max(peak, sample.write_back + sample.write_combined);
        }
        status_.peak_bytes = peak;
    }

    if (snapshot.device && status_.window_bytes && *status_.window_bytes > 0
        && status_.used_bytes > *status_.window_bytes / 10 * 9) {
        status_.full_since_ms = full_since.value_or(at_ms);
    }

    if (stats) {
        status_.window_refusals = stats->refusals;
        if (previous_refusals_ && stats->refusals > *previous_refusals_) {
            window_refused_at = at_ms;
        }
        previous_refusals_ = stats->refusals;
    }
    status_.window_refused_at_ms = window_refused_at;

    const double seconds =
        previous_at_ms_ && at_ms > *previous_at_ms_ ? static_cast<double>(at_ms - *previous_at_ms_) / 1000.0 : 0.0;
    QHash<quint32, Memory> next;
    for (const auto& channel : snapshot.channels) {
        Memory memory = memory_.value(channel.context);
        const bool known = memory_.contains(channel.context);
        const auto& before = memory.counters;
        HostWindowStatus::Transfer transfer;
        transfer.context = channel.context;
        if (const auto* context = snapshot.find_context(channel.context)) {
            transfer.name = context->name;
        }
        const auto delta = [&](quint64 now, quint64 was) { return now >= was ? now - was : quint64{0}; };
        bool moved = false;
        if (known && seconds > 0) {
            const quint64 splits = delta(channel.split_transfers, before.split_transfers);
            const quint64 pieces = delta(channel.split_pieces, before.split_pieces);
            const quint64 waits = delta(channel.room_waits, before.room_waits);
            const quint64 wait_us = delta(channel.room_wait_us, before.room_wait_us);
            transfer.splits_per_s = static_cast<double>(splits) / seconds;
            transfer.extra_pieces_per_s = static_cast<double>(pieces > splits ? pieces - splits : 0) / seconds;
            transfer.waits_per_s = static_cast<double>(waits) / seconds;
            if (waits > 0) {
                transfer.avg_wait_ms = static_cast<double>(wait_us) / static_cast<double>(waits) / 1000.0;
            }
            if (channel.arena_grows && before.arena_grows) {
                transfer.arena_grows_per_s = static_cast<double>(delta(*channel.arena_grows, *before.arena_grows)) / seconds;
            }
            if (transfer.splits_per_s > kHotSplitsPerSecond) {
                memory.hot_since_ms = memory.hot_since_ms.value_or(*previous_at_ms_);
            } else {
                memory.hot_since_ms.reset();
            }
            if (delta(channel.refused(), before.refused()) > 0) {
                memory.refused_at_ms = at_ms;
            }
            if (delta(channel.copied_mappings, before.copied_mappings) > 0) {
                memory.copied_at_ms = at_ms;
            }
            moved = splits > 0 || waits > 0 || delta(channel.inline_bulk, before.inline_bulk) > 0
                    || delta(channel.copied_mappings, before.copied_mappings) > 0
                    || delta(channel.slice_waits, before.slice_waits) > 0
                    || delta(channel.refused(), before.refused()) > 0;
            if (waits > 0 && channel.arena_bytes && *channel.arena_bytes > 0 && channel.arena_peak_ring_bytes
                && *channel.arena_peak_ring_bytes * 10 >= *channel.arena_bytes * 9) {
                memory.arena_full_at_ms = at_ms;
            }
            if (moved) {
                memory.active_at_ms = at_ms;
            }
        }
        memory.counters = channel;
        transfer.inline_bulk = channel.inline_bulk;
        transfer.copied_mappings = channel.copied_mappings;
        transfer.slice_waits = channel.slice_waits;
        transfer.refusals = channel.refused();
        transfer.arena_bytes = channel.arena_bytes;
        transfer.arena_peak_ring_bytes = channel.arena_peak_ring_bytes;
        transfer.arena_segments = channel.arena_segments;
        transfer.parked_blobs = channel.parked_blobs;
        transfer.arena_grows = channel.arena_grows;
        transfer.arena_full_at_ms = memory.arena_full_at_ms;
        transfer.hot_split_ms = memory.hot_since_ms ? at_ms - *memory.hot_since_ms : 0;
        transfer.refused_at_ms = memory.refused_at_ms;
        transfer.copied_at_ms = memory.copied_at_ms;
        transfer.active = memory.active_at_ms && at_ms - *memory.active_at_ms <= kActiveMs;
        next.insert(channel.context, memory);
        status_.transfers.push_back(transfer);
    }
    memory_ = std::move(next);
    previous_at_ms_ = at_ms;
    std::sort(status_.transfers.begin(), status_.transfers.end(),
              [](const HostWindowStatus::Transfer& a, const HostWindowStatus::Transfer& b) {
                  return a.splits_per_s != b.splits_per_s ? a.splits_per_s > b.splits_per_s
                                                          : a.context < b.context;
              });
}

}
