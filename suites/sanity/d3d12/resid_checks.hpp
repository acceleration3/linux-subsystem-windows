#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class ResidChecks {
public:
    [[nodiscard]] static Outcome make_evict(Context& context);

    [[nodiscard]] static Outcome budget_pressure(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
