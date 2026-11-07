#pragma once

namespace lsw::suites {
class ChildMode {
public:
    static constexpr const wchar_t* kVariable = L"LSW_SUITES_CHILD";

    [[nodiscard]] static bool active();
};
}  // namespace lsw::suites
