#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::shader {
class DeterminismChecks {
public:
    [[nodiscard]] static Outcome created_twice(Context& context);

    [[nodiscard]] static Outcome two_processes(Context& context);
};
}  // namespace lsw::suites::sanity::shader
