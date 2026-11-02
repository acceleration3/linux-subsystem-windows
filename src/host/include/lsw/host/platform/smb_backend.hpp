#pragma once

#include "lsw/host/platform/smb_session.hpp"

#include <memory>

namespace lsw::host::platform {
class SmbBackend {
public:
    SmbBackend() = default;
    virtual ~SmbBackend() = default;
    SmbBackend(const SmbBackend&) = delete;
    SmbBackend& operator=(const SmbBackend&) = delete;

    [[nodiscard]] virtual std::unique_ptr<SmbSession> connect(int& error) = 0;
};

}  // namespace lsw::host::platform
