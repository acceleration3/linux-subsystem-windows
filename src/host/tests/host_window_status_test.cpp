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

const HostWindowStatus::App* transfer(const HostWindowTracker& tracker, quint32 id) {
    for (const auto& t : tracker.status().apps) {
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
        check(status.apps.size() == 3, "every context is an app, holding something or not");
        check(status.apps.at(0).context == 2 && status.apps.at(1).context == 1 && status.apps.at(2).context == 3,
              "apps with no submit rate sort by held bytes");
        check(status.apps.at(1).label() == QStringLiteral("A") && status.apps.at(0).label() == QStringLiteral("context 2"),
              "an unnamed app is labelled by its context");
        check(status.apps.at(0).held_bytes == 300 && status.apps.at(1).held_bytes == 150 && status.apps.at(1).blobs == 1,
              "an app carries the bytes and blobs it holds");
        check(!status.apps.at(0).has_channel && !status.apps.at(0).submits_per_s, "an app without a channel or batches has neither");
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
        check(t && near(t->splits_per_s, 0) && !t->blob_room_wait.avg_ms, "a new channel has no rates");
        auto second = executor({channel(1)});
        second.channels[0].split_transfers = 20;
        second.channels[0].split_pieces = 50;
        second.channels[0].blob_room_waits = 4;
        second.channels[0].blob_room_wait_us = 8000;
        second.channels[0].ring_waits = 2;
        second.channels[0].ring_wait_us = 1000;
        second.channels[0].fence_waits = 6;
        second.channels[0].fence_wait_us = 3000;
        tracker.add(second, 2000);
        t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 10) && near(t->extra_pieces_per_s, 15) && near(t->blob_room_wait.per_s, 2),
              "rates are counter deltas per second");
        check(t && t->blob_room_wait.avg_ms && near(*t->blob_room_wait.avg_ms, 2.0), "the average wait is wait time over waits");
        check(t && near(t->ring_wait.per_s, 1) && t->ring_wait.avg_ms && near(*t->ring_wait.avg_ms, 0.5),
              "ring waits have their own rate and average");
        check(t && near(t->fence_wait.per_s, 3) && t->fence_wait.avg_ms && near(*t->fence_wait.avg_ms, 0.5),
              "fence waits have their own rate and average");
        auto third = second;
        third.channels[0].split_transfers = 20;
        tracker.add(third, 3000);
        t = transfer(tracker, 1);
        check(t && near(t->splits_per_s, 0) && !t->blob_room_wait.avg_ms && !t->ring_wait.avg_ms, "an idle interval has zero rates and no wait average");
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
        auto first = device(1000, {context(1, 10), context(2, 20), context(3, 30)});
        first.contexts[0].batches = 100;
        first.contexts[1].batches = 100;
        tracker.add(first, 0);
        check(!transfer(tracker, 1)->submits_per_s, "the first sample has no submit rate");
        auto second = first;
        second.contexts[0].batches = 700;
        second.contexts[1].batches = 100;
        second.contexts[2].batches = 5;
        tracker.add(second, 2000);
        check(transfer(tracker, 1)->submits_per_s && near(*transfer(tracker, 1)->submits_per_s, 300),
              "submits are batch deltas per second");
        check(transfer(tracker, 2)->submits_per_s && near(*transfer(tracker, 2)->submits_per_s, 0), "no new batches is a zero rate");
        check(!transfer(tracker, 3)->submits_per_s, "a context whose batches were unknown has no rate yet");
        check(tracker.status().apps.at(0).context == 1 && tracker.status().apps.at(1).context == 3,
              "apps sort by submit rate, then held bytes");
        auto third = second;
        third.contexts[0].batches = 50;
        tracker.add(third, 3000);
        check(transfer(tracker, 1)->submits_per_s && near(*transfer(tracker, 1)->submits_per_s, 0),
              "a batch count that went backwards reads as no submits");
        auto fourth = third;
        fourth.contexts[0].batches.reset();
        tracker.add(fourth, 4000);
        check(!transfer(tracker, 1)->submits_per_s, "missing batches stay unknown");
        tracker.add(fourth, 4000);
        check(!transfer(tracker, 2)->submits_per_s, "two snapshots at the same time give no submit rate");
    }
    {
        HostWindowTracker tracker;
        auto snap = device(1000, {context(1, 10)});
        snap.executor = true;
        tracker.add(snap, 0);
        snap.channels.push_back(channel(1));
        snap.channels[0].split_transfers = 99;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->has_channel && near(transfer(tracker, 1)->splits_per_s, 0),
              "a channel that appears after its context starts from a baseline");
        snap.channels[0].split_transfers = 109;
        tracker.add(snap, 2000);
        check(near(transfer(tracker, 1)->splits_per_s, 10), "and then has rates");
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
        const auto& rows = tracker.status().apps;
        check(rows.size() == 3 && rows.at(0).context == 1 && rows.at(1).context == 2 && rows.at(2).context == 3,
              "apps with equal submits and held bytes sort by context");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        snap.channels[0].split_transfers = 5;
        tracker.add(snap, 1000);
        check(tracker.status().apps.at(0).hot_split_ms == 0, "exactly 5 splits per second is not hot");
        snap.channels[0].split_transfers = 11;
        tracker.add(snap, 2000);
        check(tracker.status().apps.at(0).hot_split_ms == 1000, "over 5 per second starts hot from the previous sample");
        snap.channels[0].split_transfers = 30;
        tracker.add(snap, 4000);
        check(tracker.status().apps.at(0).hot_split_ms == 3000, "the hot time grows while the rate holds");
        snap.channels[0].split_transfers = 100;
        tracker.add(snap, 12000);
        check(tracker.status().apps.at(0).hot_split_ms == 11000, "hot for over ten seconds is reported as such");
        snap.channels[0].split_transfers = 100;
        tracker.add(snap, 13000);
        check(tracker.status().apps.at(0).hot_split_ms == 0, "a quiet interval resets the hot time");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        snap.channels[0].refused_no_space = 1;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->refused_at_ms == 1000 && transfer(tracker, 1)->refusals == 1, "rising refusals stamp the channel");
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
        snap.channels[0].blob_room_waits = 1;
        tracker.add(snap, 1000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "a peak at 90 percent with a new room wait stamps");
        check(transfer(tracker, 1)->arena_peak_percent() == 90, "the peak percent is reported");
        tracker.add(snap, 2000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "no new waits keep the old stamp");
        snap.channels[0].blob_room_waits = 2;
        snap.channels[0].arena_peak_ring_bytes = 899;
        tracker.add(snap, 3000);
        check(transfer(tracker, 1)->arena_full_at_ms == 1000, "a peak under 90 percent does not stamp");
        snap.channels[0].blob_room_waits = 3;
        snap.channels[0].arena_peak_ring_bytes = 1000;
        tracker.add(snap, 4000);
        check(transfer(tracker, 1)->arena_full_at_ms == 4000, "a full peak with waits stamps again");
        snap.channels[0].blob_room_waits = 4;
        snap.channels[0].arena_bytes.reset();
        tracker.add(snap, 5000);
        check(transfer(tracker, 1)->arena_full_at_ms == 4000 && !transfer(tracker, 1)->arena_peak_percent(),
              "an unknown arena does not stamp");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        snap.channels[0].arena_bytes = 1000;
        snap.channels[0].arena_peak_ring_bytes = 1000;
        tracker.add(snap, 0);
        snap.channels[0].ring_waits = 5;
        snap.channels[0].fence_waits = 5;
        tracker.add(snap, 1000);
        check(!transfer(tracker, 1)->arena_full_at_ms, "ring and fence waits do not stamp the arena alert");
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
        HostWindowStatus::App t;
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
    {
        HostWindowTracker tracker;
        auto snap = device(1000, {context(1, 10), context(2, 10)});
        snap.contexts[0].batches = 0;
        snap.contexts[1].batches = 0;
        tracker.add(snap, 0);
        quint64 a = 0;
        quint64 b = 0;
        bool held = true;
        for (int i = 1; i <= 6; ++i) {
            a += i % 2 ? 62 : 58;
            b += i % 2 ? 58 : 62;
            snap.contexts[0].batches = a;
            snap.contexts[1].batches = b;
            tracker.add(snap, i * 1000);
            held = held && tracker.status().apps.at(0).context == 1 && tracker.status().apps.at(1).context == 2;
        }
        check(held, "two rates that trade places keep their rows in order");
        b += 200;
        a += 60;
        snap.contexts[0].batches = a;
        snap.contexts[1].batches = b;
        tracker.add(snap, 7000);
        check(tracker.status().apps.at(0).context == 2, "a clearly higher rate moves a row up");
        snap.contexts.push_back(context(3, 10));
        snap.contexts.last().batches = 0;
        a += 60;
        b += 200;
        snap.contexts[0].batches = a;
        snap.contexts[1].batches = b;
        tracker.add(snap, 8000);
        check(tracker.status().apps.size() == 3 && tracker.status().apps.at(0).context == 2
                  && tracker.status().apps.at(2).context == 3,
              "a new app is inserted by its rate");
        snap.contexts.removeAt(0);
        tracker.add(snap, 9000);
        check(tracker.status().apps.size() == 2 && tracker.status().apps.at(0).context == 2, "an app that is gone loses its row");
    }
    {
        HostWindowTracker tracker;
        auto snap = executor({channel(1)});
        tracker.add(snap, 0);
        snap.channels[0].split_transfers = 100;
        tracker.add(snap, 1000);
        snap.channels[0].split_transfers = 200;
        tracker.add(snap, 20000);
        check(transfer(tracker, 1)->hot_split_ms == 20000, "a steady hot channel is hot since the sample before its first fast one");
        auto gap = executor({channel(2)});
        tracker.add(gap, 30000);
        auto back = executor({channel(1)});
        back.channels[0].split_transfers = 5000;
        tracker.add(back, 40000);
        tracker.add(back, 41000);
        check(transfer(tracker, 1)->hot_split_ms == 0, "a channel that comes back after a gap starts cold");
        back.channels[0].split_transfers = 5100;
        tracker.add(back, 42000);
        check(transfer(tracker, 1)->hot_split_ms == 1000, "and turns hot only from its own samples");
    }
    {
        HostWindowTracker tracker;
        auto snap = device(1000, {context(1, 10, 0, "A"), context(1, 20, 0, "B")});
        snap.contexts[1].device = 2;
        snap.contexts[0].batches = 0;
        snap.contexts[1].batches = 0;
        tracker.add(snap, 0);
        snap.contexts[0].batches = 100;
        snap.contexts[1].batches = 400;
        tracker.add(snap, 1000);
        const auto& apps = tracker.status().apps;
        check(apps.size() == 2 && apps.at(0).name == QStringLiteral("B") && apps.at(0).held_bytes == 20
                  && apps.at(1).name == QStringLiteral("A") && apps.at(1).held_bytes == 10,
              "contexts with the same id on two devices are two apps");
        check(apps.at(0).submits_per_s && near(*apps.at(0).submits_per_s, 400) && near(*apps.at(1).submits_per_s, 100),
              "each has its own submit rate");
        snap.executor = true;
        snap.channels.push_back(channel(1));
        tracker.add(snap, 2000);
        check(!apps.at(0).has_channel && !apps.at(1).has_channel && apps.size() == 3,
              "a channel whose id is on two devices is not merged with either");
        snap.contexts.removeLast();
        tracker.add(snap, 3000);
        check(apps.size() == 1 && apps.at(0).has_channel, "a unique id is merged with its context");
    }

    return g_failures == 0 ? 0 : 1;
}
