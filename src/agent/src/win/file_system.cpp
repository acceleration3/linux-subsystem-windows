#include "lsw/agent/win/file_system.hpp"

#include <windows.h>

namespace lsw::agent::win {

bool FileSystem::exists(const std::wstring& path) {
    return !path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace lsw::agent::win
