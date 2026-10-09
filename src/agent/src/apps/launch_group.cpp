#include "lsw/agent/apps/launch_group.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/util/text_encoding.hpp"
#include "lsw/agent/util/wide_string.hpp"
#include "lsw/agent/win/error_text.hpp"
#include "lsw/agent/win/process_info.hpp"
#include "lsw/agent/win/security_attributes.hpp"
#include "lsw/agent/win/unique_handle.hpp"

#include <chrono>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace lsw::agent::apps {

namespace {

// How stale a membership snapshot may be before the next lookup rebuilds
// it. The window enumeration runs at ~45 Hz and asks about every window
// it found, so answering each one with a fresh scan would turn one poll
// into dozens; a snapshot this old can at worst delay a mirror by one
// poll.
constexpr auto kSnapshotMaxAge = std::chrono::milliseconds(200);

// How long a launch is kept with nothing of it running, in FILETIME ticks
// (100 ns): a stub that relaunches itself through the shell has exited
// before the shell starts the replacement. The mirror gives an application
// as long (remotewin's LaunchScope), and reads the launch from the process
// held here to recognise the replacement.
constexpr std::uint64_t kRelaunchTicks = 5ULL * 10'000'000ULL;

std::uint64_t now_ticks() {
    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    return (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
}

struct Group {
    // The launched process, held after it exits: its image and start time
    // stay readable while a handle is open, and the mirror reads them from
    // the PID to find the rest of the application. Firefox started elevated
    // relaunches itself unelevated through Explorer - outside the job, and
    // not its child - and exits within a tenth of a second, before the
    // mirror is up. Held, the PID also cannot be recycled onto another
    // process while the launch lives.
    win::UniqueHandle process;
    // Empty when the job could not be created or assigned; the launch is
    // then tracked by parentage and image path alone.
    win::UniqueHandle job;
    // The launched executable's own directory, lowercased and ending in
    // a separator. Anything running out of this tree is this launch.
    std::wstring dir_lc;
    // When the launch started. Nothing older can belong to it, which is
    // what keeps the directory rule from adopting an instance the user
    // already had open.
    std::uint64_t started = 0;
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
    // Never destroyed: a process exiting has nothing to gain from
    // tearing this down first.
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
        const DWORD bytes = static_cast<DWORD>(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
                                               (capacity - 1) * sizeof(ULONG_PTR));
        std::vector<unsigned char> buf(bytes, 0);
        auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
        DWORD returned = 0;
        const BOOL ok =
            ::QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, bytes, &returned);
        if (ok == 0 && ::GetLastError() != ERROR_MORE_DATA) {
            return {};
        }
        if (ok == 0 || list->NumberOfProcessIdsInList < list->NumberOfAssignedProcesses) {
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
    for (const win::ProcessInfo::Entry& e : win::ProcessInfo::snapshot()) {
        Proc p;
        p.pid = e.pid;
        p.ppid = e.parent;
        const win::UniqueHandle h{::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid)};
        if (h) {
            p.image_lc = util::WideString::lower(win::ProcessInfo::image_path(h.get()));
            p.started = win::ProcessInfo::creation_time(h.get()).value_or(0);
        }
        out.push_back(std::move(p));
    }
    return out;
}

// Rebuild ``owner_of``, dropping groups that have gone empty. Caller
// holds the lock.
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
void rebuild_locked(State& s) {
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
        // Never asked of a null handle: QueryInformationJobObject reads
        // that as "the job this process is in", which is the agent's own
        // whenever something started it inside one.
        if (it->second.job) {
            for (const unsigned long pid : pids_in(it->second.job.get())) {
                next[pid] = root;
            }
        }
        // The launched process itself, whether or not it is still alive:
        // a window can be enumerated in the same instant the stub exits.
        next.emplace(root, root);

        if (!it->second.dir_lc.empty()) {
            for (const Proc& p : procs) {
                if (next.count(p.pid) != 0) {
                    continue;
                }
                if (p.image_lc.starts_with(it->second.dir_lc) && p.started >= it->second.started) {
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
            if (pp != nullptr && p.started != 0 && pp->started != 0 && p.started < pp->started) {
                continue;
            }
            next[p.pid] = parent->second;
            changed = true;
        }
        if (!changed) {
            break;
        }
    }

    // Retire launches with nothing left running, once past the relaunch.
    const std::uint64_t now = now_ticks();
    for (auto it = s.groups.begin(); it != s.groups.end();) {
        if (it->second.started != 0 && now - it->second.started < kRelaunchTicks) {
            ++it;
            continue;
        }
        bool any = false;
        for (const auto& [pid, root] : next) {
            if (root == it->first && by_pid.count(pid) != 0) {
                any = true;
                break;
            }
        }
        if (!any) {
            // Erasing the group closes its handle, which destroys the job.
            // Nothing is running in it to be affected — and it carries no
            // limits regardless.
            const unsigned long dead = it->first;
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

void refresh_locked(State& s) {
    if (std::chrono::steady_clock::now() - s.built < kSnapshotMaxAge) {
        return;
    }
    rebuild_locked(s);
}

}  // namespace

std::string LaunchGroup::job_name(unsigned long pid) {
    // Global: the agent creates the job from session 0, and the mirror
    // that opens it runs in the application's session — a Local\ name
    // would be looked up in the mirror's own session's namespace and not
    // found. A PID is unique machine-wide while the process lives, so
    // launches in two sessions cannot collide.
    return "Global\\lsw-launch-" + std::to_string(pid);
}

bool LaunchGroup::adopt(void* process, unsigned long pid) {
    Group g;
    // Record where and when before anything can be attributed, so the
    // directory rule has a floor to compare against.
    const auto handle = static_cast<HANDLE>(process);
    if (::DuplicateHandle(::GetCurrentProcess(), handle, ::GetCurrentProcess(),
                          g.process.put(), PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0) == 0) {
        LSW_AGENT_WARN("launch_group: cannot hold pid {} (err={}); a mirror started after it "
                       "exits will not find the application by its directory",
                       pid, win::ErrorText::last_error());
    }
    g.started = win::ProcessInfo::creation_time(handle).value_or(0);
    const std::wstring image = util::WideString::lower(win::ProcessInfo::image_path(handle));
    if (const auto dir = util::WideString::parent_dir(image)) {
        g.dir_lc = image.substr(0, dir->size() + 1);  // keep the separator
    }

    // The job is created as SYSTEM, whose default DACL would leave the
    // user's mirror unable to open it. Query is all anyone else needs:
    // SYSTEM and administrators keep full control, interactive users
    // (which is who the mirror runs as, elevated or not) get
    // JOB_OBJECT_QUERY | READ_CONTROL | SYNCHRONIZE (0x120004) — spelled
    // out rather than GR, so nothing depends on the job type's generic
    // mapping.
    const std::wstring name = util::TextEncoding::to_wide(job_name(pid));
    win::SecurityAttributes security{L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x120004;;;IU)"};
    g.job.reset(::CreateJobObjectW(security.ok() ? security.get() : nullptr, name.c_str()));
    if (!g.job) {
        LSW_AGENT_WARN("launch_group: CreateJobObject failed (err={}); this launch is tracked "
                       "by parentage and image path only",
                       win::ErrorText::last_error());
    } else if (::AssignProcessToJobObject(g.job.get(), static_cast<HANDLE>(process)) == 0) {
        LSW_AGENT_WARN("launch_group: AssignProcessToJobObject failed (err={}) for pid {}",
                       win::ErrorText::last_error(), pid);
        g.job.reset();
    }

    State& s = state();
    const std::lock_guard lock(s.mu);
    // A PID the OS has recycled onto a new launch: the old group is
    // stale by definition, so let go of it rather than answering with it.
    s.groups.erase(pid);
    s.groups[pid] = std::move(g);
    // The launch's own PID answers immediately — before any scan runs —
    // because a window can appear in the very next enumeration.
    s.owner_of[pid] = pid;
    // Force the next lookup to scan: this launch is not in the snapshot.
    s.built = {};
    return true;
}

unsigned long LaunchGroup::root_of(unsigned long pid) {
    if (pid == 0) {
        return 0;
    }
    State& s = state();
    const std::lock_guard lock(s.mu);
    if (s.groups.empty()) {
        return 0;
    }
    refresh_locked(s);
    const auto it = s.owner_of.find(pid);
    return it == s.owner_of.end() ? 0 : it->second;
}

}  // namespace lsw::agent::apps
