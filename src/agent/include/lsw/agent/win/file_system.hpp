#pragma once

#include <string>

namespace lsw::agent::win {
class FileSystem {
public:
    FileSystem() = delete;

    [[nodiscard]] static bool exists(const std::wstring& path);
};

}
