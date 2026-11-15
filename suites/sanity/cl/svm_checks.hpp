#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::cl {
class SvmChecks {
public:
    [[nodiscard]] static Outcome coarse_grain(Context& context);

    [[nodiscard]] static Outcome fine_grain_caps(Context& context);
};
}  // namespace lsw::suites::sanity::cl
