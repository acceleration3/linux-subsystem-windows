#pragma once

#include <string>

namespace lsw::agent::win {

// Pulls an application icon out of the guest and hands it back as a
// base64 PNG, ready to drop into an RPC reply.
class IconExtractor {
public:
    IconExtractor() = delete;

    // Largest icon resource in ``path``, as base64 PNG. Empty on failure.
    [[nodiscard]] static std::string from_executable(const std::wstring& path);
};

}  // namespace lsw::agent::win
