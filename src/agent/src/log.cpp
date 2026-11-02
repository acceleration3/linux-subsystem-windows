#include "lsw/agent/log.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/win/file_time.hpp"
#include "lsw/agent/win/rolling_file.hpp"

#include "lsw/base/repeat_collapser.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace lsw::agent::log {

namespace {
constexpr size_t kRingCapacity = 5000;

std::mutex& mu() {
    static auto* m = new std::mutex;
    return *m;
}

std::deque<Record>& ring() {
    static auto* r = new std::deque<Record>;
    return *r;
}

std::uint64_t& seq_counter() {
    static std::uint64_t s = 0;
    return s;
}

std::wstring& file_path() {
    static auto* p = new std::wstring;
    return *p;
}

win::RollingFile::Limits& file_limits() {
    static auto* l = new win::RollingFile::Limits{win::RollingFile::kAgentLimits};
    return *l;
}

std::string& process_source() {
    static auto* s = new std::string{"agent"};
    return *s;
}

std::uint64_t now_ms() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    return win::FileTime::to_unix_ms(ft);
}

void append_line(std::string& out, const Record& r) {
    SYSTEMTIME st{};
    const FILETIME ft = win::FileTime::from_unix_ms(r.ts_ms);
    ::FileTimeToSystemTime(&ft, &st);
    SYSTEMTIME local{};
    ::SystemTimeToTzSpecificLocalTime(nullptr, &st, &local);
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u %-5s ", local.wYear,
                  local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond,
                  local.wMilliseconds, level_name(r.level));
    out += prefix;
    out += r.source;
    out += ' ';
    out += r.msg;
    out += '\n';
}

std::int64_t mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::mutex& file_mu() {
    static auto* m = new std::mutex;
    return *m;
}

std::condition_variable& file_cv() {
    static auto* cv = new std::condition_variable;
    return *cv;
}

std::condition_variable& written_cv() {
    static auto* cv = new std::condition_variable;
    return *cv;
}

std::deque<Record>& file_q() {
    static auto* q = new std::deque<Record>;
    return *q;
}

bool& clear_pending() {
    static bool pending = false;
    return pending;
}

std::size_t& in_writer() {
    static std::size_t n = 0;
    return n;
}

constexpr size_t kFileQueueCap = 20000;

constexpr size_t kWriteChunk = 32 * 1024;

constexpr auto kRepeatTick = std::chrono::seconds(1);

void writer_thread_body() {
    std::wstring path;
    win::RollingFile::Limits limits{};
    {
        const std::lock_guard<std::mutex> lock(file_mu());
        path = file_path().empty() ? core::Paths::agent_log() : file_path();
        limits = file_limits();
    }
    win::RollingFile file{path, limits};
    lsw::base::RepeatCollapser collapser;
    std::deque<Record> batch;
    std::string text;

    const auto note_line = [&text](const lsw::base::RepeatCollapser::Note& note, std::uint64_t ts_ms) {
        Record r;
        r.ts_ms = ts_ms;
        r.level = static_cast<Level>(note.level);
        r.source = note.source;
        r.msg = "last message repeated " + std::to_string(note.count) +
                (note.count == 1 ? " time" : " times");
        append_line(text, r);
    };
    const auto write_out = [&] {
        if (!text.empty()) {
            (void)file.append(text);
            text.clear();
        }
    };

    for (;;) {
        bool idle = false;
        bool clear = false;
        {
            std::unique_lock<std::mutex> lock(file_mu());
            idle = !file_cv().wait_for(lock, kRepeatTick,
                                       [] { return !file_q().empty() || clear_pending(); });
            clear = clear_pending();
            clear_pending() = false;
            batch.swap(file_q());
            in_writer() = batch.size() + (clear ? 1 : 0);
        }
        if (clear) {
            (void)file.truncate();
        }
        if (idle) {
            if (const auto note = collapser.due(mono_ms())) {
                note_line(*note, now_ms());
                write_out();
            }
            continue;
        }
        for (const auto& r : batch) {
            const auto verdict =
                collapser.feed(static_cast<int>(r.level), r.source, r.msg, mono_ms());
            if (verdict.note) {
                note_line(*verdict.note, r.ts_ms);
            }
            if (verdict.write) {
                append_line(text, r);
            }
            if (text.size() >= kWriteChunk) {
                write_out();
            }
        }
        write_out();
        batch.clear();
        {
            const std::lock_guard<std::mutex> lock(file_mu());
            in_writer() = 0;
        }
        written_cv().notify_all();
    }
}

void ensure_writer_started() {
    static std::once_flag once;
    std::call_once(once, [] { std::thread{writer_thread_body}.detach(); });
}

void enqueue_for_file(const Record& r) {
    {
        std::lock_guard<std::mutex> lock(file_mu());
        auto& q = file_q();
        if (q.size() >= kFileQueueCap) {
            q.pop_front();
        }
        q.push_back(r);
    }
    file_cv().notify_one();
}

}

namespace detail {

void append_wide(std::string& out, std::wstring_view value) {
    out += util::TextEncoding::to_utf8(std::wstring{value});
}

}

namespace detail {
std::atomic<int> g_min_level{static_cast<int>(Level::Info)};
}

void set_level(Level level) {
    detail::g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

Level level() {
    return static_cast<Level>(detail::g_min_level.load(std::memory_order_relaxed));
}

Level level_from_name(std::string_view name) {
    std::string lower;
    for (const char c : name) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "trace") {
        return Level::Trace;
    }
    if (lower == "debug") {
        return Level::Debug;
    }
    if (lower == "warn" || lower == "warning") {
        return Level::Warn;
    }
    if (lower == "error" || lower == "err") {
        return Level::Error;
    }
    return Level::Info;
}

void set_file(const std::wstring& path, win::RollingFile::Limits limits) {
    const std::lock_guard<std::mutex> lock(file_mu());
    file_path() = path;
    file_limits() = limits;
}

void clear_file() {
    ensure_writer_started();
    std::unique_lock<std::mutex> lock(file_mu());
    file_q().clear();
    clear_pending() = true;
    file_cv().notify_one();
    written_cv().wait_for(lock, std::chrono::seconds(5),
                          [] { return !clear_pending() && in_writer() == 0; });
}

void set_source(std::string source) {
    const std::lock_guard<std::mutex> lock(mu());
    process_source() = std::move(source);
}

void flush(unsigned timeout_ms) {
    std::unique_lock<std::mutex> lock(file_mu());
    written_cv().wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [] { return file_q().empty() && in_writer() == 0; });
}

const char* level_name(Level lvl) {
    switch (lvl) {
    case Level::Trace:
        return "trace";
    case Level::Debug:
        return "debug";
    case Level::Info:
        return "info";
    case Level::Warn:
        return "warn";
    case Level::Error:
        return "error";
    }
    return "?";
}

namespace {

void push(Level level, const std::string* source, const std::string& msg, std::uint64_t ts_ms) {
    ensure_writer_started();
    Record r;
    {
        std::lock_guard<std::mutex> lock(mu());
        r.seq = ++seq_counter();
        r.ts_ms = ts_ms != 0 ? ts_ms : now_ms();
        r.level = level;
        r.source = source != nullptr ? *source : process_source();
        r.msg = msg;
        auto& q = ring();
        q.push_back(r);
        while (q.size() > kRingCapacity) {
            q.pop_front();
        }
    }
    enqueue_for_file(r);
}

}

void append(Level level, const std::string& msg) {
    push(level, nullptr, msg, 0);
}

void append_from(Level level, std::string_view source, const std::string& msg,
                 std::uint64_t ts_ms) {
    const std::string owned{source};
    push(level, &owned, msg, ts_ms);
}

std::vector<Record> collect_since(std::uint64_t since, std::uint64_t& next_since) {
    std::lock_guard<std::mutex> lock(mu());
    std::vector<Record> out;
    const auto& q = ring();
    out.reserve(q.size());
    for (const auto& r : q) {
        if (r.seq > since) {
            out.push_back(r);
        }
    }
    next_since = out.empty() ? since : out.back().seq;
    return out;
}

}
