#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class PsoChecks {
public:
    [[nodiscard]] static Outcome graphics_compute(Context& context);

    [[nodiscard]] static Outcome library_cache(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
