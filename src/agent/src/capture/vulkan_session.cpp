#include "lsw/agent/capture/vulkan_session.hpp"

#include "lsw/agent/apps/launch_group.hpp"
#include "lsw/agent/capture/wgc.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/transport/shmem.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <mutex>
#include <string>
#include <thread>

namespace lsw::agent::capture {

namespace {

std::mutex& mu() noexcept {
    static std::mutex m;
    return m;
}

// How many launched Vulkan applications are still being watched.
//
// The driver allows one at a time, so this is 0 or 1 in practice. It is
// a count rather than a flag because two launches can overlap — the
// second one's process starts before the first one's exit is noticed —
// and taking the region back while the second is still running would
// break it. Whoever leaves last hands the region back.
int live_count = 0;

}  // namespace

bool VulkanSession::begin_launch() {
    const std::lock_guard lock(mu());
    ++live_count;
    if (live_count > 1) {
        // Already released for an earlier launch.
        return true;
    }
    // Sessions first, region second. A slot is freed by writing to the
    // region, so the order is not a preference: after the release
    // there is nothing to write to, and the manager would keep mirrors
    // bound to slots nobody owns — a window that stays black because
    // its frames stopped without anything saying so.
    Wgc::drop_all_sessions();
    transport::shmem().release();
    return !transport::shmem().available();
}

void VulkanSession::abandon_launch() {
    bool take_back = false;
    {
        const std::lock_guard lock(mu());
        if (live_count > 0) {
            --live_count;
        }
        take_back = live_count == 0;
    }
    if (take_back) {
        (void)transport::shmem().reacquire();
    }
}

void VulkanSession::watch(unsigned long pid) {
    if (pid == 0) {
        // No process to wait on. Take the region back rather than
        // leaving it released forever on a launch we cannot follow.
        abandon_launch();
        return;
    }
    std::thread([pid] {
        // SYNCHRONIZE is all a wait needs, and asking for no more means
        // this works against a process the agent does not own.
        HANDLE proc = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (proc == nullptr) {
            LSW_AGENT_WARN(std::string{"vulkan_session: cannot watch pid "}
                           + std::to_string(pid) + " (err="
                           + std::to_string(::GetLastError())
                           + "); taking the region back now");
            VulkanSession::abandon_launch();
            return;
        }
        ::WaitForSingleObject(proc, INFINITE);
        ::CloseHandle(proc);
        // The launched process is gone, which is not the same as the
        // application being gone: a launcher stub exits as soon as it
        // has started the real executable. Taking the region back then
        // would pull the transport out from under an application that
        // is still rendering. Wait for the whole launch to empty out.
        if (apps::LaunchGroup::alive(pid)) {
            LSW_AGENT_INFO(std::string{"vulkan_session: pid "} + std::to_string(pid)
                           + " exited but its launch is still running; holding the "
                             "region");
            while (apps::LaunchGroup::alive(pid)) {
                ::Sleep(500);
            }
        }
        LSW_AGENT_INFO(std::string{"vulkan_session: launch "} + std::to_string(pid)
                       + " finished");
        VulkanSession::abandon_launch();
    }).detach();
}

bool VulkanSession::active() {
    const std::lock_guard lock(mu());
    return live_count > 0;
}

}  // namespace lsw::agent::capture
