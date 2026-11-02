#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lsw::agent::util {
class WideString {
public:
    WideString() = delete;

    [[nodiscard]] static std::wstring lower(std::wstring s);
    [[nodiscard]] static std::wstring upper(std::wstring s);

    [[nodiscard]] static bool istarts_with(std::wstring_view s, std::wstring_view prefix);
    [[nodiscard]] static bool iends_with(std::wstring_view s, std::wstring_view suffix);

    [[nodiscard]] static std::optional<std::wstring> parent_dir(const std::wstring& path);
};

}
