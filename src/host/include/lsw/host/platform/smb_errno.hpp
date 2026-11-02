#pragma once

namespace lsw::host::platform {
class SmbErrno {
public:
    SmbErrno() = delete;

    [[nodiscard]] static int map(int error);

    [[nodiscard]] static bool connection_lost(int error);
};

}  // namespace lsw::host::platform
