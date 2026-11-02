#include "lsw/base/crash_handler.hpp"

#include "lsw/base/log_tail.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace lsw::base {

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
constexpr int kMaxFrames = 64;

char g_dir[512];
char g_version[128];
char g_path[640];
std::atomic<bool> g_reported{false};
alignas(16) char g_alt_stack[64 * 1024];

void put(int fd, const char* text) noexcept {
    std::size_t left = std::strlen(text);
    while (left > 0) {
        const ssize_t n = ::write(fd, text, left);
        if (n <= 0) {
            return;
        }
        text += n;
        left -= static_cast<std::size_t>(n);
    }
}

void put_number(int fd, long value) noexcept {
    char buf[24];
    int pos = sizeof buf;
    const bool negative = value < 0;
    unsigned long v = negative ? -static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
    do {
        buf[--pos] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    if (negative) {
        buf[--pos] = '-';
    }
    (void)!::write(fd, buf + pos, sizeof buf - static_cast<std::size_t>(pos));
}

const char* signal_name(int signal) noexcept {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGFPE: return "SIGFPE (arithmetic exception)";
    case SIGILL: return "SIGILL (illegal instruction)";
    default: return "signal";
    }
}

void on_signal(int signal) {
    CrashHandler::write_report(signal_name(signal), signal);
    ::signal(signal, SIG_DFL);
    ::raise(signal);
}

void on_terminate() {
    static char what[256];
    what[0] = '\0';
    if (const auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            std::snprintf(what, sizeof what, "uncaught exception: %s", e.what());
        } catch (...) {
            std::snprintf(what, sizeof what, "uncaught exception of unknown type");
        }
    } else {
        std::snprintf(what, sizeof what, "std::terminate without an exception");
    }
    CrashHandler::write_report(what, 0);
    std::abort();
}

}

const char* CrashHandler::write_report(const char* reason, int signal) noexcept {
    if (g_dir[0] == '\0' || g_reported.exchange(true)) {
        return "";
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
    std::snprintf(g_path, sizeof g_path, "%s/crash-%s.log", g_dir, stamp);
    const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        g_path[0] = '\0';
        return "";
    }
    put(fd, "LSW manager crash report\nversion: ");
    put(fd, g_version);
    put(fd, "\nwhen:    ");
    char when[40];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
    put(fd, when);
    put(fd, "\npid:     ");
    put_number(fd, ::getpid());
    put(fd, "\nreason:  ");
    put(fd, reason);
    if (signal != 0) {
        put(fd, "\nsignal:  ");
        put_number(fd, signal);
    }
    put(fd, "\n\nbacktrace:\n");
    void* frames[kMaxFrames];
    const int depth = ::backtrace(frames, kMaxFrames);
    ::backtrace_symbols_fd(frames, depth, fd);
    put(fd, "\nlast log lines:\n");
    LogTail::instance().dump(fd);
    ::close(fd);
    return g_path;
}

void CrashHandler::install(const std::string& logs_dir, const std::string& version) {
    std::snprintf(g_dir, sizeof g_dir, "%s", logs_dir.c_str());
    std::snprintf(g_version, sizeof g_version, "%s", version.c_str());
    g_reported.store(false);

    void* warm[2];
    (void)::backtrace(warm, 2);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&now, &local);

    stack_t alt{};
    alt.ss_sp = g_alt_stack;
    alt.ss_size = sizeof g_alt_stack;
    ::sigaltstack(&alt, nullptr);

    struct sigaction action {};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
    for (const int signal : kSignals) {
        ::sigaction(signal, &action, nullptr);
    }
    std::set_terminate(on_terminate);
}

}
