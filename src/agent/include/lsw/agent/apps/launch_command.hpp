#pragma once

#include <string>

namespace lsw::agent::apps {

// Selects the Windows runner while retaining a trackable process/job.
class LaunchCommand {
public:
    LaunchCommand(const std::wstring& path, const std::wstring& windows_dir,
                  const std::wstring& args);

    std::wstring application;
    std::wstring command_line;
};

}  // namespace lsw::agent::apps
