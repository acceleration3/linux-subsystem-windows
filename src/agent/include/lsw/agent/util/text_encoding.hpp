#pragma once

#include <string>

namespace lsw::agent::util {
class TextEncoding {
public:
    TextEncoding() = delete;

    [[nodiscard]] static std::wstring to_wide(const std::string& utf8);
    [[nodiscard]] static std::string to_utf8(const std::wstring& wide);
};

}
