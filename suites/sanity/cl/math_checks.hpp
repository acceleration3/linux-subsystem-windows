#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class MathChecks {
public:
    [[nodiscard]] static Outcome arithmetic_vs_reference(Context& context);

    [[nodiscard]] static Outcome doubles_caps(Context& context);
};
}  // namespace lsw::suites::sanity::cl
