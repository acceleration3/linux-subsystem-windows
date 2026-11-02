#pragma once

#include <string>

namespace lsw::host::platform {
class SmbPath {
public:
    SmbPath() = delete;

    [[nodiscard]] static int check(const std::string& path);

    [[nodiscard]] static std::string url(const std::string& base, const std::string& path);
};

}  // namespace lsw::host::platform
