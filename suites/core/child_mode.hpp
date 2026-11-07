#pragma once

#include <string>

namespace lsw::suites {
class ChildMode {
public:
    static constexpr const wchar_t* kVariable = L"LSW_SUITES_CHILD";

    [[nodiscard]] static bool active();

    [[nodiscard]] static std::string variable(const wchar_t* name);
};
}  // namespace lsw::suites
