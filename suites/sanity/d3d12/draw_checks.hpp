#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class DrawChecks {
public:
    [[nodiscard]] static Outcome draw_variants(Context& context);

    [[nodiscard]] static Outcome msaa_resolve(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
