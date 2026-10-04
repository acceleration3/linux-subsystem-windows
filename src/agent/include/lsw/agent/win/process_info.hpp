#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace lsw::agent::win {

// What the agent can learn about a running process without touching it:
// where its image lives, when it started, and the system-wide process
// list.
class ProcessInfo {
public:
    ProcessInfo() = delete;

    // One process as a Toolhelp snapshot lists it.
    struct Entry {
        DWORD pid = 0;
        DWORD parent = 0;
        std::wstring exe_name;  // file name only, no directory
    };

    // Full path of ``pid``'s image, or empty when the process cannot be
    // opened (gone, or protected). Sized for long paths: an application
    // installed deep under a long-path-enabled directory has an image
    // path past MAX_PATH, and the fixed buffers this replaced returned
    // nothing for it at all.
    [[nodiscard]] static std::wstring image_path(DWORD pid);

    // The same, for a handle opened with at least
    // PROCESS_QUERY_LIMITED_INFORMATION.
    [[nodiscard]] static std::wstring image_path(HANDLE process);

    // When the process started, as FILETIME ticks; nullopt when it
    // cannot be read.
    [[nodiscard]] static std::optional<std::uint64_t> creation_time(HANDLE process);

    // This process's own image path. Read once and cached: the window
    // enumeration compares every window it finds against it, at poll
    // rate.
    [[nodiscard]] static const std::wstring& self_path();

    // Every process on the system. Empty when the snapshot fails.
    [[nodiscard]] static std::vector<Entry> snapshot();
};

}  // namespace lsw::agent::win
