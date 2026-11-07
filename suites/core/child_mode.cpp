#include "core/child_mode.hpp"

#include <windows.h>

namespace lsw::suites {

bool ChildMode::active() {
    return ::GetEnvironmentVariableW(kVariable, nullptr, 0) != 0;
}

}  // namespace lsw::suites
