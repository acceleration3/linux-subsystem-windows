#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class SpirvChecks {
public:
    [[nodiscard]] static Outcome valid_runs(Context& context);

    [[nodiscard]] static Outcome invalid_fails(Context& context);
};
}  // namespace lsw::suites::sanity::shader
