#include "lsw/agent/win/desktop_probe.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <iterator>
#include <mutex>
#include <vector>

namespace lsw::agent::win {

namespace {

constexpr const wchar_t kProbeLogPath[] = L"C:\\ProgramData\\LSW\\desktop-probe.log";

std::mutex g_write_mu;
unsigned long g_sequence = 0;

[[nodiscard]] std::string narrow_or(const std::wstring& w, const char* fallback) {
    return w.empty() ? std::string{fallback} : util::TextEncoding::to_utf8(w);
}

[[nodiscard]] std::wstring object_name(HANDLE user_object) {
    if (user_object == nullptr) {
        return {};
    }
    wchar_t buffer[256] = {};
    DWORD needed = 0;
    if (::GetUserObjectInformationW(user_object, UOI_NAME, buffer, sizeof(buffer),
                                    &needed)
        == FALSE) {
        return {};
    }
    return std::wstring{buffer};
}

[[nodiscard]] std::string timestamp() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear,
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

[[nodiscard]] std::string hex(const void* p) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "0x%llx",
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(p)));
    return buf;
}

// ---- processes -------------------------------------------------------------

struct ProcessRow {
    DWORD pid = 0;
    DWORD parent = 0;
    std::wstring image;
};

[[nodiscard]] std::vector<ProcessRow> all_processes() {
    std::vector<ProcessRow> rows;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return rows;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snap, &entry) != FALSE) {
        do {
            rows.push_back({entry.th32ProcessID, entry.th32ParentProcessID,
                            entry.szExeFile});
        } while (::Process32NextW(snap, &entry) != FALSE);
    }
    ::CloseHandle(snap);
    return rows;
}

[[nodiscard]] std::wstring image_for(const std::vector<ProcessRow>& procs, DWORD pid) {
    for (const auto& p : procs) {
        if (p.pid == pid) {
            return p.image;
        }
    }
    return {};
}

// ---- windows ---------------------------------------------------------------

struct WindowContext {
    std::string* out;
    const std::vector<ProcessRow>* procs;
    int depth;
    const char* desktop;
};

void describe_window(HWND window, WindowContext& ctx) {
    wchar_t klass[256] = {};
    wchar_t title[256] = {};
    ::GetClassNameW(window, klass, static_cast<int>(std::size(klass)));
    ::GetWindowTextW(window, title, static_cast<int>(std::size(title)));

    RECT rect{};
    ::GetWindowRect(window, &rect);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);

    char buf[256] = {};
    std::snprintf(buf, sizeof(buf),
                  "WIN desktop=%s depth=%d hwnd=%s parent=%s owner=%s pid=%lu",
                  ctx.desktop, ctx.depth, hex(window).c_str(),
                  hex(::GetParent(window)).c_str(),
                  hex(::GetWindow(window, GW_OWNER)).c_str(),
                  static_cast<unsigned long>(pid));
    *ctx.out += buf;

    std::snprintf(buf, sizeof(buf), " vis=%d style=0x%08lx ex=0x%08lx rect=%ld,%ld,%ldx%ld",
                  ::IsWindowVisible(window) ? 1 : 0,
                  static_cast<unsigned long>(::GetWindowLongW(window, GWL_STYLE)),
                  static_cast<unsigned long>(::GetWindowLongW(window, GWL_EXSTYLE)),
                  static_cast<long>(rect.left), static_cast<long>(rect.top),
                  static_cast<long>(rect.right - rect.left),
                  static_cast<long>(rect.bottom - rect.top));
    *ctx.out += buf;

    *ctx.out += " proc=" + narrow_or(image_for(*ctx.procs, pid), "<unknown>");
    *ctx.out += " cls=" + narrow_or(klass, "<none>");
    *ctx.out += " title=\"" + narrow_or(title, "") + "\"\n";
}

BOOL CALLBACK child_proc(HWND window, LPARAM param);

void walk_children(HWND parent, WindowContext& ctx) {
    // Bound the recursion: some UI trees are deep and this runs on a
    // timer during boot, when the machine is already busy.
    if (ctx.depth >= 4) {
        return;
    }
    WindowContext child_ctx = ctx;
    child_ctx.depth = ctx.depth + 1;
    ::EnumChildWindows(parent, &child_proc, reinterpret_cast<LPARAM>(&child_ctx));
}

BOOL CALLBACK child_proc(HWND window, LPARAM param) {
    auto* ctx = reinterpret_cast<WindowContext*>(param);
    describe_window(window, *ctx);
    return TRUE;
}

BOOL CALLBACK top_level_proc(HWND window, LPARAM param) {
    auto* ctx = reinterpret_cast<WindowContext*>(param);
    describe_window(window, *ctx);
    walk_children(window, *ctx);
    return TRUE;
}

// ---- desktops --------------------------------------------------------------

BOOL CALLBACK desktop_name_proc(LPWSTR name, LPARAM param) {
    auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
    names->emplace_back(name);
    return TRUE;
}

// Enumerate the windows of one named desktop. Opening a desktop we have
// no rights to (Winlogon, typically) fails — and that failure is itself
// the interesting datum, so it is recorded rather than skipped.
void dump_desktop(const std::wstring& name, const std::vector<ProcessRow>& procs,
                  std::string& out) {
    const std::string narrow_name = util::TextEncoding::to_utf8(name);
    ::SetLastError(0);
    HDESK desk = ::OpenDesktopW(name.c_str(), 0, FALSE, DESKTOP_READOBJECTS
                                                          | DESKTOP_ENUMERATE);
    if (desk == nullptr) {
        out += "DESKTOP name=" + narrow_name + " open=failed err="
               + std::to_string(::GetLastError()) + "\n";
        return;
    }
    out += "DESKTOP name=" + narrow_name + " open=ok\n";
    WindowContext ctx{&out, &procs, 0, narrow_name.c_str()};
    ::EnumDesktopWindows(desk, &top_level_proc, reinterpret_cast<LPARAM>(&ctx));
    ::CloseDesktop(desk);
}

}  // namespace

std::string DesktopProbe::snapshot(const char* reason) {
    const auto procs = all_processes();

    std::string out;
    out.reserve(64 * 1024);

    // --- header: which desktop currently owns user input ---------------
    ::SetLastError(0);
    HDESK input = ::OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const DWORD input_err = (input == nullptr) ? ::GetLastError() : 0;
    const std::wstring input_name = object_name(input);
    if (input != nullptr) {
        ::CloseDesktop(input);
    }
    const std::wstring our_desktop =
        object_name(::GetThreadDesktop(::GetCurrentThreadId()));

    HWND foreground = ::GetForegroundWindow();
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);

    out += "=== seq=" + std::to_string(++g_sequence) + " time=" + timestamp()
           + " reason=" + reason
           + " input_desktop=" + narrow_or(input_name, "<unopenable>")
           + " input_err=" + std::to_string(input_err)
           + " our_desktop=" + narrow_or(our_desktop, "<unknown>")
           + " session=" + std::to_string(session)
           + " foreground=" + hex(foreground)
           + " screen=" + std::to_string(::GetSystemMetrics(SM_CXSCREEN)) + "x"
           + std::to_string(::GetSystemMetrics(SM_CYSCREEN)) + "\n";

    // --- every process --------------------------------------------------
    for (const auto& p : procs) {
        out += "PROC pid=" + std::to_string(p.pid) + " ppid=" + std::to_string(p.parent)
               + " name=" + narrow_or(p.image, "<unknown>") + "\n";
    }

    // --- every desktop in this window station, and its windows ----------
    std::vector<std::wstring> desktops;
    if (HWINSTA station = ::GetProcessWindowStation(); station != nullptr) {
        ::EnumDesktopsW(station, &desktop_name_proc,
                        reinterpret_cast<LPARAM>(&desktops));
    }
    if (desktops.empty()) {
        desktops.push_back(our_desktop.empty() ? L"Default" : our_desktop);
    }
    for (const auto& name : desktops) {
        dump_desktop(name, procs, out);
    }

    out += "=== end seq=" + std::to_string(g_sequence) + "\n";
    return out;
}

void DesktopProbe::write(const std::string& text) {
    // CRLF: the log is read on the guest with cmd's find/findstr, which
    // treat an LF-only file as a single enormous line and silently match
    // nothing.
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 32);
    for (const char ch : text) {
        if (ch == '\n') {
            crlf.push_back('\r');
        }
        crlf.push_back(ch);
    }

    const std::lock_guard<std::mutex> lock(g_write_mu);
    HANDLE file = ::CreateFileW(kProbeLogPath, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    ::SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    ::WriteFile(file, crlf.data(), static_cast<DWORD>(crlf.size()), &written, nullptr);
    ::CloseHandle(file);
}

void DesktopProbe::start(std::chrono::seconds duration,
                         std::chrono::milliseconds interval) {
    LSW_AGENT_INFO(std::string{"probe: capturing full session snapshots to "}
                   + util::TextEncoding::to_utf8(kProbeLogPath));
    thread_ = std::thread([this, duration, interval] {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (!stop_.load() && std::chrono::steady_clock::now() < deadline) {
            write(snapshot("tick"));
            std::this_thread::sleep_for(interval);
        }
        write("=== probe: sampling window closed\n");
    });
}

void DesktopProbe::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

DesktopProbe::~DesktopProbe() { stop(); }

}  // namespace lsw::agent::win
