#include "lsw/agent/apps/launch_group.hpp"

#include "lsw/agent/apps/guest_drivers.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <atomic>
#include <chrono>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lsw::agent::apps {

namespace {

// How stale a membership snapshot may be before the next lookup rebuilds
// it. The window enumeration runs at ~45 Hz and asks about every window
// it found, so answering each one with a fresh scan would turn one poll
// into dozens; a snapshot this old can at worst delay a mirror by one
// poll.
constexpr auto kSnapshotMaxAge = std::chrono::milliseconds(200);

std::uint64_t as_u64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring lower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(::towlower(c));
    }
    return s;
}

struct Group {
    HANDLE job = nullptr;
    // The launched executable's own directory, lowercased and ending in
    // a separator. Anything running out of this tree is this launch.
    std::wstring dir_lc;
    // When the launch started. Nothing older can belong to it, which is
    // what keeps the directory rule from adopting an instance the user
    // already had open.
    std::uint64_t started = 0;
    // The driver directories this launch staged, removed when it
    // retires.
    GuestDrivers::Staging staging;
};

// One process as a scan found it.
struct Proc {
    unsigned long pid = 0;
    unsigned long ppid = 0;
    std::wstring image_lc;
    std::uint64_t started = 0;  // 0 = could not be read
};

struct State {
    std::mutex mu;
    // Launched PID -> its group.
    std::unordered_map<unsigned long, Group> groups;
    // Any PID -> the launched PID it belongs to.
    std::unordered_map<unsigned long, unsigned long> owner_of;
    std::chrono::steady_clock::time_point built{};
};

State& state() {
    // Never destroyed. The reaper below outlives static destruction in
    // any ordering that matters, and a process exiting has nothing to
    // gain from tearing this down first.
    static State* s = new State{};
    return *s;
}

// The PIDs currently in ``job``. Empty for a job whose processes have
// all exited.
std::vector<unsigned long> pids_in(HANDLE job) {
    // The list is variable-length and the API reports how many it could
    // not fit, so size the buffer up until everything fits.
    DWORD capacity = 64;
    for (int attempt = 0; attempt < 6; ++attempt) {
        const DWORD bytes =
            static_cast<DWORD>(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST)
                               + (capacity - 1) * sizeof(ULONG_PTR));
        std::vector<unsigned char> buf(bytes, 0);
        auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
        DWORD returned = 0;
        const BOOL ok = ::QueryInformationJobObject(
            job, JobObjectBasicProcessIdList, list, bytes, &returned);
        if (ok == 0 && ::GetLastError() != ERROR_MORE_DATA) {
            return {};
        }
        if (ok == 0 || list->NumberOfProcessIdsInList
                           < list->NumberOfAssignedProcesses) {
            capacity = list->NumberOfAssignedProcesses + 16;
            continue;
        }
        std::vector<unsigned long> pids;
        pids.reserve(list->NumberOfProcessIdsInList);
        for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
            pids.push_back(static_cast<unsigned long>(list->ProcessIdList[i]));
        }
        return pids;
    }
    return {};
}

// Every process on the system, with the parent, image path and start
// time needed to attribute it to a launch. Image path and start time are
// best-effort: a process we may not open keeps its parent link and
// nothing else.
std::vector<Proc> scan_processes() {
    std::vector<Proc> out;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return out;
    }
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    if (::Process32FirstW(snap, &e) != 0) {
        do {
            Proc p;
            p.pid = e.th32ProcessID;
            p.ppid = e.th32ParentProcessID;
            HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
            if (h != nullptr) {
                wchar_t buf[MAX_PATH * 2] = {};
                DWORD n = MAX_PATH * 2;
                if (::QueryFullProcessImageNameW(h, 0, buf, &n) != 0) {
                    p.image_lc = lower(std::wstring(buf, n));
                }
                FILETIME created{};
                FILETIME exited{};
                FILETIME kernel{};
                FILETIME user{};
                if (::GetProcessTimes(h, &created, &exited, &kernel, &user) != 0) {
                    p.started = as_u64(created);
                }
                ::CloseHandle(h);
            }
            out.push_back(std::move(p));
        } while (::Process32NextW(snap, &e) != 0);
    }
    ::CloseHandle(snap);
    return out;
}

bool starts_with(const std::wstring& s, const std::wstring& prefix) {
    return !prefix.empty() && s.size() >= prefix.size()
           && s.compare(0, prefix.size(), prefix) == 0;
}

// Rebuild ``owner_of``, dropping groups that have gone empty. Caller
// holds the lock, and gets each retired launch's driver staging through
// ``retired`` to undo after releasing it — removing files is not work to
// do while the window enumeration is waiting on this mutex.
//
// Three ways a process is attributed to a launch, because no one of them
// covers what real applications do:
//
//   * Job membership. Exact and immediate, and covers everything a
//     launch spawns directly.
//   * Descent from a process already attributed. Catches a child created
//     outside the job — a stub that re-launches itself through a broker
//     (the shell, the elevation service) leaves the new process outside
//     the job with its parent link intact.
//   * Running out of the launched executable's own directory tree, and
//     started after the launch did. The backstop for when even the
//     parent link is gone: portable builds and self-extracting launchers
//     hand off through intermediates that exit immediately, and by the
//     time the real application's window appears there may be nothing
//     left connecting it to what we started except where it lives.
//
// Attribution is sticky: a process keeps the launch it was assigned to
// for as long as it lives, so an intermediate exiting cannot orphan the
// application it started.
void rebuild_locked(State& s, std::vector<GuestDrivers::Staging>* retired) {
    const std::vector<Proc> procs = scan_processes();

    std::unordered_map<unsigned long, unsigned long> next;
    std::unordered_map<unsigned long, const Proc*> by_pid;
    by_pid.reserve(procs.size());
    for (const Proc& p : procs) {
        by_pid[p.pid] = &p;
    }

    // Carry forward previous attributions for processes that still
    // exist. This is what makes it sticky.
    for (const auto& [pid, root] : s.owner_of) {
        if (by_pid.count(pid) != 0 && s.groups.count(root) != 0) {
            next[pid] = root;
        }
    }

    for (auto it = s.groups.begin(); it != s.groups.end();) {
        const unsigned long root = it->first;
        for (const unsigned long pid : pids_in(it->second.job)) {
            next[pid] = root;
        }
        // The launched process itself, whether or not it is still alive:
        // a window can be enumerated in the same instant the stub exits.
        next.emplace(root, root);

        if (!it->second.dir_lc.empty()) {
            for (const Proc& p : procs) {
                if (next.count(p.pid) != 0) {
                    continue;
                }
                if (starts_with(p.image_lc, it->second.dir_lc)
                    && p.started >= it->second.started) {
                    next[p.pid] = root;
                }
            }
        }
        ++it;
    }

    // Walk parent links until nothing new is attributed. Bounded by the
    // process count; in practice one or two passes.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const Proc& p : procs) {
            if (next.count(p.pid) != 0) {
                continue;
            }
            const auto parent = next.find(p.ppid);
            if (parent == next.end()) {
                continue;
            }
            // PID reuse: a recycled parent PID would otherwise adopt
            // processes older than the launch itself.
            const auto* pp = by_pid.count(p.ppid) != 0 ? by_pid[p.ppid] : nullptr;
            if (pp != nullptr && p.started != 0 && pp->started != 0
                && p.started < pp->started) {
                continue;
            }
            next[p.pid] = parent->second;
            changed = true;
        }
        if (!changed) {
            break;
        }
    }

    // Retire launches with nothing left running.
    for (auto it = s.groups.begin(); it != s.groups.end();) {
        bool any = false;
        for (const auto& [pid, root] : next) {
            if (root == it->first && by_pid.count(pid) != 0) {
                any = true;
                break;
            }
        }
        if (!any) {
            // Closing the handle destroys the job. Nothing is running in
            // it to be affected — and it carries no limits regardless.
            ::CloseHandle(it->second.job);
            const unsigned long dead = it->first;
            if (retired != nullptr && !it->second.staging.dirs.empty()) {
                retired->push_back(std::move(it->second.staging));
            }
            it = s.groups.erase(it);
            for (auto n = next.begin(); n != next.end();) {
                n = n->second == dead ? next.erase(n) : std::next(n);
            }
            continue;
        }
        ++it;
    }

    s.owner_of = std::move(next);
    s.built = std::chrono::steady_clock::now();
}

void refresh_locked(State& s, std::vector<GuestDrivers::Staging>* retired) {
    if (std::chrono::steady_clock::now() - s.built < kSnapshotMaxAge) {
        return;
    }
    rebuild_locked(s, retired);
}

// Undo what the launches that just retired had staged. Called with the
// state lock released.
void unstage_all(const std::vector<GuestDrivers::Staging>& retired) {
    for (const GuestDrivers::Staging& staging : retired) {
        GuestDrivers::unstage(staging);
    }
}

// How often a launch is checked for having ended.
//
// Retirement used to happen only inside ``root_of``, which the host
// calls once per enumerated window. That makes the one moment it is
// needed — the last window of the last launch closing — the one moment
// nothing calls it again, and an application's drivers stayed staged
// until some unrelated launch happened to enumerate a window. So the
// check gets its own clock: one process scan a second while any launch
// is open, and a sleeping thread the rest of the time.
constexpr auto kReapInterval = std::chrono::seconds(1);

struct Reaper {
    std::thread thread;
    std::atomic<bool> stop{false};

    ~Reaper() {
        stop.store(true);
        if (thread.joinable()) {
            thread.join();
        }
    }
};

void start_reaper_once() {
    static Reaper reaper;
    static const bool started = [] {
        reaper.thread = std::thread{[] {
            while (!reaper.stop.load()) {
                std::this_thread::sleep_for(kReapInterval);
                std::vector<GuestDrivers::Staging> retired;
                {
                    State& s = state();
                    const std::lock_guard lock(s.mu);
                    if (s.groups.empty()) {
                        continue;  // nothing open: no scan, no cost
                    }
                    rebuild_locked(s, &retired);
                }
                unstage_all(retired);
            }
        }};
        return true;
    }();
    (void)started;
}

}  // namespace

std::string LaunchGroup::job_name(unsigned long pid) {
    // Session-local: the mirror runs in the same session as the agent,
    // and a global name would collide across sessions on a machine with
    // more than one user signed in.
    return "Local\\lsw-launch-" + std::to_string(pid);
}

bool LaunchGroup::adopt(void* process, unsigned long pid, GuestDrivers::Staging staging) {
    Group g;
    g.staging = std::move(staging);
    // Record where and when before anything can be attributed, so the
    // directory rule has a floor to compare against.
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetProcessTimes(static_cast<HANDLE>(process), &created, &exited, &kernel,
                          &user)
        != 0) {
        g.started = as_u64(created);
    }
    {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = MAX_PATH * 2;
        if (::QueryFullProcessImageNameW(static_cast<HANDLE>(process), 0, buf, &n) != 0) {
            const std::wstring image = lower(std::wstring(buf, n));
            const auto slash = image.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                g.dir_lc = image.substr(0, slash + 1);
            }
        }
    }

    const std::wstring name = util::TextEncoding::to_wide(job_name(pid));
    g.job = ::CreateJobObjectW(nullptr, name.c_str());
    if (g.job == nullptr) {
        LSW_AGENT_WARN(std::string{"launch_group: CreateJobObject failed (err="}
                       + std::to_string(::GetLastError())
                       + "); this launch is tracked by parentage and image path only");
    } else if (::AssignProcessToJobObject(g.job, static_cast<HANDLE>(process)) == 0) {
        LSW_AGENT_WARN(std::string{"launch_group: AssignProcessToJobObject failed (err="}
                       + std::to_string(::GetLastError()) + ") for pid "
                       + std::to_string(pid));
        ::CloseHandle(g.job);
        g.job = nullptr;
    }

    std::vector<GuestDrivers::Staging> retired;
    {
        State& s = state();
        const std::lock_guard lock(s.mu);
        // A PID the OS has recycled onto a new launch: the old group is
        // stale by definition, so let go of it rather than answering with it.
        if (auto old = s.groups.find(pid); old != s.groups.end()) {
            if (old->second.job != nullptr) {
                ::CloseHandle(old->second.job);
            }
            retired.push_back(std::move(old->second.staging));
            s.groups.erase(old);
        }
        s.groups[pid] = std::move(g);
        // The launch's own PID answers immediately — before any scan runs —
        // because a window can appear in the very next enumeration.
        s.owner_of[pid] = pid;
        // Force the next lookup to scan: this launch is not in the snapshot.
        s.built = {};
    }
    // After the new staging was recorded, so a directory both launches
    // share keeps its links.
    unstage_all(retired);
    // Cheap after the first call, and there is nothing to reap until a
    // launch exists.
    start_reaper_once();
    return true;
}

unsigned long LaunchGroup::root_of(unsigned long pid) {
    if (pid == 0) {
        return 0;
    }
    std::vector<GuestDrivers::Staging> retired;
    unsigned long root = 0;
    {
        State& s = state();
        const std::lock_guard lock(s.mu);
        if (s.groups.empty()) {
            return 0;
        }
        refresh_locked(s, &retired);
        const auto it = s.owner_of.find(pid);
        root = it == s.owner_of.end() ? 0 : it->second;
    }
    unstage_all(retired);
    return root;
}

bool LaunchGroup::alive(unsigned long root) {
    if (root == 0) {
        return false;
    }
    std::vector<GuestDrivers::Staging> retired;
    bool still_alive = false;
    {
        State& s = state();
        const std::lock_guard lock(s.mu);
        if (s.groups.find(root) == s.groups.end()) {
            return false;
        }
        // Deliberately not the cached snapshot: the caller is deciding
        // whether a launch has finished, and acting a fifth of a second
        // early on that is worse than one extra scan.
        rebuild_locked(s, &retired);
        still_alive = s.groups.find(root) != s.groups.end();
    }
    unstage_all(retired);
    return still_alive;
}

}  // namespace lsw::agent::apps
