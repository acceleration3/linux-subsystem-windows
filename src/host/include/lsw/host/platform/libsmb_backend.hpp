#pragma once

#include "lsw/host/platform/smb_backend.hpp"

#include <string>
#include <utility>

namespace lsw::host::platform {
class LibsmbBackend : public SmbBackend {
public:
    LibsmbBackend(std::string server, std::string share, std::string user, std::string password,
                  int timeout_ms);

    [[nodiscard]] std::unique_ptr<SmbSession> connect(int& error) override;

private:
    std::string base_;
    std::pair<std::string, std::string> credentials_;
    int timeout_ms_;
};

}  // namespace lsw::host::platform
