#include "lsw/host/services/host_window_status.hpp"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

using lsw::host::platform::AvStateDump;
using lsw::host::services::HostWindowStatus;
using lsw::host::services::HostWindowTracker;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

AvStateDump::Snapshot device(quint64 window, std::initializer_list<AvStateDump::Context> contexts = {}) {
    AvStateDump::Snapshot snap;
    snap.device = true;
    snap.window_bytes = window;
    for (const auto& context : contexts) {
        snap.contexts.push_back(context);
    }
    return snap;
}

AvStateDump::Context context(quint32 id, quint64 write_back, quint64 write_combined = 0, const char* name = "") {
    AvStateDump::Context out;
    out.id = id;
    out.name = QString::fromLatin1(name);
    out.write_back_bytes = write_back;
    out.write_combined_bytes = write_combined;
    out.blobs = 1;
    return out;
}

AvStateDump::Channel channel(quint32 id) {
    AvStateDump::Channel out;
    out.context = id;
    return out;
}

AvStateDump::Snapshot executor(std::initializer_list<AvStateDump::Channel> channels) {
    AvStateDump::Snapshot snap;
    snap.executor = true;
    for (const auto& c : channels) {
        snap.channels.push_back(c);
    }
    return snap;
}

const HostWindowStatus::Transfer* transfer(const HostWindowTracker& tracker, quint32 id) {
    for (const auto& t : tracker.status().transfers) {
        if (t.context == id) {
            return &t;
        }
    }
    return nullptr;
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    {
        HostWindowTracker tracker;
        tracker.add(AvStateDump::Snapshot{}, 1000);
        check(!tracker.status().read && tracker.status().history.isEmpty(), "a snapshot with neither module is not read");
    }
    {
        HostWindowTracker tracker;
        tracker.add(device(1000, {context(1, 100, 50, "A"), context(2, 300), context(3, 0)}), 1000);
        const auto& status = tracker.status();
        check(status.read && !status.channels_known && status.at_ms == 1000, "a device snapshot is read");
        check(status.window_bytes == 1000 && status.used_bytes == 450, "window and used bytes");
        check(status.holders.size() == 2, "contexts holding nothing are not holders");
        check(status.holders.at(0).context == 2 && status.holders.at(1).context == 1, "holders sort by bytes");
        check(status.holders.at(1).label() == QStringLiteral("A") && status.holders.at(0).label() == QStringLiteral("context 2"),
              "an unnamed holder is labelled by its context");
        check(status.history.size() == 1 && status.history.at(0).write_back == 400 && status.history.at(0).write_combined == 50,
              "history records the split by caching");
        check(status.peak_bytes == 450, "the peak falls back to the history maximum");
        tracker.clear();
        check(!tracker.status().read && tracker.status().history.isEmpty(), "clear forgets everything");
    }
    {
        HostWindowTracker tracker;
        auto snap = device(0, {context(1, 4096)});
        snap.window_bytes.reset();
        AvStateDump::WindowStats stats;
        stats.total_pages = 10;
        stats.peak_pages_in_use = 3;
        snap.window_stats = stats;
        tracker.add(snap, 0);
        check(tracker.status().window_bytes == 40960, "window_stats total_pages stands in for host_window_bytes");
        check(tracker.status().peak_bytes == 3 * 4096, "the peak comes from peak_pages_in_use");
        snap.window_bytes = 5000;
        tracker.add(snap, 1000);
        check(tracker.status().window_bytes == 5000, "host_window_bytes wins over window_stats");
        snap.window_bytes.reset();
        snap.window_stats->total_pages = 0;
        tracker.add(snap, 2000);
        check(!tracker.status().window_bytes, "no window size when both are missing");
    }
    {
        HostWindowTracker tracker;
        tracker.add(device(1000, {context(1, 100)}), 0);
        tracker.add(device(1000, {context(1, 700)}), 1000);
        tracker.add(device(1000, {context(1, 200)}), 2000);
        check(tracker.status().peak_bytes == 700, "the history peak survives a drop");
    }
    {
        HostWindowTracker tracker;
        tracker.add(device(1000, {context(1, 10)}), 0);
        tracker.add(device(1000, {context(1, 20)}), 1);
        tracker.add(device(1000, {context(1, 30)}), 600'000);
        check(tracker.status().history.size() == 3, "history keeps samples exactly ten minutes old");
        tracker.add(device(1000, {context(1, 40)}), 600'001);
        check(tracker.status().history.size() == 3 && tracker.status().history.first().at_ms == 1,
              "history drops samples older than ten minutes");
        tracker.add(device(1000, {context(1, 50)}), 2'000'000);
        check(tracker.status().history.size() == 1 && tracker.status().history.first().write_back == 50,
              "a long gap leaves only the new sample");
        check(tracker.status().peak_bytes == 50, "the fallback peak covers only retained history");
    }
    {
        HostWindowTracker tracker;
        tracker.add(executor({channel(1)}), 0);
        check(tracker.status().read == false && tracker.status().channels_known, "an executor-only snapshot has channels");
        check(tracker.status().history.isEmpty() && !tracker.status().peak_bytes, "an executor-only snapshot has no window history");
    }
    {
        HostWindowTracker tracker;
        tracker.add(device(1000, {context(1, 900)}), 0);
        check(!tracker.status().full_since_ms, "exactly 90 percent is not full");
        tracker.add(device(1000, {context(1, 901)}), 1000);
        check(tracker.status().full_since_ms == 1000, "over 90 percent starts the full timer");
        tracker.add(device(1000, {context(1, 950)}), 4000);
        check(tracker.status().full_since_ms == 1000, "the full timer keeps its start while over");
        tracker.add(device(1000, {context(1, 100)}), 5000);
        check(!tracker.status().full_since_ms, "dropping below 90 percent clears the timer");
        tracker.add(device(1000, {context(1, 950)}), 6000);
        check(tracker.status().full_since_ms == 6000, "the timer restarts after a drop");
        tracker.add(device(0, {context(1, 950)}), 7000);
        check(!tracker.status().full_since_ms, "an unknown window is never full");
    }
    {
        HostWindowTracker tracker;
        auto snap = device(1000);
        AvStateDump::WindowStats stats;
        stats.refusals = 5;
        snap.window_stats = stats;
        tracker.add(snap, 0);
        check(tracker.status().window_refusals == 5 && !tracker.status().window_refused_at_ms,
              "the first refusal count is a baseline");
        tracker.add(snap, 1000);
        check(!tracker.status().window_refused_at_ms, "a steady refusal count raises no alert");
        snap.window_stats->refusals = 6;
        tracker.add(snap, 2000);
        check(tracker.status().window_refused_at_ms == 2000, "rising refusals stamp the time");
        snap.window_stats->refusals = 6;
        tracker.add(snap, 3000);
        check(tracker.status().window_refused_at_ms == 2000, "the stamp stays until refusals rise again");
        snap.window_stats->refusals = 2;
        tracker.add(snap, 4000);
        check(tracker.status().window_refused_at_ms == 2000, "a falling count is not a refusal");
        snap.window_stats->refusals = 3;
        tracker.add(snap, 5000);
        check(tracker.status().window_refused_at_ms == 5000, "a rise after a reset counts again");
    }
    {
        HostWindowTracker tracker;
        auto first = executor({channel(1)});
        tracker.add(first, 0);
        const auto* t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 0) && !t->avg_wait_ms && !t->active, "a new channel has no rates");
        auto second = executor({channel(1)});
        second.channels[0].split_transfers = 20;
        second.channels[0].split_pieces = 50;
        second.channels[0].room_waits = 4;
        second.channels[0].room_wait_us = 8000;
        tracker.add(second, 2000);
        t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 10) && near(t->extra_pieces_per_s, 15) && near(t->waits_per_s, 2),
              "rates are counter deltas per second");
        check(t && t->avg_wait_ms && near(*t->avg_wait_ms, 2.0), "the average wait is wait time over waits");
        check(t && t->active, "movement makes a channel active");
        auto third = second;
        third.channels[0].split_transfers = 20;
        tracker.add(third, 3000);
        t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 0) && !t->avg_wait_ms, "an idle interval has zero rates and no wait average");
        auto fourth = third;
        fourth.channels[0].split_transfers = 5;
        fourth.channels[0].split_pieces = 1;
        tracker.add(fourth, 4000);
        t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 0) && near(t->extra_pieces_per_s, 0),
              "a counter that went backwards counts as no change");
    }
    {
        HostWindowTracker tracker;
        tracker.add(executor({channel(1)}), 1000);
        auto snap = executor({channel(1)});
        snap.channels[0].split_transfers = 1;
        tracker.add(snap, 1000);
        check(near(transfer(tracker, 1)->splits_per_s, 0), "two snapshots at the same time give no rates");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1), channel(2), channel(3)});
        tracker.add(snap, 0);
        snap.channels[0].split_transfers = 5;
        snap.channels[1].split_transfers = 30;
        snap.channels[2].split_transfers = 30;
        tracker.add(snap, 1000);
        const auto& rows = tracker.status().transfers;
        check(rows.size() == 3 && rows.at(0).context == 2 && rows.at(1).context == 3 && rows.at(2).context == 1,
              "transfers sort by split rate, then context");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        snap.channels[0].split_transfers = 5;
        tracker.add(snap, 1000);
        check(tracker.status().transfers.at(0).hot_split_ms == 0, "exactly 5 splits per second is not hot");
        snap.channels[0].split_transfers = 11;
        tracker.add(snap, 2000);
        check(tracker.status().transfers.at(0).hot_split_ms == 1000, "over 5 per second starts hot from the previous sample");
        snap.channels[0].split_transfers = 30;
        tracker.add(snap, 4000);
        check(tracker.status().transfers.at(0).hot_split_ms == 3000, "the hot time grows while the rate holds");
        snap.channels[0].split_transfers = 100;
        tracker.add(snap, 12000);
        check(tracker.status().transfers.at(0).hot_split_ms == 11000, "hot for over ten seconds is reported as such");
        snap.channels[0].split_transfers = 100;
        tracker.add(snap, 13000);
        check(tracker.status().transfers.at(0).hot_split_ms == 0, "a quiet interval resets the hot time");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        snap.channels[0].refused_no_space = 1;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->refused_at_ms == 1000 && transfer(tracker, 1)->refusals == 1, "rising refusals stamp the channel");
        check(transfer(tracker, 1)->active, "a refusal makes the channel active");
        tracker.add(snap, 2000);
        check(transfer(tracker, 1)->refused_at_ms == 1000, "the refusal stamp persists");
        snap.channels[0].refused_other = 2;
        tracker.add(snap, 3000);
        check(transfer(tracker, 1)->refused_at_ms == 3000, "any refusal reason counts");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        tracker.add(snap, 1000);
        check(!transfer(tracker, 1)->copied_at_ms, "no copied mappings, no stamp");
        snap.channels[0].copied_mappings = 3;
        tracker.add(snap, 2000);
        check(transfer(tracker, 1)->copied_at_ms == 2000 && transfer(tracker, 1)->copied_mappings == 3,
              "rising copied mappings stamp the channel");
        tracker.add(snap, 3000);
        check(transfer(tracker, 1)->copied_at_ms == 2000, "the copied stamp persists");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        snap.channels[0].arena_bytes = 1000;
        snap.channels[0].arena_peak_ring_bytes = 900;
        tracker.add(snap, 0);
        check(!transfer(tracker, 1)->arena_full_at_ms, "the first sample never raises the arena alert");
        snap.channels[0].room_waits = 1;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "a peak at 90 percent with a new room wait stamps");
        check(transfer(tracker, 1)->arena_peak_percent() == 90, "the peak percent is reported");
        tracker.add(snap, 2000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "no new waits keep the old stamp");
        snap.channels[0].room_waits = 2;
        snap.channels[0].arena_peak_ring_bytes = 899;
        tracker.add(snap, 3000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "a peak under 90 percent does not stamp");
        snap.channels[0].room_waits = 3;
        snap.channels[0].arena_peak_ring_bytes = 1000;
        tracker.add(snap, 4000);
        check(transfer(tracker, 1)->arena_full_at_ms == 4000, "a full peak with waits stamps again");
        snap.channels[0].room_waits = 4;
        snap.channels[0].arena_bytes.reset();
        tracker.add(snap, 5000);
        check(transfer(tracker, 1)->arena_full_at_ms == 4000 && !transfer(tracker, 1)->arena_peak_percent(),
              "an unknown arena does not stamp");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        snap.channels[0].arena_grows = 2;
        snap.channels[0].arena_segments = 3;
        snap.channels[0].parked_blobs = 1;
        tracker.add(snap, 0);
        check(!transfer(tracker, 1)->arena_grows_per_s, "the first sample has no grow rate");
        check(transfer(tracker, 1)->arena_segments == 3 && transfer(tracker, 1)->parked_blobs == 1
                  && transfer(tracker, 1)->arena_grows == 2,
              "segments, parked blobs and grows are carried");
        snap.channels[0].arena_grows = 6;
        tracker.add(snap, 2000);
        check(transfer(tracker, 1)->arena_grows_per_s == 2.0, "grows are a per second rate from deltas");
        tracker.add(snap, 3000);
        check(transfer(tracker, 1)->arena_grows_per_s == 0.0, "no new grows is a zero rate");
        snap.channels[0].arena_grows.reset();
        snap.channels[0].arena_segments.reset();
        tracker.add(snap, 4000);
        check(!transfer(tracker, 1)->arena_grows_per_s && !transfer(tracker, 1)->arena_segments,
              "missing grows and segments stay unknown");
    }
    {
        HostWindowStatus::Transfer t;
        check(!t.arena_peak_percent(), "no arena, no percent");
        t.arena_bytes = 0;
        t.arena_peak_ring_bytes = 5;
        check(!t.arena_peak_percent(), "a zero arena has no percent");
        t.arena_bytes = 100;
        t.arena_peak_ring_bytes = 250;
        check(t.arena_peak_percent() == 100, "the percent is capped at 100");
        t.arena_peak_ring_bytes = 74;
        check(t.arena_peak_percent() == 74, "the percent rounds down");
        t.context = 4;
        check(t.label() == QStringLiteral("context 4"), "an unnamed transfer is labelled by its context");
        t.name = QStringLiteral("Game");
        check(t.label() == QStringLiteral("Game"), "a named transfer shows its name");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        snap.channels[0].room_waits = 0;
        tracker.add(snap, 0);
        snap.channels[0].inline_bulk = 1;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->active, "an inline bulk movement makes it active");
        tracker.add(snap, 31000);
        check(transfer(tracker, 1)->active, "active lasts 30 seconds");
        tracker.add(snap, 31001);
        check(!transfer(tracker, 1)->active, "and ends just after");
        snap.channels[0].slice_waits = 1;
        tracker.add(snap, 32000);
        check(transfer(tracker, 1)->active, "a slice wait revives it");
    }
    {
        HostWindowTracker tracker;
        auto snap = device(1000, {context(1, 10, 0, "Named")});
        snap.executor = true;
        snap.channels.push_back(channel(1));
        snap.channels.push_back(channel(2));
        tracker.add(snap, 0);
        check(transfer(tracker, 1)->name == QStringLiteral("Named") && transfer(tracker, 2)->name.isEmpty(),
              "a transfer takes the name of its context");
        check(tracker.status().read && tracker.status().channels_known, "device and executor together are both known");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1), channel(2)});
        tracker.add(snap, 0);
        snap.channels.removeLast();
        snap.channels[0].split_transfers = 1;
        tracker.add(snap, 1000);
        check(!transfer(tracker, 2), "a channel that disappeared is dropped");
        snap.channels.push_back(channel(2));
        snap.channels[1].split_transfers = 50;
        tracker.add(snap, 2000);
        check(near(transfer(tracker, 2)->splits_per_s, 0), "a returning channel starts from a baseline");
    }
    {
        HostWindowTracker tracker;
        tracker.add(executor({channel(1)}), 0);
        tracker.add(AvStateDump::Snapshot{}, 1000);
        auto snap = executor({channel(1)});
        snap.channels[0].split_transfers = 99;
        tracker.add(snap, 2000);
        check(near(transfer(tracker, 1)->splits_per_s, 0), "an empty snapshot resets the baselines");
    }

    return g_failures == 0 ? 0 : 1;
}
