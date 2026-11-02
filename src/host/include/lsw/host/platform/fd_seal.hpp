#pragma once

namespace lsw::host::platform {
class FdSeal {
public:
    FdSeal() = delete;

    static int close_on_exec();
};

}  // namespace lsw::host::platform
