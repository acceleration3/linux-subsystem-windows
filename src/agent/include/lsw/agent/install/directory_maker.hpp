#pragma once

#include <string>

namespace lsw::agent::install {

// Recursive directory create and remove.
//
// Hand-rolled rather than SHCreateDirectoryExW so the agent does not
// have to link shell32 for two calls.
class DirectoryMaker {
public:
    DirectoryMaker() = delete;

    // mkdir -p. Best-effort; existing directories are not an error.
    static void make_path(const std::wstring& path);

    // rm -rf, via ``cmd /c rd /s /q``. Best-effort; failures ignored.
    static void remove_tree(const std::wstring& path);
};

}  // namespace lsw::agent::install
