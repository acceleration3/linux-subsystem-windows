#pragma once

#include <filesystem>
#include <string>

namespace lsw::agent::win {
class FileDigest {
public:
    FileDigest() = delete;

    [[nodiscard]] static std::string sha256(const std::filesystem::path& path);
};

}
