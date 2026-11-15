#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d10 {
class DxgiChecks {
public:
    [[nodiscard]] static Outcome swapchain(Context& context);

    [[nodiscard]] static Outcome interop_d3d11(Context& context);
};
}  // namespace lsw::suites::sanity::d3d10
