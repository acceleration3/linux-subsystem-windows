#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class DxilChecks {
public:
    [[nodiscard]] static Outcome sm6(Context& context);
};
}  // namespace lsw::suites::sanity::shader
