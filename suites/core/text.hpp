#pragma once

#include <string>

namespace lsw::suites {
class Text {
public:
    [[nodiscard]] static std::wstring wide(const std::string& utf8);

    [[nodiscard]] static std::string utf8(const std::wstring& wide);

    [[nodiscard]] static std::string upper(std::string text);

    [[nodiscard]] static std::wstring upper(std::wstring text);

    [[nodiscard]] static std::string escape(const std::string& text);

    [[nodiscard]] static std::string unescape(const std::string& text);

    [[nodiscard]] static std::string hex(unsigned long long value, int digits);
};
}  // namespace lsw::suites
