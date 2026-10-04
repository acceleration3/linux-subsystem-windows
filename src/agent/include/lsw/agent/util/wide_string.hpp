#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lsw::agent::util {

// The handful of UTF-16 string operations Win32 paths need, in one place.
//
// Paths arrive wide and are compared case-insensitively — "C:\Foo.EXE"
// and "c:\foo.exe" are one file — and each file that compared them grew
// its own lowercase loop, suffix check and parent-directory split.
//
// Case folding is ``towlower`` / ``towupper``, character by character:
// no locale-aware collation, which is the right call for paths and
// extensions and the wrong one for anything shown to a person.
class WideString {
public:
    WideString() = delete;

    [[nodiscard]] static std::wstring lower(std::wstring s);
    [[nodiscard]] static std::wstring upper(std::wstring s);

    // Case-insensitive prefix / suffix tests. An empty needle matches.
    [[nodiscard]] static bool istarts_with(std::wstring_view s, std::wstring_view prefix);
    [[nodiscard]] static bool iends_with(std::wstring_view s, std::wstring_view suffix);

    // Everything before the last ``\`` or ``/``, or nullopt when there is
    // no separator at all. "\\foo.exe" has an empty parent, which is not
    // the same answer as "foo.exe" having none.
    [[nodiscard]] static std::optional<std::wstring> parent_dir(const std::wstring& path);
};

}  // namespace lsw::agent::util
