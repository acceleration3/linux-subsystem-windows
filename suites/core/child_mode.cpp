#include "core/child_mode.hpp"

#include "core/text.hpp"

#include <windows.h>

namespace lsw::suites {

bool ChildMode::active() {
    return ::GetEnvironmentVariableW(kVariable, nullptr, 0) != 0;
}

std::string ChildMode::variable(const wchar_t* name) {
    wchar_t buffer[1024];
    const DWORD length = ::GetEnvironmentVariableW(name, buffer, 1024);
    return length == 0 || length >= 1024 ? std::string() : Text::utf8(std::wstring(buffer, length));
}

}  // namespace lsw::suites
