#include "lsw/host/platform/av_state_dump.hpp"

#include <QCoreApplication>
#include <cstdio>

using lsw::host::platform::AvStateDump;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

AvStateDump::Snapshot parsed(const QByteArray& dump) {
    AvStateDump::Snapshot snapshot;
    AvStateDump::parse(dump, snapshot);
    return snapshot;
}

QByteArray blob(int index, const QByteArray& body) {
    return "module/device/device/1/host_blobs/1/host_blob/" + QByteArray::number(index) + " " + body + "\n";
}

QByteArray channel(int id, const QByteArray& body) {
    return "executor/1/session/1/channel/" + QByteArray::number(id) + " " + body + "\n";
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    {
        QString path;
        const auto values = AvStateDump::fields("a/b/c k=v  x=1 =bad noequals y=", &path);
        check(path == QStringLiteral("a/b/c"), "the first token is the path");
        check(values.value(QStringLiteral("k")) == QStringLiteral("v"), "key=value splits on spaces");
        check(values.value(QStringLiteral("x")) == QStringLiteral("1"), "empty tokens are skipped");
        check(!values.contains(QString{}) && !values.contains(QStringLiteral("noequals")),
              "a token without a key or an equals is dropped");
        check(values.contains(QStringLiteral("y")) && values.value(QStringLiteral("y")).isEmpty(),
              "an empty value is kept");
        check(values.size() == 3, "no other fields appear");
    }
    {
        QString path;
        const auto bare = AvStateDump::fields("only/path", &path);
        check(path == QStringLiteral("only/path") && bare.isEmpty(), "a line with no fields has only a path");
        check(AvStateDump::fields("p k=v", nullptr).size() == 1, "a null path pointer is accepted");
    }
    {
        const auto values = AvStateDump::fields("p a=b=c d\\=e=f g\\\\=h", nullptr);
        check(values.value(QStringLiteral("a")) == QStringLiteral("b=c"), "the value may contain = after the first");
        check(!values.contains(QStringLiteral("d")) && values.value(QStringLiteral("d\\=e")) == QStringLiteral("f"),
              "an escaped = does not split the key");
        check(values.value(QStringLiteral("g\\\\")) == QStringLiteral("h"), "an escaped backslash leaves the next = unescaped");
    }
    {
        const auto values = AvStateDump::fields("p a=x\\ny b=x\\rz c=1\\=2 d=back\\\\slash e=sp\\x20ace f=\\x41\\x5a "
                                                "g=\\xZZ h=\\q i=end\\ j=\\x4 k=\\x1f\\x7f",
                                                nullptr);
        check(values.value(QStringLiteral("a")) == QStringLiteral("x y"), "\\n unescapes and becomes a space");
        check(values.value(QStringLiteral("b")) == QStringLiteral("x z"), "\\r unescapes and becomes a space");
        check(values.value(QStringLiteral("c")) == QStringLiteral("1=2"), "\\= unescapes");
        check(values.value(QStringLiteral("d")) == QStringLiteral("back\\slash"), "\\\\ unescapes");
        check(values.value(QStringLiteral("e")) == QStringLiteral("sp ace"), "\\x20 unescapes to a space");
        check(values.value(QStringLiteral("f")) == QStringLiteral("AZ"), "\\xHH unescapes in either case");
        check(values.value(QStringLiteral("g")) == QStringLiteral("xZZ"), "a bad \\x escape keeps the letter");
        check(values.value(QStringLiteral("h")) == QStringLiteral("q"), "an unknown escape keeps the character");
        check(values.value(QStringLiteral("i")) == QStringLiteral("end\\"), "a trailing backslash stays");
        check(values.value(QStringLiteral("j")) == QStringLiteral("x4"), "a short \\x escape keeps the letter");
        check(values.value(QStringLiteral("k")).isEmpty(), "control characters become spaces and are trimmed");
    }
    {
        const QByteArray longValue(2000, 'a');
        const auto values = AvStateDump::fields("p v=" + longValue, nullptr);
        check(values.value(QStringLiteral("v")).size() == 512, "a value is capped at 512 bytes");
        const auto escaped = AvStateDump::fields("p v=" + QByteArray("\\x41").repeated(600), nullptr);
        check(escaped.value(QStringLiteral("v")).size() == 512 && escaped.value(QStringLiteral("v")).at(0) == QLatin1Char('A'),
              "escapes are capped at 512 bytes too");
        const auto longKey = AvStateDump::fields("p " + QByteArray(65, 'k') + "=1 " + QByteArray(64, 'k') + "=2", nullptr);
        check(longKey.size() == 1 && longKey.value(QString(64, QLatin1Char('k'))) == QStringLiteral("2"),
              "a key over 64 bytes is dropped");
        QString path;
        const auto capped = AvStateDump::fields(QByteArray(1000, 'p') + " a=1", &path);
        check(path.size() == 256 && capped.size() == 1, "the path is capped at 256 bytes");
    }

    {
        const auto snap = parsed(
            "module/device/device/1 host_window_bytes=1000\n"
            "module/device/device/2 host_window_bytes=500\n");
        check(snap.device && !snap.executor, "device lines mark the device");
        check(snap.window_bytes == 1500, "host_window_bytes sums across devices");
    }
    {
        const auto snap = parsed(
            "module/device/device/1 host_window_bytes=18446744073709551616\n"
            "module/device/device/2 host_window_bytes=12a\n"
            "module/device/device/3 host_window_bytes=-5\n"
            "module/device/device/4 host_window_bytes=\n"
            "module/device/device/5 host_window_bytes=123456789012345678901\n");
        check(snap.device && !snap.window_bytes, "overflowing, non-digit, signed, empty and long numbers are missing");
        check(parsed("module/device/device/1 host_window_bytes=18446744073709551615\n").window_bytes
                  == std::optional<quint64>{18446744073709551615ULL},
              "the largest 64-bit number is accepted");
        check(parsed("module/device/device/1 host_window_bytes=0\n").window_bytes == std::optional<quint64>{0},
              "zero is a number");
        check(!parsed("module/device/device/1 host_window_bytes= 5\n").window_bytes, "a space ends the value");
        check(!parsed("module/device/device/1 host_window_bytes=+5\n").window_bytes, "a plus sign is not a digit");
        check(!parsed("module/device/device/1 host_window_bytes=0x10\n").window_bytes, "hex is not a number");
    }
    {
        QByteArray dump;
        dump += blob(1, "context=7 size=4096 caching=0 mapped=true");
        dump += blob(2, "context=7 size=8192 caching=2 mapped=true");
        dump += blob(3, "context=7 size=100 caching=1 mapped=true");
        dump += blob(4, "context=7 size=999 caching=0 mapped=false");
        dump += blob(5, "context=7 size=999 caching=0");
        dump += blob(6, "context=7 caching=0 mapped=true");
        dump += blob(7, "size=5 caching=0 mapped=true");
        dump += blob(8, "context=4294967296 size=5 caching=0 mapped=true");
        dump += blob(9, "context=8 size=64 mapped=true");
        dump += blob(10, "context=8 size=64 caching=2 mapped=true");
        dump += blob(11, "context=7 size=x caching=0 mapped=true");
        const auto snap = parsed(dump);
        check(snap.contexts.size() == 2, "only mapped blobs with a context and a size count");
        const auto* seven = snap.find_context(7);
        check(seven && seven->write_back_bytes == 4196 && seven->write_combined_bytes == 8192 && seven->blobs == 3,
              "blobs sum per context by caching");
        check(seven && seven->bytes() == 12388, "a context's bytes are both sums");
        const auto* eight = snap.find_context(8);
        check(eight && eight->write_back_bytes == 64 && eight->write_combined_bytes == 64 && eight->blobs == 2,
              "a blob without caching counts as write-back");
        check(snap.used_bytes() == 12388 + 128, "used bytes sum every context");
        check(!snap.find_context(9), "an unknown context is not found");
        check(!snap.device, "blob lines alone do not mark the device");
    }
    {
        QByteArray dump;
        dump += "module/device/device/1/contexts/1/context/7 name=Affinity\\x20Photo\n";
        dump += "module/device/device/1/contexts/1/context/abc name=bad\n";
        dump += "module/device/device/1/contexts/1/context/8\n";
        dump += "module/device/device/1/contexts/1/context/4294967296 name=big\n";
        dump += "module/device/device/1/contexts/1/context/9 name=\n";
        dump += blob(1, "context=7 size=10 caching=0 mapped=true");
        const auto snap = parsed(dump);
        check(snap.find_context(7) && snap.find_context(7)->name == QStringLiteral("Affinity Photo"),
              "a context takes its name from the contexts table");
        check(snap.find_context(7)->bytes() == 10, "the name joins the blob sums");
        check(snap.find_context(9) && snap.find_context(9)->name.isEmpty(), "an empty name is kept");
        check(!snap.find_context(8), "a context line without a name is ignored");
        check(snap.contexts.size() == 2, "bad context ids are ignored");
    }
    {
        QByteArray dump;
        for (int i = 0; i < 5000; ++i) {
            dump += "module/device/device/1/contexts/1/context/" + QByteArray::number(i) + " name=n\n";
        }
        dump += blob(1, "context=0 size=1 caching=0 mapped=true");
        dump += blob(2, "context=99999 size=1 caching=0 mapped=true");
        const auto snap = parsed(dump);
        check(snap.contexts.size() == 4096, "contexts are capped at 4096");
        check(snap.find_context(0) && snap.find_context(0)->bytes() == 1, "an existing context still takes blobs at the cap");
        check(!snap.find_context(99999), "a new context past the cap is refused");
    }
    {
        QByteArray dump;
        for (int i = 0; i < 5000; ++i) {
            dump += channel(i, "reports=1");
        }
        const auto snap = parsed(dump);
        check(snap.channels.size() == 4096, "channels are capped at 4096");
        AvStateDump::Snapshot again = snap;
        AvStateDump::parse(channel(0, "reports=2") + channel(9000, "reports=1"), again);
        check(again.channels.size() == 4096 && again.channels.first().reports == 3,
              "an existing channel still sums at the cap, a new one is refused");
    }
    {
        const auto snap = parsed(
            "module/device/device/1/window_stats/1 attached=true stable=false total_pages=100 pages_in_use=40 "
            "peak_pages_in_use=60 refusals=2 refused_bytes=8192 largest_free_run_at_refusal=7\n"
            "module/device/device/2/window_stats/1 attached=false stable=true total_pages=50 pages_in_use=1 "
            "peak_pages_in_use=2 refusals=1 refused_bytes=1 largest_free_run_at_refusal=9\n");
        check(snap.window_stats.has_value(), "window_stats lines make stats");
        const auto stats = snap.window_stats.value_or(AvStateDump::WindowStats{});
        check(stats.attached && stats.stable, "flags are true when any line says so");
        check(stats.total_pages == 150 && stats.pages_in_use == 41 && stats.peak_pages_in_use == 62,
              "page counts sum");
        check(stats.refusals == 3 && stats.refused_bytes == 8193, "refusals sum");
        check(stats.largest_free_run_at_refusal == 9, "the largest free run is the maximum");
        check(!snap.window_bytes, "window_stats does not set window_bytes");
    }
    {
        const auto snap = parsed(
            "module/device/device/1/window_stats/1 attached=yes total_pages=-4 refusals=99999999999999999999999\n");
        const auto stats = snap.window_stats.value_or(AvStateDump::WindowStats{});
        check(snap.window_stats && !stats.attached && stats.total_pages == 0 && stats.refusals == 0,
              "bad flags and numbers in window_stats count as missing");
        check(!parsed("module/device/device/1/window_stats/1/extra a=1\n").window_stats,
              "window_stats at the wrong depth is ignored");
    }
    {
        QByteArray dump;
        dump += channel(5, "reports=10 rejected=1 split_transfers=2 split_pieces=6 blob_room_waits=3 blob_room_wait_us=900 ring_waits=7 ring_wait_us=70 fence_waits=8 fence_wait_us=80 "
                           "inline_bulk=4 copied_mappings=5 slice_waits=6 refused_no_space=1 refused_no_memory=2 "
                           "refused_other=3 arena_grows=2 arena_bytes=1000 arena_peak_ring_bytes=400 arena_set_aside_bytes=50 "
                           "arena_segments=3 parked_blobs=4");
        dump += "executor/1/session/2/channel/5 reports=1 refused_other=1 arena_bytes=500 arena_peak_ring_bytes=100 "
                "arena_set_aside_bytes=25 arena_grows=1 arena_segments=2 parked_blobs=1\n";
        dump += channel(6, "reports=1");
        dump += channel(7, "ring_waits=q fence_wait_us=-3 arena_bytes=junk arena_peak_ring_bytes=-1 arena_grows=x arena_segments=-2 parked_blobs=y");
        const auto snap = parsed(dump);
        check(snap.executor && !snap.device, "channel lines mark the executor");
        check(snap.channels.size() == 3, "channels are keyed by context");
        const auto& c = snap.channels.at(0);
        check(c.context == 5 && c.reports == 11 && c.rejected == 1 && c.split_transfers == 2 && c.split_pieces == 6,
              "channel counters sum across sessions");
        check(c.blob_room_waits == 3 && c.blob_room_wait_us == 900 && c.ring_waits == 7 && c.ring_wait_us == 70
                  && c.fence_waits == 8 && c.fence_wait_us == 80 && c.inline_bulk == 4 && c.copied_mappings == 5
                  && c.slice_waits == 6,
              "channel wait and mapping counters read");
        check(!snap.channels.at(2).ring_waits && !snap.channels.at(2).fence_wait_us && !snap.channels.at(1).blob_room_waits,
              "malformed or missing wait counters stay unknown");
        check(c.refused_no_space == 1 && c.refused_no_memory == 2 && c.refused_other == 4 && c.refused() == 7,
              "refusals sum by reason");
        check(c.arena_bytes == 1500 && c.arena_peak_ring_bytes == 500 && c.arena_set_aside_bytes == 75,
              "arena counters sum across sessions");
        check(c.arena_grows == 3 && c.arena_segments == 5 && c.parked_blobs == 5, "grows, segments and parked blobs sum");
        check(!snap.channels.at(1).arena_grows && !snap.channels.at(1).arena_segments
                  && !snap.channels.at(1).parked_blobs,
              "grows, segments and parked blobs stay unknown when absent");
        check(!snap.channels.at(2).arena_grows && !snap.channels.at(2).arena_segments
                  && !snap.channels.at(2).parked_blobs,
              "grows, segments and parked blobs stay unknown when malformed");
        check(!snap.channels.at(1).arena_bytes && !snap.channels.at(1).arena_peak_ring_bytes
                  && !snap.channels.at(1).arena_set_aside_bytes,
              "arena counters stay unknown when absent");
        check(!snap.channels.at(2).arena_bytes && !snap.channels.at(2).arena_peak_ring_bytes,
              "arena counters stay unknown when malformed");
    }
    {
        const auto snap = parsed("executor/1/session/1 foo=bar\n");
        check(snap.executor && snap.channels.isEmpty(), "other executor lines mark the executor without channels");
        check(!parsed("executor/1/session/1/channel/x reports=1\n").channels.size(), "a non-numeric channel id is ignored");
        check(!parsed("executor/1/session/1/channel/-1 reports=1\n").channels.size(), "a negative channel id is ignored");
        check(!parsed("executor/1/session/1/channel/4294967296 reports=1\n").channels.size(),
              "an oversized channel id is ignored");
        check(!parsed("executor/1/session/1/channel/ reports=1\n").channels.size(), "an empty channel id is ignored");
    }
    {
        const auto snap = parsed(
            "\n\n"
            "garbage\n"
            "   \n"
            "=== = ==\n"
            "module\n"
            "module/x/device\n"
            "module/x/other/1 host_window_bytes=5\n"
            "other/x/device/1 host_window_bytes=5\n"
            "module/device/device/1/host_blobs/1/other/1 context=1 size=1 mapped=true\n"
            "module/device/device/1/other/1/host_blob/1 context=1 size=1 mapped=true\n"
            "\xff\xfe\x00\x01 a=b\n");
        check(!snap.device && !snap.executor && !snap.window_bytes && !snap.window_stats && snap.contexts.isEmpty()
                  && snap.channels.isEmpty(),
              "garbage and forged lines change nothing");
        check(parsed(QByteArray{}).contexts.isEmpty(), "an empty dump is empty");
        check(parsed("module/x/device/1 host_window_bytes=7").window_bytes == 7, "a last line needs no newline");
    }
    {
        QByteArray dump = "module/device/device/1 host_window_bytes=" + QByteArray(20000, '9') + "\n";
        dump += "module/device/device/2 host_window_bytes=3\n";
        const auto snap = parsed(dump);
        check(snap.window_bytes == 3, "a line over 16 KiB is cut and its number is lost");
        QByteArray tail = "module/device/device/1 " + QByteArray(17000, 'x') + " host_window_bytes=5\n";
        check(!parsed(tail).window_bytes, "fields past the 16 KiB line bound are lost");
    }
    {
        AvStateDump::Snapshot snap;
        AvStateDump::parse(blob(1, "context=3 size=10 caching=0 mapped=true"), snap);
        AvStateDump::parse(blob(1, "context=3 size=5 caching=2 mapped=true"), snap);
        check(snap.contexts.size() == 1 && snap.find_context(3)->bytes() == 15 && snap.find_context(3)->blobs == 2,
              "parsing into an existing snapshot adds to it");
    }

    return g_failures == 0 ? 0 : 1;
}
