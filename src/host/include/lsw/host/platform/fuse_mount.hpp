#pragma once

#include "lsw/host/platform/windows_fs_stack.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace lsw::host::platform {
class FuseMount {
public:
    struct Options {
        std::string mount_point;
        std::string fsname;
        double timeout = 1.0;
        int max_threads = 16;
    };

    struct State;

    FuseMount(std::shared_ptr<WindowsFsStack> stack, Options options);
    ~FuseMount();
    FuseMount(const FuseMount&) = delete;
    FuseMount& operator=(const FuseMount&) = delete;

    [[nodiscard]] bool start(std::string& error);

    bool stop(std::chrono::milliseconds wait);

    [[nodiscard]] bool running() const;

    static int unmount_path(const std::string& mount_point, bool lazy,
                            std::chrono::milliseconds wait);

private:
    std::shared_ptr<State> state_;
};

}  // namespace lsw::host::platform
