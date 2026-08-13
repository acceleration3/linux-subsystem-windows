#include "lsw/agent/install/directory_maker.hpp"

#include "lsw/agent/install/process_runner.hpp"

#include <windows.h>

namespace lsw::agent::install {

void DirectoryMaker::make_path(const std::wstring& path) {
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] != L'\\' && path[i] != L'/') {
            continue;
        }
        if (i == 0 || (i == 2 && path[1] == L':')) {
            continue;  // skip the drive root
        }
        ::CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
    }
    ::CreateDirectoryW(path.c_str(), nullptr);
}

void DirectoryMaker::remove_tree(const std::wstring& path) {
    (void)ProcessRunner::run(L"cmd.exe /c rd /s /q \"" + path + L"\"");
}

}  // namespace lsw::agent::install
