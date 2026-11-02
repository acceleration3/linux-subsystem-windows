#pragma once

#include "lsw/host/platform/session_pool.hpp"
#include "lsw/host/platform/smb_backend.hpp"
#include "lsw/host/platform/windows_fs.hpp"

#include <cstddef>
#include <memory>
#include <sys/types.h>

namespace lsw::host::platform {
class WindowsFsStack {
public:
    WindowsFsStack(std::unique_ptr<SmbBackend> backend, std::size_t sessions, uid_t uid, gid_t gid)
        : backend_(std::move(backend)), pool_(*backend_, sessions), fs_(pool_, uid, gid) {}

    [[nodiscard]] SmbBackend& backend() noexcept { return *backend_; }

    [[nodiscard]] SessionPool& pool() noexcept { return pool_; }

    [[nodiscard]] WindowsFs& fs() noexcept { return fs_; }

private:
    std::unique_ptr<SmbBackend> backend_;
    SessionPool pool_;
    WindowsFs fs_;
};

}  // namespace lsw::host::platform
