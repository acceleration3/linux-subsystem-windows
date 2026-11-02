#include "lsw/base/crash_handler.hpp"
#include "lsw/base/log.hpp"
#include "lsw/base/log_format.hpp"
#include "lsw/base/log_tail.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/base/repeat_collapser.hpp"
#include "lsw/base/rolling_file.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/log_verbosity.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/mirror/mirror_log.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace lsw::base;
using lsw::host::apps::LogRecord;
using lsw::host::apps::LogStore;

int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) ++failures;
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void wait_ms(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void rolling_file_test(const fs::path& dir) {
    const fs::path path = dir / "roll" / "x.log";
    RollingFile file{path, {100, 3}};
    const std::string line(40, 'a');
    check(file.append(line + "\n") && file.append(line + "\n"), "rolling: writes up to the limit");
    check(!fs::exists(RollingFile::numbered(path, 1)), "rolling: nothing rolls below the limit");
    file.append(std::string(40, 'b') + "\n");
    check(fs::exists(RollingFile::numbered(path, 1)) && slurp(path)[0] == 'b',
          "rolling: x.log becomes x.1.log and a fresh x.log starts");
    check(RollingFile::numbered(path, 1).filename() == "x.1.log", "rolling: numbering is x.1.log");
    for (char c : {'c', 'd', 'e', 'f'}) {
        file.append(std::string(60, c) + "\n");
        file.append(std::string(60, c) + "\n");
    }
    check(fs::exists(path) && fs::exists(RollingFile::numbered(path, 1)) &&
              fs::exists(RollingFile::numbered(path, 2)) && !fs::exists(RollingFile::numbered(path, 3)),
          "rolling: 3 files kept, the oldest dropped");
    check(slurp(RollingFile::numbered(path, 2))[0] == 'e' && slurp(path)[0] == 'f',
          "rolling: the newest is x.log, older ones count up");

    file.roll_now();
    check(!fs::exists(path) && slurp(RollingFile::numbered(path, 1))[0] == 'f',
          "rolling: roll_now moves the live file up");
    file.roll_now();
    check(!fs::exists(path) && slurp(RollingFile::numbered(path, 1))[0] == 'f',
          "rolling: roll_now with no live file changes nothing");
    file.append("again\n");
    check(slurp(path) == "again\n", "rolling: the next write starts a fresh file");

    const fs::path other = dir / "roll" / "avgpu.log";
    { std::ofstream(other) << "last run\n"; }
    RollingFile writer{other, RollingFile::kDefaultLimits};
    writer.roll_now();
    check(!fs::exists(other) && slurp(RollingFile::numbered(other, 1)) == "last run\n",
          "rolling: a file rolled before its truncating writer starts");
    { std::ofstream out(other, std::ios::trunc); out << "this run\n"; }
    writer.roll_now();
    check(slurp(RollingFile::numbered(other, 1)) == "this run\n" &&
              slurp(RollingFile::numbered(other, 2)) == "last run\n",
          "rolling: successive runs are kept, newest first");

    check(RollingFile::kManagerLimits.max_bytes == 10u * 1024 * 1024 &&
              RollingFile::kManagerLimits.max_files == 5 &&
              RollingFile::kDefaultLimits.max_bytes == 2u * 1024 * 1024 &&
              RollingFile::kDefaultLimits.max_files == 3,
          "rolling: the documented limits");
    check(RollingFile::numbered("noext", 2) == fs::path("noext.2"), "rolling: a name with no extension");
}

void collapser_test() {
    RepeatCollapser c;
    auto v = c.feed(2, "manager", "hello", 0);
    check(v.write && !v.note, "repeat: the first line is written");
    v = c.feed(2, "manager", "hello", 100);
    check(!v.write && !v.note, "repeat: an identical one within the gap is held back");
    (void)c.feed(2, "manager", "hello", 200);
    v = c.feed(2, "manager", "other", 300);
    check(v.write && v.note && v.note->count == 2 && v.note->source == "manager",
          "repeat: a different line first reports 'repeated 2 times'");
    v = c.feed(3, "manager", "other", 400);
    check(v.write && !v.note, "repeat: another level is another line");
    v = c.feed(3, "qt", "other", 500);
    check(v.write, "repeat: another source is another line");

    RepeatCollapser d;
    (void)d.feed(2, "m", "x", 0);
    (void)d.feed(2, "m", "x", 1000);
    check(!d.due(5999), "repeat: not due inside the window");
    auto note = d.due(6000);
    check(note && note->count == 1, "repeat: due after the window, with nothing after it");
    check(!d.due(5001), "repeat: reported once");
    RepeatCollapser e;
    (void)e.feed(2, "m", "x", 0);
    for (int t = 1000; t <= 8000; t += 1000) {
        auto step = e.feed(2, "m", "x", t);
        if (t == 6000) {
            check(step.note && step.note->count == 5 && !step.write,
                  "repeat: a run that goes on is reported every window, not rewritten");
        }
    }
    auto after_gap = e.feed(2, "m", "x", 20000);
    check(after_gap.write, "repeat: the same line after a long gap is written again");
}

void logger_test(const fs::path& dir) {
    Logger::init("test");
    std::vector<std::string> seen;
    const fs::path path = dir / "logger" / "manager.log";
    LSW_INFO("before attach");
    Logger::attach(path, {1 << 20, 2}, [&seen](int level, std::string_view source, std::string_view text, std::int64_t) {
        seen.push_back(std::string(LogFormat::level_name(level)) + "/" + std::string(source) + "/" + std::string(text));
    });
    for (int i = 0; i < 3; ++i) LSW_WARN("same {}", 1);
    LSW_INFO("different");
    Logger::write(spdlog::level::warn, "qt", "from qt");
    const std::string file = slurp(path);
    check(file.find("before attach") != std::string::npos && seen.size() >= 1 && seen[0] == "info/test/before attach",
          "logger: what came before attach reaches the file and the observer");
    check(seen.size() == 5 && seen[1] == "warn/test/same 1" && seen[2] == "warn/test/last message repeated 2 times" &&
              seen[3] == "info/test/different" && seen[4] == "warn/qt/from qt",
          "logger: repeats collapse into one 'repeated N times' line; other sources pass");
    check(file.find(" warn  test same 1\n") != std::string::npos && file.find(" warn  qt from qt\n") != std::string::npos,
          "logger: lines are '<date time> <level> <source> <message>'");
    check(file.size() > 24 && file[4] == '-' && file[10] == ' ' && file[19] == '.' && file[23] == ' ',
          "logger: the time is local date, time and milliseconds");
    Logger::detach();
}

void macro_test() {
    const int saved = g_log_threshold.load();
    int evaluated = 0;
    const auto count = [&evaluated] { return ++evaluated; };
    g_log_threshold.store(spdlog::level::info);
    LSW_TRACE("trace {}", count());
    LSW_DEBUG("debug {}", count());
    check(evaluated == 0, "macros: arguments are not evaluated below the level");
    LSW_INFO("info {}", count());
    check(evaluated == 1, "macros: arguments are evaluated at the level");
    g_log_threshold.store(spdlog::level::debug);
    LSW_DEBUG("debug {}", count());
    check(evaluated == 2, "macros: lowering the level turns the line on");
    g_log_threshold.store(spdlog::level::err);
    LSW_WARN("warn {}", count());
    LSW_INFO("info {}", count());
    check(evaluated == 2, "macros: nothing evaluated above-level either");
    if (true) LSW_ERROR("error {}", count()); else LSW_ERROR("never");
    check(evaluated == 3, "macros: usable as one statement in an if/else");
    g_log_threshold.store(saved);
}

void store_test() {
    LogStore store;
    int batches = 0;
    int records = 0;
    QObject::connect(&store, &LogStore::appended, &store, [&](const QVector<LogRecord>& batch) {
        ++batches;
        records += static_cast<int>(batch.size());
    });
    for (int i = 0; i < 1000; ++i) {
        store.post({0, 0, QStringLiteral("INFO"), QStringLiteral("manager"), QString::number(i)});
    }
    std::thread other([&store] {
        for (int i = 0; i < 1000; ++i) {
            store.post({0, 0, QStringLiteral("WARN"), QStringLiteral("qt"), QString::number(i)});
        }
    });
    other.join();
    wait_ms(300);
    check(records == 2000 && store.records().size() == 2000, "store: every record from every thread arrives");
    check(batches >= 1 && batches <= 6, "store: the GUI is told in batches, not per record");
    check(store.recent(3, QStringLiteral("manager")).size() == 3 &&
              store.recent(5000, QStringLiteral("qt")).size() == 1000 &&
              store.recent(10).size() == 10,
          "store: records carry their source and can be asked for by it");

    store.clear();
    store.post({1, 1, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("a1")});
    store.post({2, 2, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("a2")});
    store.post({2, 2, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("a2 again")});
    store.post({1, 1, QStringLiteral("INFO"), QStringLiteral("agent session 2"), QStringLiteral("s1")});
    wait_ms(150);
    check(store.records().size() == 3, "store: a replayed seq is dropped, per source");
    store.post({3, 3, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("a3")});
    store.post({1, 4, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("restarted 1")});
    store.post({2, 5, QStringLiteral("INFO"), QStringLiteral("agent"), QStringLiteral("restarted 2")});
    wait_ms(150);
    check(store.records().size() == 6 && store.records().last().msg == QLatin1String("restarted 2"),
          "store: after an agent restart (seq from 1 again) new records are kept");
    check(LogStore::source_matches(QStringLiteral("agent session 2"), QStringLiteral("agent")) &&
              LogStore::source_matches(QStringLiteral("mirror a b"), QStringLiteral("mirror a b")) &&
              !LogStore::source_matches(QStringLiteral("agentx"), QStringLiteral("agent")),
          "store: a source filter matches by name or first word");

    store.clear();
    for (int i = 0; i < LogStore::kCapacity + 5000; ++i) {
        store.post({0, 0, QStringLiteral("INFO"), QStringLiteral("manager"), QString::number(i)});
    }
    wait_ms(300);
    check(LogStore::kCapacity == 20000 && store.records().size() == LogStore::kCapacity &&
              store.records().first().msg == QLatin1String("5000"),
          "store: holds the last 20000, oldest dropped");
}

void mirror_parser_test(const fs::path& dir) {
    using lsw::host::mirror::MirrorLog;
    auto p = MirrorLog::parse("2026-11-02 14:03:09.482 [    0.041273]  info link     004f | guest dialled in\n");
    check(p.level == 2 && p.tag == "link" && p.message == "guest dialled in", "mirror line: wall clock, elapsed, info");
    p = MirrorLog::parse("2026-11-02 14:03:09.482 [   12.000000] error render   0001 | boom | twice");
    check(p.level == 4 && p.tag == "render" && p.message == "boom | twice", "mirror line: error, bar in the message");
    p = MirrorLog::parse("[    0.041273] trace x        0 | old format");
    check(p.level == 0 && p.tag == "x" && p.message == "old format", "mirror line: without the wall clock");
    p = MirrorLog::parse("[    1.5]  warn t 1 | w");
    check(p.level == 3, "mirror line: warn");
    p = MirrorLog::parse("wl_display: connection lost\n");
    check(p.level == 2 && p.tag.empty() && p.message == "wl_display: connection lost",
          "mirror line: anything else is kept whole at info");
    p = MirrorLog::parse("[nonsense] loud x 1 | y");
    check(p.level == 2 && p.message == "[nonsense] loud x 1 | y", "mirror line: an unknown level is not a level");

    MirrorLog::set_directory(QString::fromStdString((dir / "mirrors").string()));
    LogStore store;
    MirrorLog log(QStringLiteral("My App/1"));
    check(log.name() == QLatin1String("My App_1"), "mirror log: the file name is made safe");
    const fs::path file = dir / "mirrors" / "My App_1.log";
    { fs::create_directories(file.parent_path()); std::ofstream(file) << "previous run\n"; }
    log.roll_for_start();
    check(slurp(lsw::base::RollingFile::numbered(file, 1)) == "previous run\n", "mirror log: the last run is rolled before start");
    log.consume("2026-11-02 14:03:09.482 [    0.04]  warn link 1 | hel");
    log.consume("lo\nplain line\npartial");
    log.finish();
    wait_ms(150);
    check(slurp(file) == "2026-11-02 14:03:09.482 [    0.04]  warn link 1 | hello\nplain line\npartial\n",
          "mirror log: lines are written whole, a partial one at the end too");
    const auto kept = store.recent(10, QStringLiteral("mirror"));
    check(kept.size() == 3 && kept[0].source == QLatin1String("mirror My App_1") &&
              kept[0].level == QLatin1String("WARN") && kept[0].msg == QLatin1String("link: hello") &&
              kept[1].level == QLatin1String("INFO"),
          "mirror log: and into the store with source and level");
    MirrorLog::set_directory({});
}

void verbosity_test() {
    using lsw::host::config::LogVerbosity;
    using lsw::host::config::ManagerSettings;
    using Part = LogVerbosity::Part;
    ManagerSettings settings;
    const auto K = [](const auto& key) { return QString(key); };
    settings.set_value(K(ManagerSettings::Key::kLogDebugUntil), qlonglong{0});
    for (Part part : {Part::Manager, Part::Mirrors, Part::Agent, Part::GuestMirrors, Part::Gpu}) {
        settings.set_value(LogVerbosity::key(part), LogVerbosity::default_level(part));
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    check(LogVerbosity::effective(settings, Part::Manager, now) == QLatin1String("info") &&
              LogVerbosity::effective(settings, Part::Gpu, now) == QLatin1String("warn"),
          "settings: info by default, the GPU at warn");
    check(LogVerbosity::dxvk_level(QStringLiteral("warn")) == QLatin1String("warn") &&
              LogVerbosity::vkd3d_level(QStringLiteral("warn")) == QLatin1String("warn") &&
              LogVerbosity::vkd3d_level(QStringLiteral("error")) == QLatin1String("err") &&
              LogVerbosity::dxvk_level(QStringLiteral("debug")) == QLatin1String("debug"),
          "settings: DXVK's and vkd3d-proton's words for a level");

    LogVerbosity verbosity(settings);
    int changes = 0;
    QObject::connect(&verbosity, &LogVerbosity::changed, &verbosity, [&] { ++changes; });
    verbosity.set_level(Part::Agent, QStringLiteral("WARN"));
    check(LogVerbosity::configured(settings, Part::Agent) == QLatin1String("warn") && changes == 1,
          "settings: a level is stored and announced");
    settings.set_value(LogVerbosity::key(Part::Mirrors), QStringLiteral("loud"));
    check(LogVerbosity::configured(settings, Part::Mirrors) == QLatin1String("info"),
          "settings: a level that is not one reads as the default");

    verbosity.start_debug(30);
    check(verbosity.debug_active() && LogVerbosity::effective(settings, Part::Agent) == QLatin1String("debug") &&
              LogVerbosity::effective(settings, Part::Gpu) == QLatin1String("debug"),
          "settings: debug for 30 minutes raises every part");
    check(verbosity.debug_until_ms() > now + 29 * 60 * 1000 && verbosity.debug_until_ms() < now + 31 * 60 * 1000,
          "settings: ...until 30 minutes from now");
    check(!verbosity.expire_debug(now + 10 * 60 * 1000), "settings: not before it is time");
    const int before = changes;
    check(verbosity.expire_debug(now + 31 * 60 * 1000) && !verbosity.debug_active() &&
              verbosity.debug_until_ms() == 0 && changes > before,
          "settings: the levels are put back when the time passes, and announced");
    check(LogVerbosity::effective(settings, Part::Agent) == QLatin1String("warn"),
          "settings: ...to what was configured");
    check(LogVerbosity::effective(settings, Part::Gpu, now + 1) == QLatin1String("warn"),
          "settings: no debug time, no debug");
    settings.set_value(LogVerbosity::key(Part::Agent), LogVerbosity::default_level(Part::Agent));
}

void crash_test(const fs::path& dir) {
    const fs::path crash_dir = dir / "crash";
    fs::create_directories(crash_dir);
    std::fflush(nullptr);
    const pid_t child = ::fork();
    if (child == 0) {
        CrashHandler::install(crash_dir.string(), "test-version");
        LogTail::instance().append("2026-11-02 14:03:09.482 info  manager the line before the crash\n");
        ::raise(SIGSEGV);
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "crash: the process still dies of the signal");
    std::string report;
    for (const auto& entry : fs::directory_iterator(crash_dir)) {
        if (entry.path().filename().string().rfind("crash-", 0) == 0) report = slurp(entry.path());
    }
    check(report.find("SIGSEGV") != std::string::npos && report.find("test-version") != std::string::npos &&
              report.find("backtrace:") != std::string::npos &&
              report.find("the line before the crash") != std::string::npos,
          "crash: crash-<time>.log has the signal, a backtrace and the last log lines");
}

}

int main(int argc, char** argv) {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    const fs::path dir = temp.path().toStdString();

    rolling_file_test(dir);
    collapser_test();
    logger_test(dir);
    macro_test();
    store_test();
    mirror_parser_test(dir);
    verbosity_test();
    crash_test(dir);
    return failures == 0 ? 0 : 1;
}
