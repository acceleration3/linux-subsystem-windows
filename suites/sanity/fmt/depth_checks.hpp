#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::fmt {
class DepthChecks {
public:
    [[nodiscard]] static Outcome depth_stencil(Context& context);
};
}  // namespace lsw::suites::sanity::fmt
