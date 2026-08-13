#pragma once

#include <string>

namespace lsw::agent::uia {

// Reads a COM ``BSTR`` into a UTF-8 std::string.
//
// UIA hands back BSTRs from a dozen different property getters; without
// one owner for the conversion each call site grows its own
// SysStringLen/WideCharToMultiByte dance, and one of them eventually
// forgets the length and truncates on an embedded NUL.
class BstrText {
public:
    BstrText() = delete;

    // ``bstr`` is a BSTR, typed as void* so this header stays free of
    // <windows.h>. Null and empty both yield an empty string. Does not
    // free the BSTR.
    [[nodiscard]] static std::string to_utf8(void* bstr);
};

}  // namespace lsw::agent::uia
