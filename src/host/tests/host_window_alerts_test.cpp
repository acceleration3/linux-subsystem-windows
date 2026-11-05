#include "lsw/host/services/host_window_alerts.hpp"

#include <QCoreApplication>
#include <cstdio>

using lsw::host::services::HostWindowAlerts;
using lsw::host::services::HostWindowStatus;
using Kind = HostWindowAlerts::Warning::Kind;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

HostWindowStatus status() {
    HostWindowStatus out;
    out.read = true;
    out.at_ms = 100'000;
    return out;
}

HostWindowStatus::Transfer transfer(quint32 context, const char* name = "") {
    HostWindowStatus::Transfer out;
    out.context = context;
    out.name = QString::fromLatin1(name);
    return out;
}

int count(const QVector<HostWindowAlerts::Warning>& warnings, Kind kind) {
    int n = 0;
    for (const auto& warning : warnings) {
        n += warning.kind == kind ? 1 : 0;
    }
    return n;
}

QVector<HostWindowAlerts::Warning> warnings(const HostWindowStatus& s) {
    return HostWindowAlerts::warnings(s, s.at_ms);
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    {
        auto s = status();
        check(warnings(s).isEmpty(), "a quiet status has no warnings");
        s.read = false;
        s.full_since_ms = 0;
        s.window_refused_at_ms = s.at_ms;
        check(warnings(s).isEmpty(), "an unread status has no warnings");
    }
    {
        auto s = status();
        s.full_since_ms = s.at_ms - 4999;
        check(warnings(s).isEmpty(), "full for 4999 ms is not yet a warning");
        s.full_since_ms = s.at_ms - 5000;
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::WindowFull && !w.at(0).critical, "full for 5000 ms warns, not critical");
        check(w.at(0).headline == QStringLiteral("The host window is over 90% full."), "no holder, no name in the headline");
        s.full_since_ms = s.at_ms - 60'000;
        s.holders.push_back({1, QStringLiteral("Big"), 900, 1});
        s.holders.push_back({2, QString{}, 100, 1});
        w = warnings(s);
        check(w.at(0).headline == QStringLiteral("The host window is over 90% full, mostly with Big."),
              "the first holder is named");
        s.holders[0].name.clear();
        check(warnings(s).at(0).headline.contains(QStringLiteral("context 1")), "an unnamed holder is named by context");
    }
    {
        auto s = status();
        s.window_refused_at_ms = s.at_ms - 60'000;
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::Refusals && w.at(0).critical, "a refusal 60 s ago is shown, critical");
        check(w.at(0).headline == QStringLiteral("The host window is refusing allocations."), "without an app the window is named");
        s.window_refused_at_ms = s.at_ms - 60'001;
        check(warnings(s).isEmpty(), "a refusal 60001 ms ago is not shown");
        s.window_refused_at_ms = s.at_ms;
        check(warnings(s).size() == 1, "a refusal right now is shown");
        s.window_refused_at_ms = s.at_ms + 5;
        check(warnings(s).size() == 1, "a stamp slightly ahead of now is still recent");
    }
    {
        auto s = status();
        auto a = transfer(1, "Game");
        a.refused_at_ms = s.at_ms - 60'000;
        auto b = transfer(2);
        b.refused_at_ms = s.at_ms - 1;
        auto c = transfer(3, "Old");
        c.refused_at_ms = s.at_ms - 60'001;
        s.transfers = {a, b, c};
        s.window_refused_at_ms = s.at_ms;
        const auto w = warnings(s);
        check(w.size() == 2 && count(w, Kind::Refusals) == 2, "each recently refused app has its own warning");
        check(w.at(0).headline == QStringLiteral("Game is being refused host window memory.")
                  && w.at(1).headline == QStringLiteral("context 2 is being refused host window memory."),
              "warnings name the app and keep transfer order");
        check(w.at(0).critical && w.at(1).critical, "named refusals are critical");
        s.transfers = {c};
        const auto fallback = warnings(s);
        check(fallback.size() == 1 && fallback.at(0).headline == QStringLiteral("The host window is refusing allocations."),
              "a stale app refusal falls back to the window warning");
    }
    {
        auto s = status();
        auto t = transfer(1, "Game");
        t.hot_split_ms = 9999;
        s.transfers = {t};
        check(warnings(s).isEmpty(), "hot for 9999 ms is not yet a warning");
        s.transfers[0].hot_split_ms = 10'000;
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::SplitTransfers && !w.at(0).critical, "hot for 10 s warns");
        check(w.at(0).headline == QStringLiteral("Game's transfers need several trips through the host window."),
              "the split warning names the app");
    }
    {
        auto s = status();
        auto t = transfer(4);
        t.copied_at_ms = s.at_ms - 60'000;
        s.transfers = {t};
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::CopiedMappings && !w.at(0).critical, "copies 60 s ago warn");
        check(w.at(0).headline.startsWith(QStringLiteral("context 4's dynamic buffers")), "the copy warning names the app");
        s.transfers[0].copied_at_ms = s.at_ms - 60'001;
        check(warnings(s).isEmpty(), "copies 60001 ms ago do not warn");
        s.transfers[0].copied_at_ms.reset();
        check(warnings(s).isEmpty(), "no copy stamp does not warn");
    }
    {
        auto s = status();
        auto t = transfer(5, "Paint");
        t.arena_full_at_ms = s.at_ms - 60'000;
        s.transfers = {t};
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::ArenaFull && !w.at(0).critical, "an arena full 60 s ago warns");
        check(w.at(0).headline == QStringLiteral("Paint's transfer arena is nearly full, so its uploads wait for room."),
              "the arena warning names the app");
        s.transfers[0].arena_full_at_ms = s.at_ms - 60'001;
        check(warnings(s).isEmpty(), "an arena full 60001 ms ago does not warn");
    }
    {
        auto s = status();
        s.full_since_ms = 0;
        s.window_refused_at_ms = s.at_ms;
        auto a = transfer(1, "A");
        a.hot_split_ms = 20'000;
        a.copied_at_ms = s.at_ms;
        a.arena_full_at_ms = s.at_ms;
        a.refused_at_ms = s.at_ms;
        auto b = transfer(2, "B");
        b.hot_split_ms = 20'000;
        b.copied_at_ms = s.at_ms;
        s.transfers = {a, b};
        const auto w = warnings(s);
        const Kind order[] = {Kind::WindowFull, Kind::Refusals, Kind::SplitTransfers, Kind::SplitTransfers,
                              Kind::CopiedMappings, Kind::CopiedMappings, Kind::ArenaFull};
        bool same = w.size() == 7;
        for (qsizetype i = 0; same && i < w.size(); ++i) {
            same = w.at(i).kind == order[i];
        }
        check(same, "warnings come out by kind, then by transfer");
        check(w.at(2).headline.startsWith(QStringLiteral("A's")) && w.at(3).headline.startsWith(QStringLiteral("B's")),
              "within a kind, transfers keep their order");
        check(count(w, Kind::Refusals) == 1 && w.at(1).headline.startsWith(QStringLiteral("A ")),
              "a named refusal replaces the window-wide one");
        const auto later = HostWindowAlerts::warnings(s, s.at_ms + 60'001);
        check(count(later, Kind::Refusals) == 0 && count(later, Kind::CopiedMappings) == 0
                  && count(later, Kind::ArenaFull) == 0 && count(later, Kind::SplitTransfers) == 2
                  && count(later, Kind::WindowFull) == 1,
              "recency is measured against the time given");
    }
    {
        auto s = status();
        auto at = [&](std::optional<quint64> arena, std::optional<quint64> peak, bool active) {
            auto t = transfer(1);
            t.arena_bytes = arena;
            t.arena_peak_ring_bytes = peak;
            t.active = active;
            s.transfers = {t};
            return HostWindowAlerts::shown_transfers(s).size();
        };
        check(at(100, 74, false) == 0, "an arena peak of 74 percent is not shown");
        check(at(100, 75, false) == 1, "an arena peak of 75 percent is shown");
        check(at(100, 500, false) == 1, "an arena peak over 100 percent is shown");
        check(at(100, 10, true) == 1, "an active transfer is shown");
        check(at(std::nullopt, std::nullopt, false) == 0, "an idle transfer with no arena is not shown");
        check(at(std::nullopt, std::nullopt, true) == 1, "an active transfer with no arena is shown");
        check(at(0, 10, false) == 0, "a zero arena is not shown");
        check(at(100, std::nullopt, false) == 0, "an unknown peak is not shown");
    }
    {
        auto s = status();
        s.transfers = {transfer(1), transfer(2), transfer(3), transfer(4)};
        s.transfers[1].active = true;
        s.transfers[3].arena_bytes = 4;
        s.transfers[3].arena_peak_ring_bytes = 3;
        const auto shown = HostWindowAlerts::shown_transfers(s);
        check(shown.size() == 2 && shown.at(0)->context == 2 && shown.at(1)->context == 4,
              "shown transfers keep their order and point into the status");
        check(HostWindowAlerts::shown_transfers(HostWindowStatus{}).isEmpty(), "an empty status shows nothing");
    }

    return g_failures == 0 ? 0 : 1;
}
