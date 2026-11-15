#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d11 {
class RenderChecks {
public:
    [[nodiscard]] static Outcome basic_stages(Context& context);

    [[nodiscard]] static Outcome states(Context& context);

    [[nodiscard]] static Outcome msaa(Context& context);

    [[nodiscard]] static Outcome tessellation(Context& context);

    [[nodiscard]] static Outcome geometry_streamout(Context& context);

    [[nodiscard]] static Outcome format_sweep(Context& context);

    [[nodiscard]] static Outcome depth_stencil_formats(Context& context);

    [[nodiscard]] static Outcome multi_rt_viewports(Context& context);

    [[nodiscard]] static Outcome resource_hazards(Context& context);
};
}  // namespace lsw::suites::sanity::d3d11
