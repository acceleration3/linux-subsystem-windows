#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class ClChecks {
public:
    [[nodiscard]] static Outcome c_and_il(Context& context);
};
}  // namespace lsw::suites::sanity::shader
