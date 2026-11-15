#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

namespace lsw::suites::sanity::d3d9 {
class RenderChecks {
public:
    [[nodiscard]] static Outcome fixed_function(Context& context);

    [[nodiscard]] static Outcome shaders(Context& context);

    [[nodiscard]] static Outcome states_and_blend(Context& context);

    [[nodiscard]] static Outcome multisample(Context& context);

    [[nodiscard]] static Outcome draw_variants(Context& context);
};
}  // namespace lsw::suites::sanity::d3d9
