#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace lsw::agent::log {

namespace {

// 5000 lines is plenty for a fresh launch + a few RPCs. The manager
// polls regularly so fall-behind is unlikely; the ring just protects
// against the manager being disconnected for a long time.
constexpr size_t kRingCapacity = 5000;

constexpr const wchar_t kLogPath[] = L"C:\\ProgramData\\LSW\\agent.log";
constexpr const wchar_t kLogDir[] = L"C:\\ProgramData\\LSW";

std::mutex& mu() {
    static std::mutex m;
    return m;
}

std::deque<Record>& ring() {
    static std::deque<Record> r;
    return r;
}

std::uint64_t& seq_counter() {
    static std::uint64_t s = 0;
    return s;
}

std::uint64_t now_ms() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100-ns intervals since 1601-01-01; unix epoch is
    // 11644473600 seconds later.
    constexpr std::uint64_t kEpochDiff100ns = 116444736000000000ULL;
    return (u.QuadPart - kEpochDiff100ns) / 10000ULL;
}

void format_prefix(char* out, size_t cap, std::uint64_t ts_ms, Level level) {
    SYSTEMTIME st{};
    FILETIME ft{};
    ULARGE_INTEGER u{};
    u.QuadPart = ts_ms * 10000ULL + 116444736000000000ULL;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    ::FileTimeToSystemTime(&ft, &st);
    SYSTEMTIME local{};
    ::SystemTimeToTzSpecificLocalTime(nullptr, &st, &local);
    std::snprintf(out, cap, "[%04u-%02u-%02u %02u:%02u:%02u.%03u %-5s] ",
                  local.wYear, local.wMonth, local.wDay,
                  local.wHour, local.wMinute, local.wSecond, local.wMilliseconds,
                  level_name(level));
}

// ---- Async file writer -------------------------------------------------
//
// The file is a debug side-artifact; the in-memory ring (drained by
// collect_since and shipped over the wire) is the source of truth the
// manager actually reads. Writing the file SYNCHRONOUSLY from append()
// — reopening C:\ProgramData\LSW\agent.log per line — stalled the
// agent's single-threaded host_endpoint loop for *seconds* per log
// call on the qcow2-backed guest disk (observed: identical system_info
// path taking 26 ms vs 18 s, the whole gap between two consecutive log
// lines). That starved RPC delivery and window-frame capture. So file
// I/O is now off the hot path entirely: append() only enqueues; a
// dedicated background thread owns a single persistent handle and
// writes batched, never blocking a logging caller.

std::mutex& file_mu() {
    static std::mutex m;
    return m;
}

std::condition_variable& file_cv() {
    static std::condition_variable cv;
    return cv;
}

std::deque<Record>& file_q() {
    static std::deque<Record> q;
    return q;
}

// Cap the pending-write backlog. If the disk wedges hard we drop
// file lines (the ring/wire still has them) rather than grow forever.
constexpr size_t kFileQueueCap = 20000;

void writer_thread_body() {
    ::CreateDirectoryW(kLogDir, nullptr);
    // Open ONCE and keep it. FILE_APPEND_DATA keeps appends atomic
    // even though we no longer reopen. No FILE_FLAG_WRITE_THROUGH /
    // FlushFileBuffers — those force the qcow2 fsync stalls we're
    // eliminating; the OS lazily flushes the buffered handle, which is
    // fine for a debug log (the wire path already has every line).
    HANDLE h = ::CreateFileW(kLogPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    std::deque<Record> batch;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(file_mu());
            file_cv().wait(lock, [] { return !file_q().empty(); });
            batch.swap(file_q());
        }
        for (const auto& r : batch) {
            if (h == INVALID_HANDLE_VALUE) {
                continue;  // keep draining the queue even if the file is unusable
            }
            char prefix[64];
            format_prefix(prefix, sizeof(prefix), r.ts_ms, r.level);
            DWORD wrote = 0;
            ::WriteFile(h, prefix, static_cast<DWORD>(std::strlen(prefix)), &wrote,
                        nullptr);
            ::WriteFile(h, r.msg.data(), static_cast<DWORD>(r.msg.size()), &wrote,
                        nullptr);
            static constexpr char nl[] = "\r\n";
            ::WriteFile(h, nl, 2, &wrote, nullptr);
        }
        batch.clear();
    }
}

void ensure_writer_started() {
    // Detached, process-lifetime — same pattern as the console reader
    // threads. No join: logging has no shutdown hook and the OS
    // reclaims the handle on exit.
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread{writer_thread_body}.detach();
    });
}

void enqueue_for_file(const Record& r) {
    {
        std::lock_guard<std::mutex> lock(file_mu());
        auto& q = file_q();
        if (q.size() >= kFileQueueCap) {
            q.pop_front();  // drop oldest unwritten line under sustained backlog
        }
        q.push_back(r);
    }
    file_cv().notify_one();
}

}  // namespace

const char* level_name(Level lvl) {
    switch (lvl) {
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}

void append(Level level, const std::string& msg) {
    ensure_writer_started();
    Record r;
    {
        // Only the in-memory ring + seq are touched under mu() — this
        // is the wire-facing state and must stay ordered/immediate.
        // No file I/O here: that's the background writer's job.
        std::lock_guard<std::mutex> lock(mu());
        r.seq = ++seq_counter();
        r.ts_ms = now_ms();
        r.level = level;
        r.msg = msg;
        auto& q = ring();
        q.push_back(r);
        while (q.size() > kRingCapacity) {
            q.pop_front();
        }
    }
    enqueue_for_file(r);
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

}  // namespace lsw::agent::log
