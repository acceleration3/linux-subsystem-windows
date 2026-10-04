#pragma once

#include <string>

namespace lsw::agent::win {

// Questions about the guest's file system that need no handle.
class FileSystem {
public:
    FileSystem() = delete;

    // A file or directory exists at ``path``. False for an empty path,
    // and for one the agent is not allowed to see.
    [[nodiscard]] static bool exists(const std::wstring& path);
};

}  // namespace lsw::agent::win
