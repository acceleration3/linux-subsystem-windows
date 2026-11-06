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

HostWindowStatus::App transfer(quint32 context, const char* name = "") {
    HostWindowStatus::App out;
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
        auto big = transfer(1, "Big");
        big.held_bytes = 900;
        auto small = transfer(2);
        small.held_bytes = 100;
        s.apps = {small, big};
        w = warnings(s);
        check(w.at(0).headline == QStringLiteral("The host window is over 90% full, mostly with Big."),
              "the app holding the most is named");
        s.apps[1].name.clear();
        check(warnings(s).at(0).headline.contains(QStringLiteral("context 1")), "an unnamed app is named by context");
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
        s.apps = {a, b, c};
        s.window_refused_at_ms = s.at_ms;
        const auto w = warnings(s);
        check(w.size() == 2 && count(w, Kind::Refusals) == 2, "each recently refused app has its own warning");
        check(w.at(0).headline == QStringLiteral("Game is being refused host window memory.")
                  && w.at(1).headline == QStringLiteral("context 2 is being refused host window memory."),
              "warnings name the app and keep transfer order");
        check(w.at(0).critical && w.at(1).critical, "named refusals are critical");
        s.apps = {c};
        const auto fallback = warnings(s);
        check(fallback.size() == 1 && fallback.at(0).headline == QStringLiteral("The host window is refusing allocations."),
              "a stale app refusal falls back to the window warning");
    }
    {
        auto s = status();
        auto t = transfer(1, "Game");
        t.hot_split_ms = 9999;
        s.apps = {t};
        check(warnings(s).isEmpty(), "hot for 9999 ms is not yet a warning");
        s.apps[0].hot_split_ms = 10'000;
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::SplitTransfers && !w.at(0).critical, "hot for 10 s warns");
        check(w.at(0).headline == QStringLiteral("Game's transfers need several trips through the host window."),
              "the split warning names the app");
    }
    {
        auto s = status();
        auto t = transfer(4);
        t.copied_at_ms = s.at_ms - 60'000;
        s.apps = {t};
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::CopiedMappings && !w.at(0).critical, "copies 60 s ago warn");
        check(w.at(0).headline.startsWith(QStringLiteral("context 4's dynamic buffers")), "the copy warning names the app");
        s.apps[0].copied_at_ms = s.at_ms - 60'001;
        check(warnings(s).isEmpty(), "copies 60001 ms ago do not warn");
        s.apps[0].copied_at_ms.reset();
        check(warnings(s).isEmpty(), "no copy stamp does not warn");
    }
    {
        auto s = status();
        auto t = transfer(5, "Paint");
        t.arena_full_at_ms = s.at_ms - 60'000;
        s.apps = {t};
        auto w = warnings(s);
        check(w.size() == 1 && w.at(0).kind == Kind::ArenaFull && !w.at(0).critical, "an arena full 60 s ago warns");
        check(w.at(0).headline == QStringLiteral("Paint's transfer arena is nearly full, so its uploads wait for room."),
              "the arena warning names the app");
        s.apps[0].arena_full_at_ms = s.at_ms - 60'001;
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
        s.apps = {a, b};
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
        auto a = transfer(1, "A");
        a.copied_at_ms = s.at_ms;
        auto b = transfer(2, "B");
        b.copied_at_ms = s.at_ms;
        auto c = transfer(2, "C");
        c.device = 1;
        c.copied_at_ms = s.at_ms;
        s.apps = {c, b, a};
        const auto w = warnings(s);
        check(w.size() == 3 && w.at(0).headline.startsWith(QStringLiteral("A's")) && w.at(1).headline.startsWith(QStringLiteral("B's"))
                  && w.at(2).headline.startsWith(QStringLiteral("C's")),
              "warnings come out in device and context order, whatever order the apps are listed in");
    }

    return g_failures == 0 ? 0 : 1;
}
