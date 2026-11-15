#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class ValueChecks {
public:
    [[nodiscard]] static Outcome float_special_values(Context& context);

    [[nodiscard]] static Outcome snorm_and_clamp(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
