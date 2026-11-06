#include "lsw/host/services/host_window_status.hpp"

#include <algorithm>

namespace lsw::host::services {

namespace {

constexpr double kHotSplitsPerSecond = 5.0;

}

QString HostWindowStatus::App::label() const {
    return name.isEmpty() ? QStringLiteral("context %1").arg(context) : name;
}

std::optional<int> HostWindowStatus::App::arena_peak_percent() const {
    if (!arena_bytes || *arena_bytes == 0 || !arena_peak_ring_bytes) {
        return std::nullopt;
    }
    return static_cast<int>(qMin<quint64>(100, *arena_peak_ring_bytes * 100 / *arena_bytes));
}

void HostWindowTracker::clear() {
    status_ = HostWindowStatus{};
    memory_.clear();
    order_.clear();
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
        }
        history.push_back({at_ms, write_back, write_combined});
    }
    while (!history.isEmpty() && at_ms - history.first().at_ms > HostWindowStatus::kHistoryMs) {
        history.removeFirst();
    }
    status_.history = std::move(history);

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
    QHash<quint64, Memory> next;
    QHash<quint64, const platform::AvStateDump::Channel*> channels;
    QVector<quint64> keys;
    for (const auto& context : snapshot.contexts) {
        keys.push_back(HostWindowStatus::key(context.device, context.id));
    }
    for (const auto& channel : snapshot.channels) {
        const auto* owner = snapshot.contexts_with_id(channel.context) == 1 ? snapshot.find_context(channel.context)
                                                                          : nullptr;
        const quint64 key = HostWindowStatus::key(owner != nullptr ? owner->device : HostWindowStatus::kNoDevice,
                                                  channel.context);
        if (owner == nullptr) {
            keys.push_back(key);
        }
        channels.insert(key, &channel);
    }
    for (const quint64 key : keys) {
        const quint32 device = static_cast<quint32>(key >> 32);
        const quint32 id = static_cast<quint32>(key & 0xffffffffULL);
        Memory memory = memory_.value(key);
        const auto* context = device == HostWindowStatus::kNoDevice ? nullptr : snapshot.find_context(device, id);
        const auto* found = channels.value(key, nullptr);
        const bool known = memory.has_counters;
        const auto& before = memory.counters;
        HostWindowStatus::App app;
        app.device = device;
        app.context = id;
        if (context != nullptr) {
            app.name = context->name;
            app.held_bytes = context->bytes();
            app.blobs = context->blobs;
            if (context->batches && memory.batches && seconds > 0) {
                const quint64 batches = *context->batches >= *memory.batches ? *context->batches - *memory.batches : 0;
                app.submits_per_s = static_cast<double>(batches) / seconds;
            }
            memory.batches = context->batches;
        } else {
            memory.batches.reset();
        }
        if (found == nullptr) {
            memory.has_counters = false;
            memory.counters = {};
            memory.hot_since_ms.reset();
            memory.refused_at_ms.reset();
            memory.copied_at_ms.reset();
            memory.arena_full_at_ms.reset();
            next.insert(key, memory);
            status_.apps.push_back(app);
            continue;
        }
        const auto& channel = *found;
        app.has_channel = true;
        const auto delta = [&](quint64 now, quint64 was) { return now >= was ? now - was : quint64{0}; };
        if (known && seconds > 0) {
            const quint64 splits = delta(channel.split_transfers, before.split_transfers);
            const quint64 pieces = delta(channel.split_pieces, before.split_pieces);
            const auto optional_delta = [&](const std::optional<quint64>& now, const std::optional<quint64>& was) {
                return now ? delta(*now, was.value_or(0)) : quint64{0};
            };
            const auto wait = [&](const std::optional<quint64>& count, const std::optional<quint64>& was_count,
                                  const std::optional<quint64>& micros, const std::optional<quint64>& was_micros) {
                HostWindowStatus::Wait result;
                const quint64 waits = optional_delta(count, was_count);
                result.per_s = static_cast<double>(waits) / seconds;
                if (waits > 0) {
                    result.avg_ms = static_cast<double>(optional_delta(micros, was_micros)) / static_cast<double>(waits) / 1000.0;
                }
                return result;
            };
            const quint64 blob_room_waits = optional_delta(channel.blob_room_waits, before.blob_room_waits);
            app.splits_per_s = static_cast<double>(splits) / seconds;
            app.extra_pieces_per_s = static_cast<double>(pieces > splits ? pieces - splits : 0) / seconds;
            app.blob_room_wait = wait(channel.blob_room_waits, before.blob_room_waits, channel.blob_room_wait_us,
                                           before.blob_room_wait_us);
            app.ring_wait = wait(channel.ring_waits, before.ring_waits, channel.ring_wait_us, before.ring_wait_us);
            app.fence_wait = wait(channel.fence_waits, before.fence_waits, channel.fence_wait_us, before.fence_wait_us);
            if (channel.arena_grows && before.arena_grows) {
                app.arena_grows_per_s = static_cast<double>(delta(*channel.arena_grows, *before.arena_grows)) / seconds;
            }
            if (app.splits_per_s > kHotSplitsPerSecond) {
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
            if (blob_room_waits > 0 && channel.arena_bytes && *channel.arena_bytes > 0 && channel.arena_peak_ring_bytes
                && *channel.arena_peak_ring_bytes * 10 >= *channel.arena_bytes * 9) {
                memory.arena_full_at_ms = at_ms;
            }
        }
        memory.counters = channel;
        memory.has_counters = true;
        app.inline_bulk = channel.inline_bulk;
        app.copied_mappings = channel.copied_mappings;
        app.slice_waits = channel.slice_waits;
        app.refusals = channel.refused();
        app.arena_bytes = channel.arena_bytes;
        app.arena_peak_ring_bytes = channel.arena_peak_ring_bytes;
        app.arena_segments = channel.arena_segments;
        app.parked_blobs = channel.parked_blobs;
        app.arena_grows = channel.arena_grows;
        app.arena_full_at_ms = memory.arena_full_at_ms;
        app.hot_split_ms = memory.hot_since_ms ? at_ms - *memory.hot_since_ms : 0;
        app.refused_at_ms = memory.refused_at_ms;
        app.copied_at_ms = memory.copied_at_ms;
        next.insert(key, memory);
        status_.apps.push_back(app);
    }
    memory_ = std::move(next);
    previous_at_ms_ = at_ms;
    order();
}

void HostWindowTracker::order() {
    const auto rate = [](const HostWindowStatus::App& app) { return app.submits_per_s.value_or(0); };
    const auto before = [&](const HostWindowStatus::App& a, const HostWindowStatus::App& b) {
        if (rate(a) != rate(b)) {
            return rate(a) > rate(b);
        }
        if (a.held_bytes != b.held_bytes) {
            return a.held_bytes > b.held_bytes;
        }
        return a.device != b.device ? a.device < b.device : a.context < b.context;
    };
    const auto beats = [&](const HostWindowStatus::App& a, const HostWindowStatus::App& b) {
        return rate(a) > rate(b) * kJumpRatio && rate(a) - rate(b) > kJumpPerSecond;
    };
    QHash<quint64, HostWindowStatus::App> by_key;
    for (const auto& app : status_.apps) {
        by_key.insert(HostWindowStatus::key(app.device, app.context), app);
    }
    QVector<HostWindowStatus::App> ordered;
    for (const quint64 key : std::as_const(order_)) {
        const auto it = by_key.constFind(key);
        if (it != by_key.constEnd()) {
            ordered.push_back(*it);
            by_key.remove(key);
        }
    }
    QVector<HostWindowStatus::App> fresh = by_key.values().toVector();
    std::sort(fresh.begin(), fresh.end(), before);
    for (const auto& app : fresh) {
        qsizetype at = 0;
        while (at < ordered.size() && !before(app, ordered.at(at))) {
            ++at;
        }
        ordered.insert(at, app);
    }
    for (qsizetype i = 1; i < ordered.size(); ++i) {
        for (qsizetype j = i; j > 0 && beats(ordered.at(j), ordered.at(j - 1)); --j) {
            std::swap(ordered[j], ordered[j - 1]);
        }
    }
    order_.clear();
    for (const auto& app : ordered) {
        order_.push_back(HostWindowStatus::key(app.device, app.context));
    }
    status_.apps = std::move(ordered);
}

}
