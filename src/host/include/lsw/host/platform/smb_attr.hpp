#pragma once

#include <cstdint>
#include <ctime>

namespace lsw::host::platform {
struct SmbAttr {
    bool directory = false;
    std::uint64_t size = 0;
    timespec atime{};
    timespec mtime{};
    timespec ctime{};
};

}  // namespace lsw::host::platform
