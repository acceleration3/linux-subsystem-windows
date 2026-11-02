#include "lsw/agent/system/driver_log_pruner.hpp"

#include "lsw/agent/core/paths.hpp"
#include "lsw/agent/log.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::system {

void DriverLogPruner::run() {
    const std::wstring dir = core::Paths::driver_logs_dir();
    WIN32_FIND_DATAW found{};
    const HANDLE find = ::FindFirstFileW((dir + L"\\*.log").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    struct Entry {
        std::uint64_t written;
        std::wstring name;
    };
    std::vector<Entry> logs;
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            logs.push_back({(static_cast<std::uint64_t>(found.ftLastWriteTime.dwHighDateTime) << 32) |
                                found.ftLastWriteTime.dwLowDateTime,
                            found.cFileName});
        }
    } while (::FindNextFileW(find, &found) != FALSE);
    ::FindClose(find);

    if (logs.size() <= static_cast<std::size_t>(kKeep)) {
        return;
    }
    std::sort(logs.begin(), logs.end(),
              [](const Entry& a, const Entry& b) { return a.written > b.written; });
    int removed = 0;
    for (std::size_t i = kKeep; i < logs.size(); ++i) {
        removed += ::DeleteFileW((dir + L"\\" + logs[i].name).c_str()) != FALSE ? 1 : 0;
    }
    LSW_AGENT_INFO("driver logs: removed {} of {} in {}", removed, logs.size() - kKeep,
                   "C:\\ProgramData\\av");
}

}
