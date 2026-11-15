#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d10 {
class RenderChecks {
public:
    [[nodiscard]] static Outcome pipeline_warp(Context& context);

    [[nodiscard]] static Outcome streamout(Context& context);

    [[nodiscard]] static Outcome map_contracts(Context& context);
};
}  // namespace lsw::suites::sanity::d3d10
