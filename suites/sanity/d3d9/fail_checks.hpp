#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class FailChecks {
public:
    [[nodiscard]] static Outcome unsupported_features(Context& context);

    [[nodiscard]] static Outcome bad_parameters(Context& context);

    [[nodiscard]] static Outcome lifetime(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
