#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d12 {
class OptChecks {
public:
    [[nodiscard]] static Outcome raytracing(Context& context);

    [[nodiscard]] static Outcome mesh_shaders(Context& context);

    [[nodiscard]] static Outcome vrs_sampler_feedback(Context& context);

    [[nodiscard]] static Outcome work_graphs_dxr11(Context& context);
};
}  // namespace lsw::suites::sanity::d3d12
