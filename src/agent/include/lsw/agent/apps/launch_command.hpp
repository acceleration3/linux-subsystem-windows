#pragma once

#include <string>

namespace lsw::agent::apps {
class LaunchCommand {
public:
    LaunchCommand(const std::wstring& path, const std::wstring& windows_dir,
                  const std::wstring& args);

    std::wstring application;
    std::wstring command_line;
};

}
