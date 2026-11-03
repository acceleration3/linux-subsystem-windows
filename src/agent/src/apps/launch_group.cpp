#include "lsw/agent/apps/launch_group.hpp"

#include "lsw/agent/apps/launch_watcher.hpp"
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
constexpr auto kSnapshotMaxAge = std::chrono::milliseconds(200);

constexpr std::uint64_t kRelaunchTicks = 5ULL * 10'000'000ULL;

std::uint64_t now_ticks() {
    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    return (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
}

struct Group {
    win::UniqueHandle process;
    win::UniqueHandle job;
    std::wstring dir_lc;
    std::uint64_t started = 0;
};

struct Proc {
    unsigned long pid = 0;
    unsigned long ppid = 0;
    std::wstring image_lc;
    std::uint64_t started = 0;
};

struct State {
    std::mutex mu;
    std::unordered_map<unsigned long, Group> groups;
    std::unordered_map<unsigned long, unsigned long> owner_of;
    std::chrono::steady_clock::time_point built{};
};

State& state() {
    static State* s = new State{};
    return *s;
}

std::vector<unsigned long> pids_in(HANDLE job) {
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

void rebuild_locked(State& s) {
    const std::vector<Proc> procs = scan_processes();

    std::unordered_map<unsigned long, unsigned long> next;
    std::unordered_map<unsigned long, const Proc*> by_pid;
    by_pid.reserve(procs.size());
    for (const Proc& p : procs) {
        by_pid[p.pid] = &p;
    }

    for (const auto& [pid, root] : s.owner_of) {
        if (by_pid.count(pid) != 0 && s.groups.count(root) != 0) {
            next[pid] = root;
        }
    }

    for (auto it = s.groups.begin(); it != s.groups.end();) {
        const unsigned long root = it->first;
        if (it->second.job) {
            for (const unsigned long pid : pids_in(it->second.job.get())) {
                next[pid] = root;
            }
        }
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

}

std::string LaunchGroup::job_name(unsigned long pid) {
    return "Global\\lsw-launch-" + std::to_string(pid);
}

bool LaunchGroup::adopt(void* process, unsigned long pid, LaunchIdentity identity) {
    Group g;
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
        g.dir_lc = image.substr(0, dir->size() + 1);
    }

    const std::wstring name = util::TextEncoding::to_wide(job_name(pid));
    win::SecurityAttributes security{L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x120004;;;IU)"};
    g.job.reset(::CreateJobObjectW(security.ok() ? security.get() : nullptr, name.c_str()));
    if (!g.job) {
        LSW_AGENT_WARN("launch_group: CreateJobObject failed (err={}); this launch is tracked "
                       "by parentage and image path only",
                       win::ErrorText::last_error());
    } else {
        (void)LaunchWatcher::shared().attach(g.job.get(), pid, std::move(identity));
        if (::AssignProcessToJobObject(g.job.get(), static_cast<HANDLE>(process)) == 0) {
            LSW_AGENT_WARN("launch_group: AssignProcessToJobObject failed (err={}) for pid {}",
                           win::ErrorText::last_error(), pid);
            g.job.reset();
        }
    }

    State& s = state();
    const std::lock_guard lock(s.mu);
    s.groups.erase(pid);
    s.groups[pid] = std::move(g);
    s.owner_of[pid] = pid;
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

}
