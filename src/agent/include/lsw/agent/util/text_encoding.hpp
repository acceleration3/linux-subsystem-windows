#pragma once

#include <string>

namespace lsw::agent::util {

// UTF-8 <-> UTF-16 conversion. Win32 is wide-char, the RPC wire is
// UTF-8, and the boundary between them is crossed constantly — so it
// gets one owner rather than a copy in every translation unit.
class TextEncoding {
public:
    TextEncoding() = delete;

    [[nodiscard]] static std::wstring to_wide(const std::string& utf8);
    [[nodiscard]] static std::string to_utf8(const std::wstring& wide);
};

}  // namespace lsw::agent::util
